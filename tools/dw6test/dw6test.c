/* dw6test — DirectWrite's Windows 10 font model as XAML (Windows.UI.Xaml's
 * text core) uses it, through NovaOS's dwrite.dll: IDWriteFactory4 to
 * IDWriteFactory7, the system font collection in the typographic family
 * model (IDWriteFontCollection2/3, IDWriteFontFamily2, IDWriteFontList2),
 * matching by axis values, IDWriteFontSet1/2 filtering and axis ranges,
 * IDWriteFontFace4/5/6, IDWriteFontResource, IDWriteFontSetBuilder2,
 * collections from sets, the in-memory font file loader, glyph origins,
 * IDWriteTextFormat1-3 and IDWriteTextLayout1-4 (axis values, optical
 * alignment, line spacing, metrics), IDWriteFontFallback1 and
 * IDWriteTextAnalyzer1/2.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dw6test.exe dw6test.c -ldwrite
 * Run:   dw6test
 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <dwrite_3.h>
#include <stdio.h>
#include <wchar.h>

static int pass, fail;
#define CHECK(what, cond) do { if (cond) { pass++; printf("ok   %s\n", what); } \
                               else { fail++; printf("FAIL %s (line %d)\n", what, __LINE__); } } while (0)

/* a method by its vtable name (COBJMACROS' macros of the same names are function-like) */
#define V(obj, m) ((obj)->lpVtbl->m)

#define TAG(a, b, c, d) ((DWRITE_FONT_AXIS_TAG)((a) | (b) << 8 | (c) << 16 | (d) << 24))

/* ---- an IDWriteTextAnalysisSource over a string ---- */
typedef struct { IDWriteTextAnalysisSource iface; const WCHAR *text; UINT32 len; } Source;
static HRESULT STDMETHODCALLTYPE src_qi(IDWriteTextAnalysisSource *s, REFIID iid, void **out) { (void)iid; *out = s; return S_OK; }
static ULONG STDMETHODCALLTYPE src_addref(IDWriteTextAnalysisSource *s) { (void)s; return 1; }
static ULONG STDMETHODCALLTYPE src_release(IDWriteTextAnalysisSource *s) { (void)s; return 1; }
static HRESULT STDMETHODCALLTYPE src_text(IDWriteTextAnalysisSource *s, UINT32 pos, const WCHAR **text, UINT32 *len)
{
    Source *src = (Source *)s;
    if (pos >= src->len) { *text = NULL; *len = 0; return S_OK; }
    *text = src->text + pos;
    *len = src->len - pos;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE src_before(IDWriteTextAnalysisSource *s, UINT32 pos, const WCHAR **text, UINT32 *len)
{
    Source *src = (Source *)s;
    *text = pos && pos <= src->len ? src->text : NULL;
    *len = pos <= src->len ? pos : 0;
    return S_OK;
}
static DWRITE_READING_DIRECTION STDMETHODCALLTYPE src_dir(IDWriteTextAnalysisSource *s) { (void)s; return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
static HRESULT STDMETHODCALLTYPE src_locale(IDWriteTextAnalysisSource *s, UINT32 pos, UINT32 *len, const WCHAR **locale)
{
    Source *src = (Source *)s;
    *len = pos < src->len ? src->len - pos : 0;
    *locale = L"en-us";
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE src_subst(IDWriteTextAnalysisSource *s, UINT32 pos, UINT32 *len, IDWriteNumberSubstitution **n)
{
    Source *src = (Source *)s;
    *len = pos < src->len ? src->len - pos : 0;
    *n = NULL;
    return S_OK;
}
static IDWriteTextAnalysisSourceVtbl src_vtbl = { src_qi, src_addref, src_release, src_text, src_before, src_dir, src_locale, src_subst };

/* the path of a face reference's file */
static void path_of(IDWriteFontFaceReference *r, WCHAR *out, UINT32 cap)
{
    IDWriteFontFile *file = NULL;
    IDWriteFontFileLoader *loader = NULL;
    IDWriteLocalFontFileLoader *local = NULL;
    const void *key = NULL;
    UINT32 size = 0;
    out[0] = 0;
    if (FAILED(V(r, GetFontFile)(r, &file))) return;
    if (SUCCEEDED(IDWriteFontFile_GetLoader(file, &loader)) &&
        SUCCEEDED(IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void **)&local)) &&
        SUCCEEDED(IDWriteFontFile_GetReferenceKey(file, &key, &size)))
        IDWriteLocalFontFileLoader_GetFilePathFromKey(local, key, size, out, cap);
    if (local) IDWriteLocalFontFileLoader_Release(local);
    if (loader) IDWriteFontFileLoader_Release(loader);
    IDWriteFontFile_Release(file);
}

int main(void)
{
    IDWriteFactory6 *f6 = NULL;
    IDWriteFactory7 *f7 = NULL;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory6, (IUnknown **)&f6);
    CHECK("DWriteCreateFactory gives IDWriteFactory6", SUCCEEDED(hr) && f6);
    if (!f6) { printf("dw6test: %d passed, %d failed\n", pass, fail + 1); return 1; }
    CHECK("the factory is an IDWriteFactory7", SUCCEEDED(IDWriteFactory6_QueryInterface(f6, &IID_IDWriteFactory7, (void **)&f7)) && f7);

    /* ---- the system collection, typographic model (as XAML asks for it) ---- */
    IDWriteFontCollection2 *coll = NULL;
    hr = IDWriteFactory6_GetSystemFontCollection(f6, TRUE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &coll);
    CHECK("GetSystemFontCollection(typographic)", SUCCEEDED(hr) && coll);
    CHECK("its family model is typographic", coll && IDWriteFontCollection2_GetFontFamilyModel(coll) == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC);
    IDWriteFontCollection2 *wws = NULL;
    hr = IDWriteFactory6_GetSystemFontCollection(f6, FALSE, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, &wws);
    CHECK("GetSystemFontCollection(weight/stretch/style)", SUCCEEDED(hr) && wws &&
          IDWriteFontCollection2_GetFontFamilyModel(wws) == DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE);
    CHECK("an unknown family model is refused",
          IDWriteFactory6_GetSystemFontCollection(f6, FALSE, (DWRITE_FONT_FAMILY_MODEL)7, (IDWriteFontCollection2 **)&f7) == E_INVALIDARG);
    f7 = NULL;
    IDWriteFontCollection3 *coll3 = NULL;
    CHECK("the collection is an IDWriteFontCollection3",
          coll && SUCCEEDED(IDWriteFontCollection2_QueryInterface(coll, &IID_IDWriteFontCollection3, (void **)&coll3)) && coll3 &&
          IDWriteFontCollection3_GetExpirationEvent(coll3) != NULL);
    if (coll3) IDWriteFontCollection3_Release(coll3);

    UINT32 index = 0;
    BOOL exists = FALSE;
    CHECK("FindFamilyName(Segoe UI)", coll && SUCCEEDED(IDWriteFontCollection2_FindFamilyName(coll, L"Segoe UI", &index, &exists)) && exists);
    IDWriteFontFamily2 *fam = NULL;
    CHECK("GetFontFamily gives an IDWriteFontFamily2",
          coll && exists && SUCCEEDED(V(coll, IDWriteFontCollection2_GetFontFamily)(coll, index, &fam)) && fam);

    /* bold, by axis values, as XAML's DWriteFontFamily::LookupNominalFontFace asks */
    DWRITE_FONT_AXIS_VALUE bold[] = {
        { DWRITE_FONT_AXIS_TAG_WEIGHT, 700 }, { DWRITE_FONT_AXIS_TAG_ITALIC, 0 },
        { DWRITE_FONT_AXIS_TAG_SLANT, 0 }, { DWRITE_FONT_AXIS_TAG_WIDTH, 100 } };
    IDWriteFontList2 *list = NULL;
    IDWriteFont *font = NULL;
    hr = fam ? V(fam, IDWriteFontFamily2_GetMatchingFonts)(fam, bold, 4, &list) : E_FAIL;
    CHECK("IDWriteFontFamily2::GetMatchingFonts(wght 700)", SUCCEEDED(hr) && list && IDWriteFontList2_GetFontCount(list) > 0);
    if (list) V(list, IDWriteFontList1_GetFont)(list, 0, (IDWriteFont3 **)&font);
    CHECK("its first font is bold", font && IDWriteFont_GetWeight(font) >= DWRITE_FONT_WEIGHT_BOLD);
    IDWriteFontSet1 *famset = NULL;
    CHECK("IDWriteFontFamily2::GetFontSet", fam && SUCCEEDED(IDWriteFontFamily2_GetFontSet(fam, &famset)) && famset &&
          IDWriteFontSet1_GetFontCount(famset) == IDWriteFontFamily2_GetFontCount(fam));
    IDWriteFontSet1 *listset = NULL;
    CHECK("IDWriteFontList2::GetFontSet", list && SUCCEEDED(IDWriteFontList2_GetFontSet(list, &listset)) && listset);
    if (listset) IDWriteFontSet1_Release(listset);
    IDWriteFontList2 *clist = NULL;
    CHECK("IDWriteFontCollection2::GetMatchingFonts",
          coll && SUCCEEDED(IDWriteFontCollection2_GetMatchingFonts(coll, L"Segoe UI", bold, 4, &clist)) && clist &&
          IDWriteFontList2_GetFontCount(clist) > 0);
    if (clist) IDWriteFontList2_Release(clist);

    /* ---- faces: IDWriteFontFace4/5/6, the font resource ---- */
    IDWriteFontFace *face = NULL;
    IDWriteFontFace5 *face5 = NULL;
    if (font) IDWriteFont_CreateFontFace(font, &face);
    CHECK("the face is an IDWriteFontFace5", face && SUCCEEDED(IDWriteFontFace_QueryInterface(face, &IID_IDWriteFontFace5, (void **)&face5)) && face5);
    if (face5) {
        DWRITE_GLYPH_IMAGE_FORMATS formats = IDWriteFontFace5_GetGlyphImageFormats(face5);
        CHECK("GetGlyphImageFormats: outlines", formats & (DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE | DWRITE_GLYPH_IMAGE_FORMATS_CFF));
        CHECK("no variations", !IDWriteFontFace5_HasVariations(face5) && IDWriteFontFace5_GetFontAxisValueCount(face5) == 0);
        CHECK("Equals itself", IDWriteFontFace5_Equals(face5, face));
        IDWriteFontResource *res = NULL;
        CHECK("GetFontResource", SUCCEEDED(IDWriteFontFace5_GetFontResource(face5, &res)) && res &&
              IDWriteFontResource_GetFontFaceIndex(res) == IDWriteFontFace5_GetIndex(face5));
        IDWriteFontFace5 *again = NULL;
        CHECK("the resource makes the same face",
              res && SUCCEEDED(IDWriteFontResource_CreateFontFace(res, DWRITE_FONT_SIMULATIONS_NONE, NULL, 0, &again)) && again &&
              IDWriteFontFace5_Equals(again, (IDWriteFontFace *)face5) == (IDWriteFontFace5_GetSimulations(face5) == DWRITE_FONT_SIMULATIONS_NONE));
        if (again) IDWriteFontFace5_Release(again);
        if (res) IDWriteFontResource_Release(res);
        IDWriteFontFace6 *face6 = NULL;
        IDWriteLocalizedStrings *names = NULL;
        CHECK("IDWriteFontFace6::GetFamilyNames(typographic)",
              SUCCEEDED(IDWriteFontFace5_QueryInterface(face5, &IID_IDWriteFontFace6, (void **)&face6)) && face6 &&
              SUCCEEDED(V(face6, IDWriteFontFace6_GetFamilyNames)(face6, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &names)) && names &&
              IDWriteLocalizedStrings_GetCount(names) > 0);
        if (names) IDWriteLocalizedStrings_Release(names);
        if (face6) IDWriteFontFace6_Release(face6);
    }

    /* ---- font sets: IDWriteFontSet1 filtering and axis ranges ---- */
    IDWriteFontSet1 *sys = NULL;
    CHECK("IDWriteFactory6::GetSystemFontSet", SUCCEEDED(V(f6, IDWriteFactory6_GetSystemFontSet)(f6, FALSE, &sys)) && sys &&
          IDWriteFontSet1_GetFontCount(sys) > 0);
    if (sys) {
        DWRITE_FONT_AXIS_RANGE heavy = { DWRITE_FONT_AXIS_TAG_WEIGHT, 600, 1000 };
        IDWriteFontSet1 *bolds = NULL;
        CHECK("GetFilteredFonts by a weight range",
              SUCCEEDED(V(sys, GetFilteredFonts_)(sys, &heavy, 1, FALSE, &bolds)) && bolds &&
              IDWriteFontSet1_GetFontCount(bolds) > 0 && IDWriteFontSet1_GetFontCount(bolds) < IDWriteFontSet1_GetFontCount(sys));
        DWRITE_FONT_AXIS_RANGE ranges[8];
        UINT32 n = 0;
        BOOL ok = bolds && SUCCEEDED(V(bolds, GetFontAxisRanges)(bolds, ranges, 8, &n)) && n > 0;
        for (UINT32 i = 0; ok && i < n; i++)
            if (ranges[i].axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT) ok = ranges[i].minValue >= 600;
        CHECK("their weight axis range is 600 or more", ok);
        UINT32 idx[4], got = 0;
        CHECK("GetFilteredFontIndices by a weight range",
              SUCCEEDED(V(sys, GetFilteredFontIndices_)(sys, &heavy, 1, FALSE, idx, 4, &got)) || got > 4);
        IDWriteFontFace5 *sface = NULL;
        CHECK("IDWriteFontSet1::CreateFontFace", SUCCEEDED(IDWriteFontSet1_CreateFontFace(sys, 0, &sface)) && sface);
        if (sface) IDWriteFontFace5_Release(sface);
        DWRITE_FONT_PROPERTY prop = { DWRITE_FONT_PROPERTY_ID_TYPOGRAPHIC_FAMILY_NAME, L"Segoe UI", L"" };
        IDWriteFontSet1 *matched = NULL;
        CHECK("IDWriteFontSet1::GetMatchingFonts(family, wght 700)",
              SUCCEEDED(V(sys, IDWriteFontSet1_GetMatchingFonts)(sys, &prop, bold, 4, &matched)) && matched &&
              IDWriteFontSet1_GetFontCount(matched) > 0);
        IDWriteFontFaceReference1 *ref = NULL;
        IDWriteFontFace5 *mface = NULL;
        CHECK("its best match is bold",
              matched && SUCCEEDED(V(matched, IDWriteFontSet1_GetFontFaceReference)(matched, 0, &ref)) && ref &&
              SUCCEEDED(V(ref, IDWriteFontFaceReference1_CreateFontFace)(ref, &mface)) && mface &&
              IDWriteFontFace5_GetWeight(mface) >= DWRITE_FONT_WEIGHT_SEMI_BOLD);

        /* a set builder from a file path, and a collection from it (XAML's custom fonts) */
        WCHAR path[MAX_PATH];
        path[0] = 0;
        if (ref) path_of((IDWriteFontFaceReference *)ref, path, MAX_PATH);
        IDWriteFontSetBuilder2 *builder = NULL;
        IDWriteFontSet *built = NULL;
        IDWriteFontCollection2 *custom = NULL;
        CHECK("IDWriteFontSetBuilder2::AddFontFile(path)",
              path[0] && SUCCEEDED(IDWriteFactory6_CreateFontSetBuilder(f6, &builder)) && builder &&
              SUCCEEDED(V(builder, IDWriteFontSetBuilder2_AddFontFile)(builder, path)) &&
              SUCCEEDED(IDWriteFontSetBuilder2_CreateFontSet(builder, &built)) && built && IDWriteFontSet_GetFontCount(built) > 0);
        CHECK("CreateFontCollectionFromFontSet(typographic)",
              built && SUCCEEDED(V(f6, IDWriteFactory6_CreateFontCollectionFromFontSet)(f6, built, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &custom)) &&
              custom && IDWriteFontCollection2_GetFontFamilyCount(custom) > 0 &&
              IDWriteFontCollection2_GetFontFamilyModel(custom) == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC);
        CHECK("AddFontFile of a missing file fails",
              builder && V(builder, IDWriteFontSetBuilder2_AddFontFile)(builder, L"C:\\no-such-font.ttf") == DWRITE_E_FILENOTFOUND);

        /* the same file from memory */
        HANDLE h = path[0] ? CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
        DWORD size = h != INVALID_HANDLE_VALUE ? GetFileSize(h, NULL) : 0, read = 0;
        BYTE *bytes = size ? HeapAlloc(GetProcessHeap(), 0, size) : NULL;
        if (bytes) ReadFile(h, bytes, size, &read, NULL);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        IDWriteInMemoryFontFileLoader *mem = NULL;
        IDWriteFontFile *mfile = NULL;
        BOOL supported = FALSE;
        DWRITE_FONT_FILE_TYPE ftype;
        DWRITE_FONT_FACE_TYPE fctype;
        UINT32 faces = 0;
        CHECK("an in-memory font file",
              bytes && read == size && SUCCEEDED(IDWriteFactory6_CreateInMemoryFontFileLoader(f6, &mem)) && mem &&
              SUCCEEDED(IDWriteFactory6_RegisterFontFileLoader(f6, (IDWriteFontFileLoader *)mem)) &&
              SUCCEEDED(IDWriteInMemoryFontFileLoader_CreateInMemoryFontFileReference(mem, (IDWriteFactory *)f6, bytes, size, NULL, &mfile)) &&
              mfile && SUCCEEDED(IDWriteFontFile_Analyze(mfile, &supported, &ftype, &fctype, &faces)) && supported && faces > 0 &&
              IDWriteInMemoryFontFileLoader_GetFileCount(mem) == 1);
        if (mfile) IDWriteFontFile_Release(mfile);
        if (mem) {
            IDWriteFactory6_UnregisterFontFileLoader(f6, (IDWriteFontFileLoader *)mem);
            IDWriteInMemoryFontFileLoader_Release(mem);
        }
        if (bytes) HeapFree(GetProcessHeap(), 0, bytes);
        static const BYTE woff[8] = { 'w', 'O', 'F', 'F', 0, 1, 0, 0 };
        CHECK("AnalyzeContainerType(WOFF)", IDWriteFactory6_AnalyzeContainerType(f6, woff, 8) == DWRITE_CONTAINER_TYPE_WOFF);

        if (custom) IDWriteFontCollection2_Release(custom);
        if (built) IDWriteFontSet_Release(built);
        if (builder) IDWriteFontSetBuilder2_Release(builder);
        if (mface) IDWriteFontFace5_Release(mface);
        if (ref) IDWriteFontFaceReference1_Release(ref);
        if (matched) IDWriteFontSet1_Release(matched);
        if (bolds) IDWriteFontSet1_Release(bolds);
        IDWriteFontSet1_Release(sys);
    }

    /* ---- glyph origins ---- */
    if (face) {
        UINT16 glyphs[2];
        UINT32 cps[2] = { 'A', 'B' };
        FLOAT adv[2] = { 10, 12 };
        D2D1_POINT_2F origins[2], base = { 5, 20 };
        IDWriteFontFace_GetGlyphIndices(face, cps, 2, glyphs);
        DWRITE_GLYPH_RUN run = { face, 16, 2, glyphs, adv, NULL, FALSE, 0 };
        CHECK("ComputeGlyphOrigins",
              SUCCEEDED(V(f6, ComputeGlyphOrigins_)(f6, &run, base, origins)) &&
              origins[0].x == 5 && origins[1].x == 15 && origins[1].y == 20);
    }

    /* ---- text formats and layouts ---- */
    IDWriteTextFormat3 *fmt = NULL;
    hr = IDWriteFactory6_CreateTextFormat(f6, L"Segoe UI", (IDWriteFontCollection *)coll, bold, 4, 15.0f, L"en-us", &fmt);
    CHECK("IDWriteFactory6::CreateTextFormat with axis values", SUCCEEDED(hr) && fmt);
    CHECK("its weight comes from wght", fmt && IDWriteTextFormat3_GetFontWeight(fmt) == DWRITE_FONT_WEIGHT_BOLD &&
          IDWriteTextFormat3_GetFontAxisValueCount(fmt) == 4);
    DWRITE_LINE_SPACING spacing = { DWRITE_LINE_SPACING_METHOD_UNIFORM, 20, 16, 0.5f, DWRITE_FONT_LINE_GAP_USAGE_ENABLED }, back;
    CHECK("IDWriteTextFormat2 line spacing",
          fmt && SUCCEEDED(V(fmt, IDWriteTextFormat2_SetLineSpacing)(fmt, &spacing)) &&
          SUCCEEDED(V(fmt, IDWriteTextFormat2_GetLineSpacing)(fmt, &back)) && back.height == 20 && back.leadingBefore == 0.5f);
    IDWriteTextLayout *layout = NULL;
    IDWriteTextLayout4 *layout4 = NULL;
    hr = fmt ? IDWriteFactory6_CreateTextLayout(f6, L"Hello, world", 12, (IDWriteTextFormat *)fmt, 200, 50, &layout) : E_FAIL;
    CHECK("CreateTextLayout from an IDWriteTextFormat3", SUCCEEDED(hr) && layout);
    CHECK("the layout is an IDWriteTextLayout4",
          layout && SUCCEEDED(IDWriteTextLayout_QueryInterface(layout, &IID_IDWriteTextLayout4, (void **)&layout4)) && layout4);
    if (layout4) {
        DWRITE_FONT_AXIS_VALUE light[] = { { DWRITE_FONT_AXIS_TAG_WEIGHT, 300 } }, got[4];
        DWRITE_TEXT_RANGE r = { 0, 5 }, out;
        DWRITE_FONT_WEIGHT w = 0;
        CHECK("SetFontAxisValues on a range",
              SUCCEEDED(IDWriteTextLayout4_SetFontAxisValues(layout4, light, 1, r)) &&
              IDWriteTextLayout4_GetFontAxisValueCount(layout4, 2) == 1 &&
              SUCCEEDED(V(layout4, GetFontAxisValues)(layout4, 2, got, 4, &out)) && got[0].value == 300 &&
              out.startPosition == 0 && out.length == 5 &&
              SUCCEEDED(V(layout4, IDWriteTextLayout_GetFontWeight)(layout4, 2, &w, NULL)) && w == DWRITE_FONT_WEIGHT_LIGHT);
        IDWriteTextFormat1 *f1 = NULL;
        IDWriteTextLayout *same = NULL;
        CHECK("the layout's IDWriteTextFormat1 sets its optical alignment",
              SUCCEEDED(IDWriteTextLayout4_QueryInterface(layout4, &IID_IDWriteTextFormat1, (void **)&f1)) && f1 &&
              SUCCEEDED(IDWriteTextFormat1_SetOpticalAlignment(f1, DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS)) &&
              IDWriteTextLayout4_GetOpticalAlignment(layout4) == DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS &&
              IDWriteTextFormat1_GetFontSize(f1) == 15.0f);
        CHECK("and leads back to the layout",
              f1 && SUCCEEDED(IDWriteTextFormat1_QueryInterface(f1, &IID_IDWriteTextLayout, (void **)&same)) && same == layout);
        if (same) IDWriteTextLayout_Release(same);
        if (f1) IDWriteTextFormat1_Release(f1);
        DWRITE_TEXT_METRICS1 m;
        CHECK("IDWriteTextLayout2::GetMetrics", SUCCEEDED(V(layout4, IDWriteTextLayout2_GetMetrics)(layout4, &m)) &&
              m.width > 0 && m.heightIncludingTrailingWhitespace >= m.height && m.lineCount == 1);
        DWRITE_LINE_METRICS1 lm[2];
        UINT32 lines = 0;
        CHECK("IDWriteTextLayout3::GetLineMetrics", SUCCEEDED(V(layout4, IDWriteTextLayout3_GetLineMetrics)(layout4, lm, 2, &lines)) &&
              lines == 1 && lm[0].length == 12);
        IDWriteFontFallback *sysfb = NULL, *fbback = NULL;
        CHECK("IDWriteTextLayout2::SetFontFallback",
              SUCCEEDED(IDWriteFactory6_GetSystemFontFallback(f6, &sysfb)) && sysfb &&
              SUCCEEDED(IDWriteTextLayout4_SetFontFallback(layout4, sysfb)) &&
              SUCCEEDED(IDWriteTextLayout4_GetFontFallback(layout4, &fbback)) && fbback == sysfb);
        if (fbback) IDWriteFontFallback_Release(fbback);

        /* ---- IDWriteFontFallback1 ---- */
        IDWriteFontFallback1 *fb1 = NULL;
        Source src = { { &src_vtbl }, L"abc", 3 };
        UINT32 mapped = 0;
        FLOAT scale = 0;
        IDWriteFontFace5 *mf = NULL;
        CHECK("IDWriteFontFallback1::MapCharacters",
              sysfb && SUCCEEDED(IDWriteFontFallback_QueryInterface(sysfb, &IID_IDWriteFontFallback1, (void **)&fb1)) && fb1 &&
              SUCCEEDED(V(fb1, IDWriteFontFallback1_MapCharacters)(fb1, &src.iface, 0, 3, (IDWriteFontCollection *)coll, L"Segoe UI",
                                                                   bold, 4, &mapped, &scale, &mf)) &&
              mapped == 3 && scale == 1.0f && mf && IDWriteFontFace5_GetWeight(mf) >= DWRITE_FONT_WEIGHT_SEMI_BOLD);
        if (mf) IDWriteFontFace5_Release(mf);
        if (fb1) IDWriteFontFallback1_Release(fb1);
        if (sysfb) IDWriteFontFallback_Release(sysfb);
        IDWriteTextLayout4_Release(layout4);
    }
    if (layout) IDWriteTextLayout_Release(layout);
    if (fmt) IDWriteTextFormat3_Release(fmt);

    /* ---- IDWriteTextAnalyzer1/2 ---- */
    IDWriteTextAnalyzer *an = NULL;
    IDWriteTextAnalyzer2 *an2 = NULL;
    CHECK("the text analyzer is an IDWriteTextAnalyzer2",
          SUCCEEDED(IDWriteFactory6_CreateTextAnalyzer(f6, &an)) && an &&
          SUCCEEDED(IDWriteTextAnalyzer_QueryInterface(an, &IID_IDWriteTextAnalyzer2, (void **)&an2)) && an2);
    if (an2 && face) {
        DWRITE_SCRIPT_ANALYSIS latin = { 25, DWRITE_SCRIPT_SHAPES_DEFAULT };   /* the script AnalyzeScript gives Latin text */
        DWRITE_SCRIPT_PROPERTIES props;
        CHECK("GetScriptProperties(Latin)",
              SUCCEEDED(IDWriteTextAnalyzer2_GetScriptProperties(an2, latin, &props)) &&
              props.isoScriptCode == TAG('L', 'a', 't', 'n') && props.isoScriptNumber == 215 && props.usesWordDividers &&
              !props.restrictCaretToClusters);
        BOOL simple = FALSE;
        UINT32 len = 0;
        UINT16 idx[8];
        CHECK("GetTextComplexity: Latin is simple",
              SUCCEEDED(IDWriteTextAnalyzer2_GetTextComplexity(an2, L"Hi there", 8, face, &simple, &len, idx)) &&
              simple && len == 8 && idx[0] != 0);
        CHECK("GetTextComplexity: Arabic is not",
              SUCCEEDED(IDWriteTextAnalyzer2_GetTextComplexity(an2, L"\x0645\x0631", 2, face, &simple, &len, NULL)) &&
              !simple && len == 2);
        UINT16 cmap[3] = { 0, 1, 2 };
        DWRITE_SHAPING_GLYPH_PROPERTIES gprops[3];
        DWRITE_JUSTIFICATION_OPPORTUNITY jo[3];
        memset(gprops, 0, sizeof(gprops));
        CHECK("GetJustificationOpportunities: the space stretches",
              SUCCEEDED(IDWriteTextAnalyzer2_GetJustificationOpportunities(an2, NULL, 0, latin, 3, 3, L"a b", cmap, gprops, jo)) &&
              jo[1].expansionPriority > 0 && jo[1].applyToTrailingEdge && jo[0].expansionPriority == 0);
        DWRITE_MATRIX m;
        CHECK("GetGlyphOrientationTransform(90)",
              SUCCEEDED(IDWriteTextAnalyzer2_GetGlyphOrientationTransform(an2, DWRITE_GLYPH_ORIENTATION_ANGLE_90_DEGREES, FALSE, 10, 0, &m)) &&
              m.m11 == 0 && m.m12 == 1 && m.m21 == -1 && m.dx == 10 && m.dy == -10);
        UINT32 tags[64], ntags = 0;
        hr = IDWriteTextAnalyzer2_GetTypographicFeatures(an2, face, latin, L"en-us", 64, &ntags, tags);
        CHECK("GetTypographicFeatures", SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
        INT32 coord = 1;
        BOOL has = FALSE;
        CHECK("GetBaseline(roman) is the origin",
              SUCCEEDED(IDWriteTextAnalyzer2_GetBaseline(an2, face, DWRITE_BASELINE_ROMAN, FALSE, TRUE, latin, L"en-us", &coord, &has)) &&
              coord == 0 && has);
    }
    if (an2) IDWriteTextAnalyzer2_Release(an2);
    if (an) IDWriteTextAnalyzer_Release(an);

    if (face5) IDWriteFontFace5_Release(face5);
    if (face) IDWriteFontFace_Release(face);
    if (famset) IDWriteFontSet1_Release(famset);
    if (font) IDWriteFont_Release(font);
    if (list) IDWriteFontList2_Release(list);
    if (fam) IDWriteFontFamily2_Release(fam);
    if (wws) IDWriteFontCollection2_Release(wws);
    if (coll) IDWriteFontCollection2_Release(coll);
    if (f7) IDWriteFactory7_Release(f7);
    IDWriteFactory6_Release(f6);
    printf("dw6test: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}
