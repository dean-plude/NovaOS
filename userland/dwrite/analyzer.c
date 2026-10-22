/*
 * analyzer.c — DirectWrite text analysis and HarfBuzz-backed glyph shaping
 * (IDWriteTextAnalyzer2): script, bidi and line-break analysis from ICU,
 * glyphs and placements from HarfBuzz, and IDWriteTextAnalyzer1/2's script
 * properties, text complexity, justification, character spacing,
 * baselines, glyph orientation and OpenType feature lists.
 */
#include "dwrite_int.h"

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

DEFINE_GUID(IID_IDWriteTextAnalyzer, 0xb7e6163e, 0x7f46, 0x43b4, 0x84,0xb3, 0xe4,0xe6,0x24,0x9c,0x36,0x5d);
DEFINE_GUID(IID_IDWriteTextAnalyzer1, 0x80dad800, 0xe21f, 0x4e83, 0x96,0xce, 0xbf,0xcc,0xe5,0x00,0xdb,0x7c);
DEFINE_GUID(IID_IDWriteTextAnalyzer2, 0x553a9ff3, 0x5693, 0x4df7, 0xb5,0x2b, 0x74,0x80,0x6f,0x7f,0x2e,0xb9);

typedef struct { UINT16 script, shapes; } ScriptAnalysis;
enum { LINE_BREAK_NEUTRAL, LINE_BREAK_CAN, LINE_BREAK_MAY_NOT, LINE_BREAK_MUST };
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
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDWriteTextAnalyzer) ||
        IsEqualGUID(iid, &IID_IDWriteTextAnalyzer1) || IsEqualGUID(iid, &IID_IDWriteTextAnalyzer2)) {
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
        breaks[i].break_before = LINE_BREAK_MAY_NOT;
        breaks[i].break_after = LINE_BREAK_MAY_NOT;
        breaks[i].whitespace = text[i] == ' ' || text[i] == '\t' || text[i] == 0x3000 ||
                               (text[i] >= 0x2000 && text[i] <= 0x200a);
        breaks[i].soft_hyphen = text[i] == 0x00ad;
    }
    g_icu.break_first(iterator);
    INT32 boundary;
    while ((boundary = g_icu.break_next(iterator)) != -1) {
        if (boundary > 0 && (UINT32)boundary <= length) {
            INT32 status = g_icu.break_status(iterator);
            BYTE condition = status >= 100 ? LINE_BREAK_MUST : LINE_BREAK_CAN;
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

/* -----------------------------------------------------------------------
 * IDWriteTextAnalyzer1 and IDWriteTextAnalyzer2
 * ----------------------------------------------------------------------- */
/* DWRITE_SCRIPT_ANALYSIS as these methods take it, by value: the script (an ICU
 * UScriptCode here, as AnalyzeScript gives it) and DWRITE_SCRIPT_SHAPES */
typedef struct { UINT16 script; UINT32 shapes; } ScriptValue;

enum { SP_CARET = 1, SP_WORDS = 2, SP_DISCRETE = 4, SP_BLOCK = 8, SP_DISTRIBUTED = 16, SP_CONNECTED = 32, SP_CURSIVE = 64 };
static const struct { UINT16 icu; char iso[5]; UINT16 number; WCHAR justify; BYTE flags; } g_scripts[] = {
    { 0,  "Zyyy", 998, ' ',    SP_WORDS | SP_DISCRETE },
    { 1,  "Zinh", 994, ' ',    SP_WORDS | SP_DISCRETE },
    { 2,  "Arab", 160, 0x640,  SP_WORDS | SP_CONNECTED | SP_CURSIVE },
    { 3,  "Armn", 230, ' ',    SP_WORDS | SP_DISCRETE },
    { 4,  "Beng", 325, ' ',    SP_CARET | SP_WORDS | SP_CONNECTED },
    { 5,  "Bopo", 285, 0,      SP_DISCRETE | SP_BLOCK },
    { 8,  "Cyrl", 220, ' ',    SP_WORDS | SP_DISCRETE },
    { 10, "Deva", 315, ' ',    SP_CARET | SP_WORDS | SP_CONNECTED },
    { 11, "Ethi", 430, ' ',    SP_WORDS | SP_DISCRETE },
    { 12, "Geor", 240, ' ',    SP_WORDS | SP_DISCRETE },
    { 14, "Grek", 200, ' ',    SP_WORDS | SP_DISCRETE },
    { 15, "Gujr", 320, ' ',    SP_CARET | SP_WORDS },
    { 16, "Guru", 310, ' ',    SP_CARET | SP_WORDS | SP_CONNECTED },
    { 17, "Hani", 500, 0,      SP_DISCRETE | SP_BLOCK },
    { 18, "Hang", 286, ' ',    SP_WORDS | SP_DISCRETE | SP_BLOCK },
    { 19, "Hebr", 125, ' ',    SP_WORDS | SP_DISCRETE },
    { 20, "Hira", 410, 0,      SP_DISCRETE | SP_BLOCK },
    { 21, "Knda", 345, ' ',    SP_CARET | SP_WORDS },
    { 22, "Kana", 411, 0,      SP_DISCRETE | SP_BLOCK },
    { 23, "Khmr", 355, 0,      SP_CARET | SP_DISTRIBUTED },
    { 24, "Laoo", 356, 0,      SP_CARET | SP_DISTRIBUTED },
    { 25, "Latn", 215, ' ',    SP_WORDS | SP_DISCRETE },
    { 26, "Mlym", 347, ' ',    SP_CARET | SP_WORDS },
    { 27, "Mong", 145, ' ',    SP_WORDS | SP_CONNECTED | SP_CURSIVE },
    { 28, "Mymr", 350, 0,      SP_CARET | SP_DISTRIBUTED },
    { 31, "Orya", 327, ' ',    SP_CARET | SP_WORDS },
    { 33, "Sinh", 348, ' ',    SP_CARET | SP_WORDS },
    { 34, "Syrc", 135, ' ',    SP_WORDS | SP_CONNECTED | SP_CURSIVE },
    { 35, "Taml", 346, ' ',    SP_CARET | SP_WORDS },
    { 36, "Telu", 340, ' ',    SP_CARET | SP_WORDS },
    { 37, "Thaa", 170, ' ',    SP_WORDS },
    { 38, "Thai", 352, 0,      SP_CARET | SP_DISTRIBUTED },
    { 39, "Tibt", 330, 0x0F0B, SP_CARET | SP_CONNECTED },
};

static HRESULT STDMETHODCALLTYPE analyzer_script_props(TextAnalyzer *a, ScriptValue sa, UINT32 *props)
{
    (void)a;
    if (!props) return E_INVALIDARG;
    UINT32 i = 0, n = sizeof(g_scripts) / sizeof(g_scripts[0]);
    while (i < n && g_scripts[i].icu != sa.script) i++;
    if (i == n) i = 0;            /* scripts not in the table: as the common one */
    const char *iso = g_scripts[i].iso;
    BYTE f = g_scripts[i].flags;
    /* DWRITE_SCRIPT_PROPERTIES: isoScriptCode, isoScriptNumber, clusterLookahead, justificationCharacter, flag bits */
    props[0] = (UINT32)(BYTE)iso[0] | (UINT32)(BYTE)iso[1] << 8 | (UINT32)(BYTE)iso[2] << 16 | (UINT32)(BYTE)iso[3] << 24;
    props[1] = g_scripts[i].number;
    props[2] = (f & SP_DISTRIBUTED) ? 8 : 0;
    props[3] = g_scripts[i].justify;
    props[4] = f;                 /* the SP_ bits are the bit fields, in order */
    return S_OK;
}

/* characters drawn one glyph each with no shaping: no combining marks, joiners,
 * bidi controls, surrogates or right-to-left and complex scripts */
static BOOL simple_char(WCHAR c)
{
    if (c < 0x300) return TRUE;
    if (c >= 0x370 && c < 0x483) return TRUE;                 /* Greek, Cyrillic */
    if (c >= 0x48A && c < 0x530) return TRUE;
    if (c >= 0x1E00 && c < 0x2000) return TRUE;               /* Latin extended additional, Greek extended */
    if (c >= 0x2010 && c < 0x2028) return TRUE;               /* punctuation */
    if (c >= 0x2030 && c < 0x2060) return TRUE;
    if (c >= 0x2070 && c < 0x20D0) return TRUE;               /* super/subscripts, currency */
    if (c >= 0x2100 && c < 0x2C00) return TRUE;               /* symbols, arrows, maths, boxes */
    return FALSE;
}
static HRESULT STDMETHODCALLTYPE analyzer_complexity(TextAnalyzer *a, const WCHAR *text, UINT32 len, void *face,
                                                     BOOL *is_simple, UINT32 *len_read, UINT16 *indices)
{
    (void)a;
    FontFace *f = font_face_from(face);
    if (!text || !is_simple || !len_read || !f) return E_INVALIDARG;
    *is_simple = FALSE;
    *len_read = 0;
    if (!len) return S_OK;
    BOOL first = simple_char(text[0]);
    UINT32 n = 1;
    while (n < len && simple_char(text[n]) == first) n++;
    *is_simple = first;
    *len_read = n;
    if (indices) for (UINT32 i = 0; i < n; i++) indices[i] = first ? face_glyph_index(f->face, text[i]) : 0;
    return S_OK;
}

/* DWRITE_JUSTIFICATION_OPPORTUNITY: three floats and the priority and edge bits */
typedef struct { float exp_min, exp_max, comp_max; UINT32 bits; } Justification;
#define J_EXPANSION_PRIORITY(p) ((UINT32)(p))
#define J_APPLY_TRAILING (1u << 19)

static BOOL justify_space(WCHAR c) { return c == ' ' || c == 0xA0 || c == 0x3000 || c == 0x1680; }

/* spaces stretch; nothing else does or shrinks */
static HRESULT STDMETHODCALLTYPE analyzer_justification(TextAnalyzer *a, void *face, float em, ScriptValue sa, UINT32 len,
                                                        UINT32 gcount, const WCHAR *text, const UINT16 *cmap,
                                                        const void *props, Justification *jo)
{
    (void)a; (void)face; (void)sa; (void)props;
    if (!jo || (len && (!text || !cmap))) return E_INVALIDARG;
    memset(jo, 0, gcount * sizeof(*jo));
    for (UINT32 i = 0; i < len; i++) {
        if (!justify_space(text[i]) || cmap[i] >= gcount) continue;
        Justification *j = &jo[cmap[i]];
        j->exp_max = em > 0 ? em * 4 : 1e6f;
        j->bits = J_EXPANSION_PRIORITY(1) | J_APPLY_TRAILING;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE analyzer_justify(TextAnalyzer *a, float width, UINT32 gcount, const Justification *jo,
                                                  const float *adv, const DW_GLYPH_OFFSET *off, float *jadv,
                                                  DW_GLYPH_OFFSET *joff)
{
    (void)a;
    if (gcount && (!jo || !adv || !off || !jadv || !joff)) return E_INVALIDARG;
    float total = 0;
    UINT32 spaces = 0;
    for (UINT32 i = 0; i < gcount; i++) {
        total += adv[i];
        if (jo[i].bits & 0xFF) spaces++;
        jadv[i] = adv[i];
        joff[i] = off[i];
    }
    if (width > total && spaces) {
        float add = (width - total) / spaces;
        for (UINT32 i = 0; i < gcount; i++) {
            if (!(jo[i].bits & 0xFF)) continue;
            float d = add < jo[i].exp_max ? add : jo[i].exp_max;
            jadv[i] += d;
        }
    }
    return S_OK;
}
/* the justified glyphs are the same glyphs (no kashidas are inserted) */
static HRESULT STDMETHODCALLTYPE analyzer_justified_glyphs(TextAnalyzer *a, void *face, float em, ScriptValue sa, UINT32 len,
                                                           UINT32 gcount, UINT32 max, const UINT16 *cmap,
                                                           const UINT16 *indices, const float *adv, const float *jadv,
                                                           const DW_GLYPH_OFFSET *joff, const void *props, UINT32 *actual,
                                                           UINT16 *mcmap, UINT16 *mindices, float *madv,
                                                           DW_GLYPH_OFFSET *moff)
{
    (void)a; (void)face; (void)em; (void)sa; (void)adv; (void)props;
    if (!actual) return E_INVALIDARG;
    *actual = gcount;
    if (max < gcount) return E_NOT_SUFFICIENT_BUFFER_;
    if ((len && (!cmap || !mcmap)) || (gcount && (!indices || !jadv || !joff || !mindices || !madv || !moff)))
        return E_INVALIDARG;
    if (len) memcpy(mcmap, cmap, len * sizeof(UINT16));
    if (gcount) {
        memcpy(mindices, indices, gcount * sizeof(UINT16));
        memcpy(madv, jadv, gcount * sizeof(float));
        memcpy(moff, joff, gcount * sizeof(DW_GLYPH_OFFSET));
    }
    return S_OK;
}

/* leading space before each cluster, trailing space after it, and a minimum cluster advance */
static HRESULT STDMETHODCALLTYPE analyzer_char_spacing(TextAnalyzer *a, float leading, float trailing, float min_adv,
                                                       UINT32 len, UINT32 gcount, const UINT16 *cmap, const float *adv,
                                                       const DW_GLYPH_OFFSET *off, const void *props, float *madv,
                                                       DW_GLYPH_OFFSET *moff)
{
    (void)a; (void)props;
    if (min_adv < 0 || (len && !cmap) || (gcount && (!adv || !off || !madv || !moff))) return E_INVALIDARG;
    if (madv != adv) memmove(madv, adv, gcount * sizeof(float));
    if (moff != off) memmove(moff, off, gcount * sizeof(DW_GLYPH_OFFSET));
    for (UINT32 i = 0; i < len;) {
        UINT32 g0 = cmap[i], j = i + 1;
        while (j < len && cmap[j] == g0) j++;
        UINT32 g1 = j < len ? cmap[j] : gcount;
        i = j;
        if (g0 >= g1 || g1 > gcount) continue;
        float w = 0;
        for (UINT32 g = g0; g < g1; g++) w += madv[g];
        madv[g0] += leading;
        for (UINT32 g = g0; g < g1; g++) moff[g].advanceOffset += leading;
        madv[g1 - 1] += trailing;
        w += leading + trailing;
        if (w < min_adv) madv[g1 - 1] += min_adv - w;
    }
    return S_OK;
}

/* baselines from the font's metrics (no BASE table is read): the roman
 * baseline is the origin, the others are placed as DirectWrite simulates them */
enum { BL_DEFAULT, BL_ROMAN, BL_CENTRAL, BL_MATH, BL_HANGING, BL_IDEO_BOTTOM, BL_IDEO_TOP, BL_MINIMUM, BL_MAXIMUM };
static HRESULT STDMETHODCALLTYPE analyzer_baseline(TextAnalyzer *a, void *face, UINT32 baseline, BOOL vertical,
                                                   BOOL simulation_allowed, ScriptValue sa, const WCHAR *locale,
                                                   INT32 *coord, BOOL *exists)
{
    (void)a; (void)sa; (void)locale;
    FontFace *f = font_face_from(face);
    if (!coord || !exists) return E_INVALIDARG;
    *coord = 0;
    *exists = FALSE;
    if (!f || baseline > BL_MAXIMUM) return E_INVALIDARG;
    const DW_FONT_METRICS *m = &f->face->metrics.m;
    INT32 asc = m->ascent, desc = m->descent;
    if (baseline == BL_DEFAULT || baseline == BL_ROMAN) {
        *coord = vertical ? (asc - desc) / 2 : 0;
        *exists = !vertical;
        return S_OK;
    }
    if (!simulation_allowed) return S_OK;
    switch (baseline) {
    case BL_CENTRAL:     *coord = (asc - desc) / 2; break;
    case BL_MATH:        *coord = m->xHeight ? m->xHeight / 2 : asc / 4; break;
    case BL_HANGING:     *coord = asc * 4 / 5; break;
    case BL_IDEO_BOTTOM:
    case BL_MINIMUM:     *coord = -desc; break;
    case BL_IDEO_TOP:
    case BL_MAXIMUM:     *coord = asc; break;
    }
    return S_OK;
}

/* every glyph stands upright (angle 0) at the paragraph's bidi level */
typedef DWORD (STDMETHODCALLTYPE *ReadingDirection)(void *);
typedef HRESULT (STDMETHODCALLTYPE *SetGlyphOrientation)(void *, UINT32, UINT32, UINT32, BYTE, BOOL, BOOL);
static HRESULT STDMETHODCALLTYPE analyzer_vertical(TextAnalyzer *a, void *source, UINT32 pos, UINT32 len, void *sink)
{
    (void)a;
    if (!source || !sink) return E_INVALIDARG;
    if (!len) return S_OK;
    DWORD dir = ((ReadingDirection)VT(source)[5])(source);
    BOOL rtl = dir == 1;          /* DWRITE_READING_DIRECTION_RIGHT_TO_LEFT */
    return ((SetGlyphOrientation)VT(sink)[7])(sink, pos, len, 0, (BYTE)(rtl ? 1 : 0), FALSE, rtl);
}

/* rotations by 0, 90, 180 and 270 degrees; a sideways glyph turns a further 90 */
static HRESULT orientation(UINT32 angle, BOOL sideways, float ox, float oy, DW_MATRIX *m)
{
    static const DW_MATRIX rot[4] = {
        { 1, 0, 0, 1, 0, 0 }, { 0, 1, -1, 0, 0, 0 }, { -1, 0, 0, -1, 0, 0 }, { 0, -1, 1, 0, 0, 0 } };
    if (!m) return E_INVALIDARG;
    if (angle > 3) { memset(m, 0, sizeof(*m)); return E_INVALIDARG; }
    if (sideways) angle = (angle + 1) & 3;
    *m = rot[angle];
    if (angle) {                  /* about the origin given */
        m->dx = ox - ox * m->m11 - oy * m->m21;
        m->dy = oy - ox * m->m12 - oy * m->m22;
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE analyzer_orientation(TextAnalyzer *a, UINT32 angle, BOOL sideways, DW_MATRIX *m)
{
    (void)a;
    return orientation(angle, sideways, 0, 0, m);
}
static HRESULT STDMETHODCALLTYPE analyzer_orientation2(TextAnalyzer *a, UINT32 angle, BOOL sideways, float ox, float oy,
                                                       DW_MATRIX *m)
{
    (void)a;
    return orientation(angle, sideways, ox, oy, m);
}

/* ---- OpenType feature lists (GSUB and GPOS) ---- */
static UINT16 rd16(const BYTE *p) { return (UINT16)(p[0] << 8 | p[1]); }
static UINT32 rd32(const BYTE *p) { return (UINT32)p[0] << 24 | (UINT32)p[1] << 16 | (UINT32)p[2] << 8 | p[3]; }
static UINT32 tag_of(const BYTE *p) { return (UINT32)p[0] | (UINT32)p[1] << 8 | (UINT32)p[2] << 16 | (UINT32)p[3] << 24; }

/* the table's FeatureList and its feature count, or NULL */
static const BYTE *feature_list(const BYTE *t, UINT32 size, UINT32 *count)
{
    *count = 0;
    if (!t || size < 10) return NULL;
    UINT32 off = rd16(t + 6);
    if (off + 2 > size) return NULL;
    UINT32 n = rd16(t + off);
    if (off + 2 + n * 6 > size) return NULL;
    *count = n;
    return t + off;
}
static HRESULT STDMETHODCALLTYPE analyzer_features(TextAnalyzer *a, void *face, ScriptValue sa, const WCHAR *locale,
                                                   UINT32 max, UINT32 *actual, UINT32 *tags)
{
    (void)a; (void)sa; (void)locale;
    FontFace *f = font_face_from(face);
    if (!f || !actual || (max && !tags)) return E_INVALIDARG;
    static const char *const names[2] = { "GSUB", "GPOS" };
    UINT32 n = 0;
    for (int t = 0; t < 2; t++) {
        UINT32 size = 0, count = 0;
        const BYTE *table = sfnt_table(f->face, names[t], &size);
        const BYTE *fl = feature_list(table, size, &count);
        for (UINT32 i = 0; fl && i < count; i++) {
            UINT32 tag = tag_of(fl + 2 + i * 6), k = 0;
            while (k < n && k < max && tags[k] != tag) k++;
            if (k < n && k < max) continue;
            if (n < max) tags[n] = tag;
            n++;
        }
    }
    *actual = n;
    return n > max ? E_NOT_SUFFICIENT_BUFFER_ : S_OK;
}

/* does the coverage table at c list glyph g */
static BOOL coverage_has(const BYTE *t, UINT32 size, UINT32 c, UINT16 g)
{
    if (c + 4 > size) return FALSE;
    UINT32 fmt = rd16(t + c), n = rd16(t + c + 2);
    if (fmt == 1) {
        if (c + 4 + n * 2 > size) return FALSE;
        for (UINT32 i = 0; i < n; i++) if (rd16(t + c + 4 + i * 2) == g) return TRUE;
    } else if (fmt == 2) {
        if (c + 4 + n * 6 > size) return FALSE;
        for (UINT32 i = 0; i < n; i++) {
            const BYTE *r = t + c + 4 + i * 6;
            if (g >= rd16(r) && g <= rd16(r + 2)) return TRUE;
        }
    }
    return FALSE;
}
/* would a GSUB feature's lookups touch each glyph: the glyph is in the
 * coverage of one of its substitution subtables (contextual ones by their
 * first coverage) */
static HRESULT STDMETHODCALLTYPE analyzer_check_feature(TextAnalyzer *a, void *face, ScriptValue sa, const WCHAR *locale,
                                                        UINT32 feature, UINT32 gcount, const UINT16 *indices,
                                                        BYTE *applies)
{
    (void)a; (void)sa; (void)locale;
    FontFace *f = font_face_from(face);
    if (!f || (gcount && (!indices || !applies))) return E_INVALIDARG;
    memset(applies, 0, gcount);
    UINT32 size = 0, count = 0;
    const BYTE *t = sfnt_table(f->face, "GSUB", &size);
    const BYTE *fl = feature_list(t, size, &count);
    if (!fl) return S_OK;
    UINT32 ll = rd16(t + 8);
    if (ll + 2 > size) return S_OK;
    UINT32 nlookups = rd16(t + ll);
    for (UINT32 i = 0; i < count; i++) {
        if (tag_of(fl + 2 + i * 6) != feature) continue;
        UINT32 ft = (UINT32)(fl - t) + rd16(fl + 2 + i * 6 + 4);
        if (ft + 4 > size) continue;
        UINT32 nidx = rd16(t + ft + 2);
        for (UINT32 k = 0; k < nidx && ft + 6 + k * 2 <= size; k++) {
            UINT32 li = rd16(t + ft + 4 + k * 2);
            if (li >= nlookups || ll + 4 + li * 2 > size) continue;
            UINT32 lk = ll + rd16(t + ll + 2 + li * 2);
            if (lk + 6 > size) continue;
            UINT32 type = rd16(t + lk), nsub = rd16(t + lk + 4);
            for (UINT32 s = 0; s < nsub && lk + 8 + s * 2 <= size; s++) {
                UINT32 st = lk + rd16(t + lk + 6 + s * 2), ty = type;
                if (ty == 7 && st + 8 <= size) { ty = rd16(t + st + 2); st += rd32(t + st + 4); }   /* extension */
                if (st + 4 > size) continue;
                if ((ty == 5 || ty == 6) && rd16(t + st) == 3) continue;     /* coverage-based contexts: not read */
                UINT32 cov = st + rd16(t + st + 2);
                for (UINT32 g = 0; g < gcount; g++)
                    if (!applies[g] && coverage_has(t, size, cov, indices[g])) applies[g] = 1;
            }
        }
    }
    return S_OK;
}

static const void *const analyzer_vtbl[] = {
    analyzer_qi, analyzer_addref, analyzer_release,
    analyzer_script, analyzer_bidi, analyzer_numbers, analyzer_linebreaks, analyzer_glyphs, analyzer_placements,
    analyzer_gdi_placements,
    analyzer_char_spacing, analyzer_baseline, analyzer_vertical, analyzer_orientation, analyzer_script_props,
    analyzer_complexity, analyzer_justification, analyzer_justify, analyzer_justified_glyphs,
    analyzer_orientation2, analyzer_features, analyzer_check_feature,
};
_Static_assert(sizeof(analyzer_vtbl) == 22 * sizeof(void *), "IDWriteTextAnalyzer2 has 22 methods");

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
