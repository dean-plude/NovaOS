/*
 * analyzer.c — DirectWrite text analysis and HarfBuzz-backed glyph shaping
 */
#include "dwrite_int.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

DEFINE_GUID(IID_IDWriteTextAnalyzer, 0xb7e6163e, 0x7f46, 0x43b4, 0x84,0xb3, 0xe4,0xe6,0x24,0x9c,0x36,0x5d);

typedef struct { UINT16 script, shapes; } ScriptAnalysis;
typedef struct {
    BYTE break_before : 2;
    BYTE break_after : 2;
    BYTE whitespace : 1;
    BYTE soft_hyphen : 1;
    BYTE padding : 2;
} LineBreakpoint;
typedef struct { UINT16 bits; } ShapingTextProperties;
typedef struct { UINT16 bits; } ShapingGlyphProperties;
typedef struct { INT32 x_advance, y_advance, x_offset, y_offset, var; } HbPosition;
typedef struct { UINT32 codepoint, mask, cluster, var1, var2; } HbInfo;

typedef struct {
    const void *const *vtbl;
    LONG ref;
    SRWLOCK cache_lock;
    WCHAR *cache_text;
    UINT32 cache_length;
    FontFace *cache_face;
    BOOL cache_rtl;
    DW_SHAPED_GLYPH *cache_glyphs;
    UINT32 cache_count;
} TextAnalyzer;

typedef struct {
    HMODULE module;
    INT32 (*script)(INT32, INT32 *);
    void *(*bidi_open)(INT32, INT32, INT32 *);
    void (*bidi_set_para)(void *, const UINT16 *, INT32, UINT8, void *, INT32 *);
    const UINT8 *(*bidi_levels)(void *, INT32 *);
    UINT8 (*bidi_para_level)(void *);
    void (*bidi_close)(void *);
    void *(*break_open)(INT32, const char *, const UINT16 *, INT32, INT32 *);
    INT32 (*break_first)(void *);
    INT32 (*break_next)(void *);
    INT32 (*break_status)(void *);
    void (*break_close)(void *);
} IcuText;

static IcuText g_icu;
static LONG g_icu_state;
static SRWLOCK g_icu_lock = SRWLOCK_INIT;

static BOOL icu_load(void)
{
    if (g_icu_state) return g_icu_state > 0;
    AcquireSRWLockExclusive(&g_icu_lock);
    if (!g_icu_state) {
        IcuText api = { 0 };
        HMODULE module = LoadLibraryExW(L"icu.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        static const char *const names[] = {
            "uscript_getScript", "ubidi_openSized", "ubidi_setPara", "ubidi_getLevels",
            "ubidi_getParaLevel", "ubidi_close", "ubrk_open", "ubrk_first", "ubrk_next",
            "ubrk_getRuleStatus", "ubrk_close"
        };
        void **slots = (void **)(void *)&api.script;
        BOOL ok = module != NULL;
        for (UINT32 i = 0; ok && i < sizeof(names) / sizeof(names[0]); i++)
            ok = (slots[i] = (void *)GetProcAddress(module, names[i])) != NULL;
        if (ok) {
            api.module = module;
            g_icu = api;
            g_icu_state = 1;
        } else {
            if (module) FreeLibrary(module);
            g_icu_state = -1;
        }
    }
    ReleaseSRWLockExclusive(&g_icu_lock);
    return g_icu_state > 0;
}

static HRESULT STDMETHODCALLTYPE analyzer_qi(TextAnalyzer *analyzer, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDWriteTextAnalyzer)) {
        *out = analyzer;
        InterlockedIncrement(&analyzer->ref);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE analyzer_addref(TextAnalyzer *analyzer)
{
    return (ULONG)InterlockedIncrement(&analyzer->ref);
}

static ULONG STDMETHODCALLTYPE analyzer_release(TextAnalyzer *analyzer)
{
    LONG ref = InterlockedDecrement(&analyzer->ref);
    if (!ref) {
        if (analyzer->cache_face) COM_RELEASE(analyzer->cache_face);
        dw_free(analyzer->cache_text);
        dw_free(analyzer->cache_glyphs);
        dw_free(analyzer);
    }
    return (ULONG)ref;
}

typedef HRESULT (STDMETHODCALLTYPE *GetTextAtPosition)(void *, UINT32, const WCHAR **, UINT32 *);

static HRESULT source_copy(void *source, UINT32 position, UINT32 length, WCHAR **out)
{
    *out = NULL;
    if (length > 0x7fffffff / sizeof(WCHAR) || length > 0xffffffffu - position) return E_INVALIDARG;
    WCHAR *copy = dw_alloc((SIZE_T)(length ? length : 1) * sizeof(WCHAR));
    if (!copy) return E_OUTOFMEMORY;
    UINT32 copied = 0;
    while (copied < length) {
        const WCHAR *part = NULL;
        UINT32 available = 0;
        HRESULT hr = ((GetTextAtPosition)VT(source)[3])(source, position + copied, &part, &available);
        if (FAILED(hr) || !part || !available) {
            dw_free(copy);
            return FAILED(hr) ? hr : E_INVALIDARG;
        }
        UINT32 n = available < length - copied ? available : length - copied;
        memcpy(copy + copied, part, (SIZE_T)n * sizeof(WCHAR));
        copied += n;
    }
    *out = copy;
    return S_OK;
}

static UINT32 scalar_at(const WCHAR *text, UINT32 length, UINT32 *position)
{
    UINT32 i = *position;
    UINT32 c = text[i++];
    if (c >= 0xd800 && c <= 0xdbff && i < length && text[i] >= 0xdc00 && text[i] <= 0xdfff)
        c = 0x10000 + ((c - 0xd800) << 10) + (text[i++] - 0xdc00);
    *position = i;
    return c;
}

typedef HRESULT (STDMETHODCALLTYPE *SetScriptAnalysis)(void *, UINT32, UINT32, const ScriptAnalysis *);
typedef HRESULT (STDMETHODCALLTYPE *SetLineBreakpoints)(void *, UINT32, UINT32, const LineBreakpoint *);
typedef HRESULT (STDMETHODCALLTYPE *SetBidiLevel)(void *, UINT32, UINT32, BYTE, BYTE);
typedef UINT32 (STDMETHODCALLTYPE *GetParagraphReadingDirection)(void *);

static HRESULT icu_scripts(const WCHAR *text, UINT32 length, INT32 *scripts)
{
    if (!icu_load()) return E_FAIL;
    for (UINT32 i = 0; i < length;) {
        UINT32 end = i, cp = scalar_at(text, length, &end);
        INT32 error = 0;
        INT32 script = g_icu.script((INT32)cp, &error);
        if (error > 0) return E_FAIL;
        for (; i < end; i++) scripts[i] = script;
    }
    for (UINT32 i = 0; i < length; i++) {
        if (scripts[i] != 0 && scripts[i] != 1) continue;
        INT32 script = 0;
        for (UINT32 j = i; j > 0; j--)
            if (scripts[j - 1] != 0 && scripts[j - 1] != 1) { script = scripts[j - 1]; break; }
        if (!script)
            for (UINT32 j = i + 1; j < length; j++)
                if (scripts[j] != 0 && scripts[j] != 1) { script = scripts[j]; break; }
        scripts[i] = script;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE analyzer_script(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    WCHAR *text = NULL;
    HRESULT hr = source_copy(source, position, length, &text);
    if (FAILED(hr) || !length) { dw_free(text); return hr; }
    if (length > (UINT32)-1 / sizeof(INT32)) { dw_free(text); return E_OUTOFMEMORY; }
    INT32 *scripts = dw_alloc((SIZE_T)length * sizeof(*scripts));
    if (!scripts) { dw_free(text); return E_OUTOFMEMORY; }
    hr = icu_scripts(text, length, scripts);
    for (UINT32 run = 0; SUCCEEDED(hr) && run < length;) {
        UINT32 end = run + 1;
        while (end < length && scripts[end] == scripts[run]) end++;
        ScriptAnalysis analysis = { (UINT16)scripts[run], 0 };
        hr = ((SetScriptAnalysis)VT(sink)[3])(sink, position + run, end - run, &analysis);
        run = end;
    }
    dw_free(scripts);
    dw_free(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE analyzer_linebreaks(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    WCHAR *text = NULL;
    HRESULT hr = source_copy(source, position, length, &text);
    if (FAILED(hr) || !length) { dw_free(text); return hr; }
    if (length > (UINT32)-1 / sizeof(LineBreakpoint)) { dw_free(text); return E_OUTOFMEMORY; }
    LineBreakpoint *breaks = dw_zalloc((SIZE_T)length * sizeof(*breaks));
    if (!breaks) { dw_free(text); return E_OUTOFMEMORY; }
    if (!icu_load()) { dw_free(breaks); dw_free(text); return E_FAIL; }
    INT32 error = 0;
    void *iterator = g_icu.break_open(2, NULL, (const UINT16 *)text, (INT32)length, &error);
    if (error > 0 || !iterator) { dw_free(breaks); dw_free(text); return E_FAIL; }
    for (UINT32 i = 0; i < length; i++) {
        breaks[i].break_before = 1;
        breaks[i].break_after = 1;
        breaks[i].whitespace = text[i] == ' ' || text[i] == '\t' || text[i] == 0x3000 ||
                               (text[i] >= 0x2000 && text[i] <= 0x200a);
        breaks[i].soft_hyphen = text[i] == 0x00ad;
    }
    g_icu.break_first(iterator);
    INT32 boundary;
    while ((boundary = g_icu.break_next(iterator)) != -1) {
        if (boundary > 0 && (UINT32)boundary <= length) {
            INT32 status = g_icu.break_status(iterator);
            BYTE condition = status >= 100 ? 2 : 0;
            breaks[boundary - 1].break_after = condition;
            if ((UINT32)boundary < length) breaks[boundary].break_before = condition;
        }
    }
    g_icu.break_close(iterator);
    hr = ((SetLineBreakpoints)VT(sink)[4])(sink, position, length, breaks);
    dw_free(breaks);
    dw_free(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE analyzer_bidi(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    WCHAR *text = NULL;
    HRESULT hr = source_copy(source, position, length, &text);
    if (FAILED(hr) || !length) { dw_free(text); return hr; }
    if (!icu_load()) { dw_free(text); return E_FAIL; }
    INT32 error = 0;
    void *bidi = g_icu.bidi_open((INT32)length, 0, &error);
    if (error > 0 || !bidi) { dw_free(text); return E_FAIL; }
    UINT8 requested = ((GetParagraphReadingDirection)VT(source)[5])(source) == 1 ? 1 : 0;
    g_icu.bidi_set_para(bidi, (const UINT16 *)text, (INT32)length, requested, NULL, &error);
    const UINT8 *levels = error <= 0 ? g_icu.bidi_levels(bidi, &error) : NULL;
    if (error > 0 || !levels) {
        g_icu.bidi_close(bidi);
        dw_free(text);
        return E_FAIL;
    }
    BYTE base = g_icu.bidi_para_level(bidi);
    for (UINT32 run = 0; SUCCEEDED(hr) && run < length;) {
        UINT32 end = run + 1;
        while (end < length && levels[end] == levels[run]) end++;
        hr = ((SetBidiLevel)VT(sink)[5])(sink, position + run, end - run, base, levels[run]);
        run = end;
    }
    g_icu.bidi_close(bidi);
    dw_free(text);
    return hr;
}

typedef HRESULT (STDMETHODCALLTYPE *GetNumberSubstitution)(void *, UINT32, UINT32 *, void **);
typedef HRESULT (STDMETHODCALLTYPE *SetNumberSubstitution)(void *, UINT32, UINT32, void *);

static HRESULT STDMETHODCALLTYPE analyzer_numbers(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    UINT32 remaining = length, current = position;
    while (remaining) {
        UINT32 span = 0;
        void *substitution = NULL;
        HRESULT hr = ((GetNumberSubstitution)VT(source)[7])(source, current, &span, &substitution);
        if (FAILED(hr)) return hr;
        if (!span) break;
        if (span > remaining) span = remaining;
        hr = ((SetNumberSubstitution)VT(sink)[6])(sink, current, span, substitution);
        if (substitution) COM_RELEASE(substitution);
        if (FAILED(hr)) return hr;
        current += span;
        remaining -= span;
    }
    return remaining ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE analyzer_glyphs(TextAnalyzer *analyzer, const WCHAR *text, UINT32 length, void *font_face,
                                                  BOOL sideways, BOOL rtl, const ScriptAnalysis *script,
                                                  const WCHAR *locale, void *number_substitution, const void *features,
                                                  const UINT32 *feature_lengths, UINT32 feature_ranges, UINT32 max_glyphs,
                                                  UINT16 *cluster_map, ShapingTextProperties *text_props,
                                                  UINT16 *glyph_indices, ShapingGlyphProperties *glyph_props,
                                                  UINT32 *actual_glyph_count)
{
    (void)sideways; (void)script; (void)locale; (void)number_substitution;
    (void)features; (void)feature_lengths; (void)feature_ranges;
    if (!actual_glyph_count || (length && (!text || !font_face || !cluster_map || !text_props ||
        !glyph_indices || !glyph_props))) return E_INVALIDARG;
    *actual_glyph_count = 0;
    if (!length) return S_OK;
    FontFace *face = font_face_from(font_face);
    if (!face || !face->face) return E_INVALIDARG;
    DW_SHAPED_GLYPH *shaped = NULL;
    UINT32 count = 0;
    HRESULT hr = dw_shape_text(face->face, text, length, 0, length, rtl, face->face->upem, &shaped, &count);
    if (FAILED(hr)) return hr;
    *actual_glyph_count = count;
    if (count > max_glyphs || count > 0xffff || length > 0xffff) {
        dw_free(shaped);
        return (HRESULT)0x8007007aL;
    }

    UINT16 *first_glyph = dw_alloc((SIZE_T)length * sizeof(UINT16));
    WCHAR *cached_text = dw_alloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!first_glyph || !cached_text) {
        dw_free(first_glyph); dw_free(cached_text); dw_free(shaped);
        return E_OUTOFMEMORY;
    }
    memcpy(cached_text, text, (SIZE_T)length * sizeof(WCHAR));
    cached_text[length] = 0;
    for (UINT32 i = 0; i < length; i++) first_glyph[i] = 0xffff;
    for (UINT32 i = 0; i < count; i++) {
        UINT32 c = shaped[i].cluster;
        if (c < length && first_glyph[c] == 0xffff) first_glyph[c] = (UINT16)i;
        glyph_indices[i] = shaped[i].glyph;
        glyph_props[i].bits = 0;
    }
    UINT32 mapped = 0;
    for (UINT32 i = 0; i < length; i++) {
        if (first_glyph[i] != 0xffff) mapped = first_glyph[i], glyph_props[mapped].bits |= 0x10;
        cluster_map[i] = (UINT16)mapped;
        text_props[i].bits = 0;
    }
    dw_free(first_glyph);

    COM_ADDREF(font_face);
    AcquireSRWLockExclusive(&analyzer->cache_lock);
    FontFace *old_face = analyzer->cache_face;
    WCHAR *old_text = analyzer->cache_text;
    DW_SHAPED_GLYPH *old_glyphs = analyzer->cache_glyphs;
    analyzer->cache_face = face;
    analyzer->cache_text = cached_text;
    analyzer->cache_length = length;
    analyzer->cache_rtl = rtl;
    analyzer->cache_glyphs = shaped;
    analyzer->cache_count = count;
    ReleaseSRWLockExclusive(&analyzer->cache_lock);
    if (old_face) COM_RELEASE(old_face);
    dw_free(old_text);
    dw_free(old_glyphs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE analyzer_placements(TextAnalyzer *analyzer, const WCHAR *text, const UINT16 *cluster_map,
                                                      const ShapingTextProperties *text_props, UINT32 length,
                                                      const UINT16 *glyph_indices, const ShapingGlyphProperties *glyph_props,
                                                      UINT32 glyph_count, void *font_face, FLOAT em_size, BOOL sideways,
                                                      BOOL rtl, const ScriptAnalysis *script, const WCHAR *locale,
                                                      const void *features, const UINT32 *feature_lengths,
                                                      UINT32 feature_ranges, FLOAT *advances, void *offsets)
{
    (void)cluster_map; (void)text_props; (void)glyph_props;
    (void)sideways; (void)script; (void)locale; (void)features; (void)feature_lengths; (void)feature_ranges;
    if ((length && (!text || !cluster_map || !text_props)) ||
        (glyph_count && (!text || !glyph_indices || !glyph_props || !font_face || !advances || !offsets)) ||
        em_size <= 0 || em_size > 1.0e7f || em_size != em_size) return E_INVALIDARG;
    if (!glyph_count) return S_OK;
    FontFace *face = font_face_from(font_face);
    if (!face || !face->face) return E_INVALIDARG;
    struct { FLOAT advanceOffset, ascenderOffset; } *glyph_offsets = offsets;
    AcquireSRWLockExclusive(&analyzer->cache_lock);
    BOOL cached = analyzer->cache_face == face && analyzer->cache_length == length && analyzer->cache_rtl == rtl &&
                  analyzer->cache_count == glyph_count && !memcmp(analyzer->cache_text, text, (SIZE_T)length * sizeof(WCHAR));
    for (UINT32 i = 0; cached && i < glyph_count; i++)
        cached = analyzer->cache_glyphs[i].glyph == glyph_indices[i];
    if (cached) {
        FLOAT scale = em_size / face->face->upem;
        for (UINT32 i = 0; i < glyph_count; i++) {
            advances[i] = analyzer->cache_glyphs[i].advance * scale;
            glyph_offsets[i].advanceOffset = analyzer->cache_glyphs[i].offset.advanceOffset * scale;
            glyph_offsets[i].ascenderOffset = analyzer->cache_glyphs[i].offset.ascenderOffset * scale;
        }
        ReleaseSRWLockExclusive(&analyzer->cache_lock);
        return S_OK;
    }
    ReleaseSRWLockExclusive(&analyzer->cache_lock);

    DW_SHAPED_GLYPH *shaped = NULL;
    UINT32 count = 0;
    HRESULT hr = dw_shape_text(face->face, text, length, 0, length, rtl, face->face->upem, &shaped, &count);
    if (FAILED(hr)) return hr;
    if (count != glyph_count) { dw_free(shaped); return E_INVALIDARG; }
    FLOAT scale = em_size / face->face->upem;
    for (UINT32 i = 0; i < glyph_count; i++) {
        if (shaped[i].glyph != glyph_indices[i]) { dw_free(shaped); return E_INVALIDARG; }
        advances[i] = shaped[i].advance * scale;
        glyph_offsets[i].advanceOffset = shaped[i].offset.advanceOffset * scale;
        glyph_offsets[i].ascenderOffset = shaped[i].offset.ascenderOffset * scale;
    }
    dw_free(shaped);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE analyzer_gdi_placements(TextAnalyzer *analyzer, const WCHAR *text, const UINT16 *cluster_map,
                                                          const ShapingTextProperties *text_props, UINT32 length,
                                                          const UINT16 *glyph_indices, const ShapingGlyphProperties *glyph_props,
                                                          UINT32 glyph_count, void *font_face, FLOAT em_size, FLOAT pixels_per_dip,
                                                          const DW_MATRIX *transform, BOOL use_gdi_natural, BOOL sideways,
                                                          BOOL rtl, const ScriptAnalysis *script, const WCHAR *locale,
                                                          const void *features, const UINT32 *feature_lengths,
                                                          UINT32 feature_ranges, FLOAT *advances, void *offsets)
{
    (void)pixels_per_dip; (void)transform; (void)use_gdi_natural;
    return analyzer_placements(analyzer, text, cluster_map, text_props, length, glyph_indices, glyph_props,
                               glyph_count, font_face, em_size, sideways, rtl, script, locale,
                               features, feature_lengths, feature_ranges, advances, offsets);
}

static const void *const analyzer_vtbl[] = {
    analyzer_qi, analyzer_addref, analyzer_release,
    analyzer_script, analyzer_bidi, analyzer_numbers, analyzer_linebreaks, analyzer_glyphs, analyzer_placements,
    analyzer_gdi_placements
};

HRESULT text_analyzer_create(void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    TextAnalyzer *analyzer = dw_zalloc(sizeof(*analyzer));
    if (!analyzer) return E_OUTOFMEMORY;
    analyzer->vtbl = analyzer_vtbl;
    analyzer->ref = 1;
    *out = analyzer;
    return S_OK;
}
