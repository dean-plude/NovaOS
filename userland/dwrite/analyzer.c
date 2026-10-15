/*
 * analyzer.c — DirectWrite text analysis and HarfBuzz-backed glyph shaping
 */
#include "dwrite_int.h"

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
} TextAnalyzer;

typedef struct {
    void *module;
    void *(*blob_create)(const char *, unsigned, int, void *, void *);
    void (*blob_destroy)(void *);
    void *(*face_create)(void *, unsigned);
    void (*face_destroy)(void *);
    void *(*font_create)(void *);
    void (*font_destroy)(void *);
    void (*font_set_scale)(void *, int, int);
    void *(*buffer_create)(void);
    void (*buffer_destroy)(void *);
    void (*buffer_add_utf16)(void *, const UINT16 *, int, unsigned, int);
    void (*buffer_set_direction)(void *, int);
    void (*buffer_guess_segment_properties)(void *);
    void (*shape)(void *, void *, const void *, unsigned);
    HbInfo *(*buffer_get_glyph_infos)(void *, unsigned *);
    HbPosition *(*buffer_get_glyph_positions)(void *, unsigned *);
} HarfBuzz;

static HarfBuzz g_hb;
static LONG g_hb_state;
static SRWLOCK g_hb_lock = SRWLOCK_INIT;

static BOOL hb_load(void)
{
    if (g_hb_state) return g_hb_state > 0;
    AcquireSRWLockExclusive(&g_hb_lock);
    if (!g_hb_state) {
        HMODULE module = LoadLibraryW(L"novatext.dll");
        static const char *const names[] = {
            "hb_blob_create", "hb_blob_destroy", "hb_face_create", "hb_face_destroy",
            "hb_font_create", "hb_font_destroy", "hb_font_set_scale", "hb_buffer_create",
            "hb_buffer_destroy", "hb_buffer_add_utf16", "hb_buffer_set_direction",
            "hb_buffer_guess_segment_properties", "hb_shape", "hb_buffer_get_glyph_infos",
            "hb_buffer_get_glyph_positions"
        };
        void **slots = (void **)&g_hb.blob_create;
        BOOL ok = module != NULL;
        for (UINT32 i = 0; ok && i < sizeof(names) / sizeof(names[0]); i++)
            ok = (slots[i] = (void *)GetProcAddress(module, names[i])) != NULL;
        if (ok) {
            g_hb.module = module;
            g_hb_state = 1;
        } else {
            if (module) FreeLibrary(module);
            g_hb_state = -1;
            dw_log("novatext.dll unavailable to DirectWrite text analyzer");
        }
    }
    ReleaseSRWLockExclusive(&g_hb_lock);
    return g_hb_state > 0;
}

typedef struct {
    void *buffer;
    HbInfo *infos;
    HbPosition *positions;
    UINT32 count;
} ShapedText;

static void shaped_text_destroy(ShapedText *shaped)
{
    if (shaped->buffer) g_hb.buffer_destroy(shaped->buffer);
    memset(shaped, 0, sizeof(*shaped));
}

static HRESULT shape_text(const WCHAR *text, UINT32 length, FontFace *face, BOOL rtl, ShapedText *shaped)
{
    memset(shaped, 0, sizeof(*shaped));
    if (!text || !face || !face->face || !face->face->file || !face->face->file->bytes ||
        length > 0x7fffffff / sizeof(WCHAR) || !face->face->upem)
        return E_INVALIDARG;
    if (!hb_load()) return E_FAIL;

    FaceData *fd = face->face;
    void *blob = g_hb.blob_create((const char *)fd->file->bytes, fd->file->size, 1, NULL, NULL);
    if (!blob) return E_OUTOFMEMORY;
    void *hb_face = g_hb.face_create(blob, fd->index);
    g_hb.blob_destroy(blob);
    if (!hb_face) return E_OUTOFMEMORY;
    void *font = g_hb.font_create(hb_face);
    if (!font) {
        g_hb.face_destroy(hb_face);
        return E_OUTOFMEMORY;
    }
    g_hb.font_set_scale(font, fd->upem, fd->upem);
    shaped->buffer = g_hb.buffer_create();
    if (!shaped->buffer) {
        g_hb.font_destroy(font);
        g_hb.face_destroy(hb_face);
        return E_OUTOFMEMORY;
    }

    g_hb.buffer_add_utf16(shaped->buffer, text, (int)length, 0, (int)length);
    g_hb.buffer_set_direction(shaped->buffer, rtl ? 5 : 4);
    g_hb.buffer_guess_segment_properties(shaped->buffer);
    g_hb.shape(shaped->buffer, font, NULL, 0);
    g_hb.font_destroy(font);
    g_hb.face_destroy(hb_face);

    unsigned count = 0, position_count = 0;
    shaped->infos = g_hb.buffer_get_glyph_infos(shaped->buffer, &count);
    shaped->positions = g_hb.buffer_get_glyph_positions(shaped->buffer, &position_count);
    if (count != position_count || (count && (!shaped->infos || !shaped->positions))) {
        shaped_text_destroy(shaped);
        return E_FAIL;
    }
    shaped->count = count;
    return S_OK;
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
    if (!ref) dw_free(analyzer);
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

static UINT16 script_of(UINT32 c)
{
    if ((c >= 0x0600 && c <= 0x08ff) || (c >= 0xfb50 && c <= 0xfdff) || (c >= 0xfe70 && c <= 0xfeff)) return 2;
    if ((c >= 0x0590 && c <= 0x05ff) || (c >= 0xfb1d && c <= 0xfb4f)) return 3;
    if (c >= 0x0900 && c <= 0x0dff) return 4;
    if ((c >= 0x0400 && c <= 0x052f) || (c >= 0x2de0 && c <= 0x2dff)) return 5;
    if ((c >= 0x0370 && c <= 0x03ff) || (c >= 0x1f00 && c <= 0x1fff)) return 6;
    if ((c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
        (c >= 0x20000 && c <= 0x3134f)) return 7;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0x00c0 && c <= 0x024f)) return 1;
    return 0;
}

typedef HRESULT (STDMETHODCALLTYPE *SetScriptAnalysis)(void *, UINT32, UINT32, const ScriptAnalysis *);
typedef HRESULT (STDMETHODCALLTYPE *SetLineBreakpoints)(void *, UINT32, UINT32, const LineBreakpoint *);
typedef HRESULT (STDMETHODCALLTYPE *SetBidiLevel)(void *, UINT32, UINT32, BYTE, BYTE);
typedef UINT32 (STDMETHODCALLTYPE *GetParagraphReadingDirection)(void *);

static HRESULT STDMETHODCALLTYPE analyzer_script(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    WCHAR *text = NULL;
    HRESULT hr = source_copy(source, position, length, &text);
    if (FAILED(hr) || !length) { dw_free(text); return hr; }

    UINT32 run = 0, i = 0;
    UINT16 script = script_of(scalar_at(text, length, &i));
    while (i < length) {
        UINT32 start = i;
        UINT16 next = script_of(scalar_at(text, length, &i));
        if (next != script && next) {
            ScriptAnalysis analysis = { script, 0 };
            hr = ((SetScriptAnalysis)VT(sink)[3])(sink, position + run, start - run, &analysis);
            if (FAILED(hr)) break;
            run = start;
            script = next;
        } else if (!script && next) {
            script = next;
        }
    }
    if (SUCCEEDED(hr) && run < length) {
        ScriptAnalysis analysis = { script, 0 };
        hr = ((SetScriptAnalysis)VT(sink)[3])(sink, position + run, length - run, &analysis);
    }
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
    LineBreakpoint *breaks = dw_zalloc((SIZE_T)length * sizeof(*breaks));
    if (!breaks) { dw_free(text); return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < length; i++) {
        UINT32 c = text[i];
        if (c == '\r' || c == '\n' || c == 0x2028 || c == 0x2029) {
            breaks[i].break_after = 3;
        } else if (c == ' ' || c == '\t' || c == 0x3000 || c == 0x00a0 ||
                   (c >= 0x2000 && c <= 0x200a)) {
            breaks[i].break_after = 1;
            breaks[i].whitespace = 1;
        } else if (c == '-' || c == 0x2010 || c == 0x2013) {
            breaks[i].break_after = 1;
        } else if (c == 0x00ad) {
            breaks[i].break_after = 1;
            breaks[i].soft_hyphen = 1;
        }
        if (i && (text[i - 1] == ' ' || text[i - 1] == '\t' || text[i - 1] == '-' ||
                  text[i - 1] == 0x2010 || text[i - 1] == 0x2013 || text[i - 1] == 0x00ad))
            breaks[i].break_before = 1;
    }
    hr = ((SetLineBreakpoints)VT(sink)[4])(sink, position, length, breaks);
    dw_free(breaks);
    dw_free(text);
    return hr;
}

static int rtl_scalar(UINT32 c)
{
    return (c >= 0x0590 && c <= 0x08ff) || (c >= 0xfb1d && c <= 0xfdff) ||
           (c >= 0xfe70 && c <= 0xfeff) || (c >= 0x10800 && c <= 0x10fff);
}

static int ltr_scalar(UINT32 c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= 0x00c0 && c <= 0x058f) || (c >= 0x0900 && c <= 0x10ffff && !rtl_scalar(c));
}

static HRESULT STDMETHODCALLTYPE analyzer_bidi(TextAnalyzer *analyzer, void *source, UINT32 position, UINT32 length, void *sink)
{
    (void)analyzer;
    if (!source || !sink) return E_INVALIDARG;
    WCHAR *text = NULL;
    HRESULT hr = source_copy(source, position, length, &text);
    if (FAILED(hr) || !length) { dw_free(text); return hr; }
    BYTE base = ((GetParagraphReadingDirection)VT(source)[5])(source) == 1;
    BYTE level = base;
    UINT32 run = 0, i = 0;
    while (i < length) {
        UINT32 start = i;
        UINT32 c = scalar_at(text, length, &i);
        BYTE next = rtl_scalar(c) ? 1 : ltr_scalar(c) ? (BYTE)(base ? 2 : 0) : level;
        if (next != level) {
            if (start > run) {
                hr = ((SetBidiLevel)VT(sink)[5])(sink, position + run, start - run, base, level);
                if (FAILED(hr)) break;
            }
            run = start;
            level = next;
        }
    }
    if (SUCCEEDED(hr) && run < length)
        hr = ((SetBidiLevel)VT(sink)[5])(sink, position + run, length - run, base, level);
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
    (void)analyzer; (void)sideways; (void)script; (void)locale; (void)number_substitution;
    (void)features; (void)feature_lengths; (void)feature_ranges;
    if (!actual_glyph_count || (length && (!text || !font_face || !cluster_map || !text_props ||
        !glyph_indices || !glyph_props))) return E_INVALIDARG;
    *actual_glyph_count = 0;
    if (!length) return S_OK;
    FontFace *face = font_face_from(font_face);
    if (!face) return E_INVALIDARG;
    ShapedText shaped;
    HRESULT hr = shape_text(text, length, face, rtl, &shaped);
    if (FAILED(hr)) return hr;
    *actual_glyph_count = shaped.count;
    if (shaped.count > max_glyphs || shaped.count > 0xffff) {
        shaped_text_destroy(&shaped);
        return (HRESULT)0x8007007aL;
    }

    UINT16 *first_glyph = dw_alloc((SIZE_T)length * sizeof(UINT16));
    if (!first_glyph) { shaped_text_destroy(&shaped); return E_OUTOFMEMORY; }
    for (UINT32 i = 0; i < length; i++) first_glyph[i] = 0xffff;
    for (UINT32 i = 0; i < shaped.count; i++) {
        UINT32 c = shaped.infos[i].cluster;
        if (c < length && first_glyph[c] > i) first_glyph[c] = (UINT16)i;
        glyph_indices[i] = (UINT16)shaped.infos[i].codepoint;
        glyph_props[i].bits = 0;
    }
    UINT32 mapped = 0;
    for (UINT32 i = 0; i < length; i++) {
        if (first_glyph[i] != 0xffff) {
            mapped = first_glyph[i];
            glyph_props[mapped].bits |= 0x10;
        }
        cluster_map[i] = (UINT16)mapped;
        text_props[i].bits = 0;
    }
    dw_free(first_glyph);
    shaped_text_destroy(&shaped);
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
    (void)analyzer; (void)cluster_map; (void)text_props; (void)glyph_indices; (void)glyph_props;
    (void)sideways; (void)script; (void)locale; (void)features; (void)feature_lengths; (void)feature_ranges;
    if ((length && (!text || !cluster_map || !text_props)) ||
        (glyph_count && (!glyph_indices || !glyph_props || !font_face || !advances || !offsets)) ||
        em_size <= 0 || em_size > 1.0e7f || em_size != em_size) return E_INVALIDARG;
    if (!glyph_count) return S_OK;
    FontFace *face = font_face_from(font_face);
    if (!face) return E_INVALIDARG;
    ShapedText shaped;
    HRESULT hr = shape_text(text, length, face, rtl, &shaped);
    if (FAILED(hr)) return hr;
    if (shaped.count != glyph_count) {
        shaped_text_destroy(&shaped);
        return E_INVALIDARG;
    }
    struct { FLOAT advanceOffset, ascenderOffset; } *glyph_offsets = offsets;
    FLOAT scale = em_size / face->face->upem;
    for (UINT32 i = 0; i < glyph_count; i++) {
        advances[i] = shaped.positions[i].x_advance * scale;
        glyph_offsets[i].advanceOffset = (rtl ? -shaped.positions[i].x_offset : shaped.positions[i].x_offset) * scale;
        glyph_offsets[i].ascenderOffset = shaped.positions[i].y_offset * scale;
    }
    shaped_text_destroy(&shaped);
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
