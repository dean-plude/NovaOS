/*
 * imagelist.c — image lists: equal-sized images kept as 32-bit ARGB, drawn
 * with their transparency (from an alpha channel, a mask or a color key)
 */
#include "cc.h"

#define IL_MAGIC 0x494D4C32u

typedef struct _IMAGELIST {
    const void *vtbl;               /* an HIMAGELIST is its IImageList, as on Windows (SHGetImageList's) */
    LONG refs;
    DWORD magic;
    int cx, cy, count, cap, grow;
    UINT flags;
    DWORD *px;                      /* count images of cx * cy ARGB (not premultiplied) */
    COLORREF bk;
    int overlay[16];
} IL;

static const void *const g_il_vtbl[];

static IL *il(HIMAGELIST h)
{
    IL *l = (IL *)h;
    return l && l->magic == IL_MAGIC ? l : NULL;
}

static int reserve(IL *l, int n)
{
    if (n <= l->cap) return 1;
    int nc = MAX(n, l->cap + MAX(l->grow, 4));
    DWORD *p = realloc(l->px, (size_t)nc * l->cx * l->cy * 4);
    if (!p) return 0;
    l->px = p;
    l->cap = nc;
    return 1;
}

static DWORD *img(IL *l, int i) { return l->px + (size_t)i * l->cx * l->cy; }

CC HIMAGELIST WINAPI ImageList_Create(int cx, int cy, UINT flags, int initial, int grow)
{
    if (cx <= 0 || cy <= 0) return NULL;
    IL *l = calloc(1, sizeof(IL));
    if (!l) return NULL;
    l->vtbl = g_il_vtbl;
    l->refs = 1;
    l->magic = IL_MAGIC;
    l->cx = cx; l->cy = cy; l->flags = flags; l->grow = grow > 0 ? grow : 4;
    l->bk = CLR_NONE;
    for (int i = 0; i < 16; i++) l->overlay[i] = -1;
    reserve(l, initial > 0 ? initial : 4);
    return (HIMAGELIST)l;
}

CC BOOL WINAPI ImageList_Destroy(HIMAGELIST h)
{
    IL *l = il(h);
    if (!l) return FALSE;
    l->magic = 0;
    free(l->px);
    free(l);
    return TRUE;
}

CC int WINAPI ImageList_GetImageCount(HIMAGELIST h) { IL *l = il(h); return l ? l->count : 0; }

CC BOOL WINAPI ImageList_SetImageCount(HIMAGELIST h, UINT n)
{
    IL *l = il(h);
    if (!l || !reserve(l, (int)n)) return FALSE;
    for (int i = l->count; i < (int)n; i++) memset(img(l, i), 0, (size_t)l->cx * l->cy * 4);
    l->count = (int)n;
    return TRUE;
}

CC BOOL WINAPI ImageList_GetIconSize(HIMAGELIST h, int *cx, int *cy)
{
    IL *l = il(h);
    if (!l) return FALSE;
    if (cx) *cx = l->cx;
    if (cy) *cy = l->cy;
    return TRUE;
}

CC BOOL WINAPI ImageList_SetIconSize(HIMAGELIST h, int cx, int cy)
{
    IL *l = il(h);
    if (!l || cx <= 0 || cy <= 0) return FALSE;
    free(l->px);
    l->px = NULL; l->cap = 0; l->count = 0;
    l->cx = cx; l->cy = cy;
    reserve(l, 4);
    return TRUE;
}

CC COLORREF WINAPI ImageList_SetBkColor(HIMAGELIST h, COLORREF c) { IL *l = il(h); if (!l) return CLR_NONE; COLORREF o = l->bk; l->bk = c; return o; }
CC COLORREF WINAPI ImageList_GetBkColor(HIMAGELIST h) { IL *l = il(h); return l ? l->bk : CLR_NONE; }
CC BOOL WINAPI ImageList_SetOverlayImage(HIMAGELIST h, int i, int ov) { IL *l = il(h); if (!l || ov < 1 || ov > 15) return FALSE; l->overlay[ov] = i; return TRUE; }

/* A bitmap's pixels as 32-bit top-down BGRA; *w, *h its size */
static DWORD *bitmap_pixels(HBITMAP bm, int *w, int *h, int *has_alpha)
{
    BITMAP b;
    if (!bm || !GetObjectW(bm, sizeof(b), &b)) return NULL;
    *w = b.bmWidth; *h = b.bmHeight;
    DWORD *p = malloc((size_t)*w * *h * 4);
    if (!p) return NULL;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = *w; bi.bmiHeader.biHeight = -*h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(NULL);
    GetDIBits(dc, bm, 0, (UINT)*h, p, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    /* an alpha channel counts only when it varies: all 0 or all 255 is a plain bitmap */
    *has_alpha = 0;
    if (b.bmBitsPixel == 32) {
        int zero = 0, full = 0;
        for (int i = 0; i < *w * *h; i++) {
            DWORD a = p[i] >> 24;
            if (a == 0) zero = 1; else if (a == 255) full = 1; else { *has_alpha = 1; break; }
        }
        if (zero && full) *has_alpha = 1;
    }
    return p;
}

/* Add the images in a strip; transparent where @mask says (mask bitmap / color key) */
static int add_strip(IL *l, HBITMAP bm, HBITMAP mask, COLORREF key, int use_key)
{
    int w, h, alpha;
    DWORD *p = bitmap_pixels(bm, &w, &h, &alpha);
    if (!p) return -1;
    DWORD *m = NULL;
    int mw = 0, mh = 0, ma;
    if (mask) m = bitmap_pixels(mask, &mw, &mh, &ma);
    int n = w / l->cx;
    if (n < 1) n = 1;
    if (!reserve(l, l->count + n)) { free(p); free(m); return -1; }
    int first = l->count;
    DWORD keyc = (key & 0xFF) << 16 | (key & 0xFF00) | ((key >> 16) & 0xFF);
    /* premultiplied (DIB section) alpha: not knowable; treat the colors as straight */
    for (int k = 0; k < n; k++) {
        DWORD *d = img(l, first + k);
        for (int y = 0; y < l->cy; y++)
            for (int x = 0; x < l->cx; x++) {
                int sx = k * l->cx + x;
                DWORD c = sx < w && y < h ? p[(size_t)y * w + sx] : 0;
                if (alpha && !(use_key && (c & 0xFFFFFF) == keyc)) {
                    int a = (int)(c >> 24);
                    if (a && a < 255) {                     /* un-premultiply */
                        int r = MIN(255, (int)((c >> 16) & 0xFF) * 255 / a), g = MIN(255, (int)((c >> 8) & 0xFF) * 255 / a), b = MIN(255, (int)(c & 0xFF) * 255 / a);
                        c = (DWORD)a << 24 | (DWORD)r << 16 | (DWORD)g << 8 | (DWORD)b;
                    }
                } else {
                    int transparent = 0;
                    if (m && sx < mw && y < mh) transparent = (m[(size_t)y * mw + sx] & 0xFFFFFF) != 0;
                    else if (use_key) transparent = (c & 0xFFFFFF) == keyc;
                    c = transparent ? 0 : (c | 0xFF000000u);
                }
                d[(size_t)y * l->cx + x] = c;
            }
    }
    l->count += n;
    free(p); free(m);
    return first;
}

CC int WINAPI ImageList_Add(HIMAGELIST h, HBITMAP bm, HBITMAP mask)
{
    IL *l = il(h);
    return l ? add_strip(l, bm, (l->flags & ILC_MASK) ? mask : NULL, 0, 0) : -1;
}

CC int WINAPI ImageList_AddMasked(HIMAGELIST h, HBITMAP bm, COLORREF key)
{
    IL *l = il(h);
    return l ? add_strip(l, bm, NULL, key, 1) : -1;
}

CC BOOL WINAPI ImageList_Replace(HIMAGELIST h, int i, HBITMAP bm, HBITMAP mask)
{
    IL *l = il(h);
    if (!l || i < 0 || i >= l->count) return FALSE;
    int n = l->count;
    int at = add_strip(l, bm, mask, 0, 0);
    if (at < 0) return FALSE;
    memcpy(img(l, i), img(l, at), (size_t)l->cx * l->cy * 4);
    l->count = n;
    return TRUE;
}

/* An icon's pixels (ARGB) scaled to the list's size */
static int icon_into(IL *l, DWORD *d, HICON icon)
{
    ICONINFO ii;
    if (!GetIconInfo(icon, &ii)) return 0;
    int w, h, alpha;
    DWORD *p = bitmap_pixels(ii.hbmColor, &w, &h, &alpha);
    int mw = 0, mh = 0, ma;
    DWORD *m = alpha ? NULL : bitmap_pixels(ii.hbmMask, &mw, &mh, &ma);
    if (p) {
        for (int y = 0; y < l->cy; y++)
            for (int x = 0; x < l->cx; x++) {
                int sx = x * w / l->cx, sy = y * h / l->cy;
                DWORD c = p[(size_t)sy * w + sx];
                if (!alpha) {
                    int t = m && sx < mw && sy < mh ? (m[(size_t)sy * mw + sx] & 0xFFFFFF) != 0 : 0;
                    c = t ? 0 : c | 0xFF000000u;
                }
                d[(size_t)y * l->cx + x] = c;
            }
    }
    free(p); free(m);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return p != NULL;
}

CC int WINAPI ImageList_ReplaceIcon(HIMAGELIST h, int i, HICON icon)
{
    IL *l = il(h);
    if (!l || !icon) return -1;
    if (i < 0 || i >= l->count) {
        if (!reserve(l, l->count + 1)) return -1;
        i = l->count;
        memset(img(l, i), 0, (size_t)l->cx * l->cy * 4);
        l->count++;
    }
    icon_into(l, img(l, i), icon);
    return i;
}

#undef ImageList_AddIcon
CC int WINAPI ImageList_AddIcon(HIMAGELIST h, HICON icon) { return ImageList_ReplaceIcon(h, -1, icon); }

CC BOOL WINAPI ImageList_Remove(HIMAGELIST h, int i)
{
    IL *l = il(h);
    if (!l) return FALSE;
    if (i < 0) { l->count = 0; return TRUE; }
    if (i >= l->count) return FALSE;
    size_t sz = (size_t)l->cx * l->cy;
    memmove(img(l, i), img(l, i + 1), sz * 4 * (size_t)(l->count - i - 1));
    l->count--;
    return TRUE;
}

CC BOOL WINAPI ImageList_Copy(HIMAGELIST hd, int di, HIMAGELIST hs, int si, UINT flags)
{
    IL *d = il(hd), *s = il(hs);
    if (!d || !s || si < 0 || si >= s->count || di < 0 || di >= d->count || d->cx != s->cx || d->cy != s->cy) return FALSE;
    size_t sz = (size_t)d->cx * d->cy * 4;
    if (flags & 1 /* ILCF_SWAP */) {
        DWORD *t = malloc(sz);
        if (!t) return FALSE;
        memcpy(t, img(d, di), sz); memcpy(img(d, di), img(s, si), sz); memcpy(img(s, si), t, sz);
        free(t);
    } else memcpy(img(d, di), img(s, si), sz);
    return TRUE;
}

CC HIMAGELIST WINAPI ImageList_Duplicate(HIMAGELIST h)
{
    IL *l = il(h);
    if (!l) return NULL;
    IL *n = (IL *)ImageList_Create(l->cx, l->cy, l->flags, l->count, l->grow);
    if (!n) return NULL;
    reserve(n, l->count);
    memcpy(n->px, l->px, (size_t)l->count * l->cx * l->cy * 4);
    n->count = l->count;
    n->bk = l->bk;
    memcpy(n->overlay, l->overlay, sizeof(n->overlay));
    return (HIMAGELIST)n;
}

/* Draw image @i at (x, y), scaled to cx x cy (0: its size) */
static BOOL draw(IL *l, int i, HDC dc, int x, int y, int cx, int cy, COLORREF bk, UINT style)
{
    if (i < 0 || i >= l->count) return FALSE;
    if (!cx) cx = l->cx;
    if (!cy) cy = l->cy;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = l->cx; bi.bmiHeader.biHeight = -l->cy; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP bm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm) return FALSE;
    DWORD *d = bits;
    const DWORD *s = img(l, i);
    int blend = (style & (ILD_BLEND25 | ILD_BLEND50)) != 0;
    COLORREF hi = GetSysColor(COLOR_HIGHLIGHT);
    for (int k = 0; k < l->cx * l->cy; k++) {
        DWORD c = s[k];
        int a = (int)(c >> 24);
        int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        if (blend && a) {
            int f = (style & ILD_BLEND50) ? 128 : 64;
            r = (r * (256 - f) + GetRValue(hi) * f) >> 8; g = (g * (256 - f) + GetGValue(hi) * f) >> 8; b = (b * (256 - f) + GetBValue(hi) * f) >> 8;
        }
        if (bk != CLR_NONE && bk != CLR_DEFAULT && !(style & ILD_TRANSPARENT)) {
            int br = GetRValue(bk), bg = GetGValue(bk), bb = GetBValue(bk);
            r = (r * a + br * (255 - a)) / 255; g = (g * a + bg * (255 - a)) / 255; b = (b * a + bb * (255 - a)) / 255;
            a = 255;
        }
        /* premultiplied for AlphaBlend */
        d[k] = (DWORD)a << 24 | (DWORD)(r * a / 255) << 16 | (DWORD)(g * a / 255) << 8 | (DWORD)(b * a / 255);
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ o = SelectObject(mem, bm);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dc, x, y, cx, cy, mem, 0, 0, l->cx, l->cy, bf);
    SelectObject(mem, o);
    DeleteDC(mem);
    DeleteObject(bm);
    int ov = (int)((style & ILD_OVERLAYMASK) >> 8);
    if (ov > 0 && ov < 16 && l->overlay[ov] >= 0 && l->overlay[ov] != i) draw(l, l->overlay[ov], dc, x, y, cx, cy, CLR_NONE, ILD_TRANSPARENT);
    return TRUE;
}

int il_draw(HIMAGELIST h, int i, HDC dc, int x, int y, UINT style)
{
    IL *l = il(h);
    return l ? draw(l, i, dc, x, y, 0, 0, l->bk, style) : 0;
}

CC BOOL WINAPI ImageList_Draw(HIMAGELIST h, int i, HDC dc, int x, int y, UINT style)
{
    IL *l = il(h);
    return l ? draw(l, i, dc, x, y, 0, 0, l->bk, style) : FALSE;
}

CC BOOL WINAPI ImageList_DrawEx(HIMAGELIST h, int i, HDC dc, int x, int y, int cx, int cy, COLORREF bk, COLORREF fg, UINT style)
{
    (void)fg;
    IL *l = il(h);
    if (!l) return FALSE;
    if (bk == CLR_DEFAULT) bk = l->bk;
    return draw(l, i, dc, x, y, cx, cy, bk, style);
}

CC BOOL WINAPI ImageList_DrawIndirect(IMAGELISTDRAWPARAMS *p)
{
    if (!p) return FALSE;
    return ImageList_DrawEx(p->himl, p->i, p->hdcDst, p->x, p->y, p->cx, p->cy, p->rgbBk, p->rgbFg, p->fStyle);
}

CC HICON WINAPI ImageList_GetIcon(HIMAGELIST h, int i, UINT flags)
{
    (void)flags;
    IL *l = il(h);
    if (!l || i < 0 || i >= l->count) return NULL;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = l->cx; bi.bmiHeader.biHeight = -l->cy; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits, *mbits;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP mask = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &mbits, NULL, 0);
    if (!color || !mask) { DeleteObject(color); DeleteObject(mask); return NULL; }
    memcpy(bits, img(l, i), (size_t)l->cx * l->cy * 4);
    for (int k = 0; k < l->cx * l->cy; k++) ((DWORD *)mbits)[k] = (img(l, i)[k] >> 24) ? 0 : 0xFFFFFF;
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON r = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return r;
}

CC BOOL WINAPI ImageList_GetImageInfo(HIMAGELIST h, int i, IMAGEINFO *info)
{
    IL *l = il(h);
    if (!l || !info || i < 0 || i >= l->count) return FALSE;
    memset(info, 0, sizeof(*info));
    SetRect(&info->rcImage, i * l->cx, 0, (i + 1) * l->cx, l->cy);
    return TRUE;
}

CC HIMAGELIST WINAPI ImageList_LoadImageW(HINSTANCE inst, LPCWSTR name, int cx, int grow, COLORREF key, UINT type, UINT flags)
{
    if (type == IMAGE_ICON) {
        HICON ic = LoadImageW(inst, name, IMAGE_ICON, cx, cx, flags);
        if (!ic) return NULL;
        HIMAGELIST l = ImageList_Create(cx, cx, ILC_COLOR32 | ILC_MASK, 1, grow);
        ImageList_ReplaceIcon(l, -1, ic);
        DestroyIcon(ic);
        return l;
    }
    HBITMAP bm = LoadImageW(inst, name, IMAGE_BITMAP, 0, 0, flags);
    if (!bm) return NULL;
    BITMAP b;
    GetObjectW(bm, sizeof(b), &b);
    HIMAGELIST l = ImageList_Create(cx, b.bmHeight, ILC_COLOR32 | ILC_MASK, b.bmWidth / MAX(cx, 1), grow);
    if (key == CLR_NONE) ImageList_Add(l, bm, NULL);
    else ImageList_AddMasked(l, bm, key == CLR_DEFAULT ? 0xFF00FF : key);
    DeleteObject(bm);
    return l;
}

CC HIMAGELIST WINAPI ImageList_LoadImageA(HINSTANCE inst, LPCSTR name, int cx, int grow, COLORREF key, UINT type, UINT flags)
{
    if ((ULONG_PTR)name < 0x10000) return ImageList_LoadImageW(inst, (LPCWSTR)name, cx, grow, key, type, flags);
    WCHAR w[260];
    MultiByteToWideChar(CP_ACP, 0, name, -1, w, 260);
    return ImageList_LoadImageW(inst, w, cx, grow, key, type, flags);
}

CC HIMAGELIST WINAPI ImageList_Merge(HIMAGELIST a, int ia, HIMAGELIST b, int ib, int dx, int dy)
{
    (void)b; (void)ib; (void)dx; (void)dy;
    IL *l = il(a);
    if (!l) return NULL;
    HIMAGELIST n = ImageList_Create(l->cx, l->cy, l->flags, 1, 1);
    IL *nl = il(n);
    if (nl && ia >= 0 && ia < l->count) { reserve(nl, 1); memcpy(nl->px, img(l, ia), (size_t)l->cx * l->cy * 4); nl->count = 1; }
    return n;
}

/* Dragging images: not shown */
CC BOOL WINAPI ImageList_BeginDrag(HIMAGELIST h, int i, int dx, int dy) { (void)h; (void)i; (void)dx; (void)dy; return TRUE; }
CC void WINAPI ImageList_EndDrag(void) { }
CC BOOL WINAPI ImageList_DragEnter(HWND h, int x, int y) { (void)h; (void)x; (void)y; return TRUE; }
CC BOOL WINAPI ImageList_DragLeave(HWND h) { (void)h; return TRUE; }
CC BOOL WINAPI ImageList_DragMove(int x, int y) { (void)x; (void)y; return TRUE; }
CC BOOL WINAPI ImageList_DragShowNolock(BOOL show) { (void)show; return TRUE; }
CC BOOL WINAPI ImageList_SetDragCursorImage(HIMAGELIST h, int i, int dx, int dy) { (void)h; (void)i; (void)dx; (void)dy; return TRUE; }
CC HIMAGELIST WINAPI ImageList_GetDragImage(POINT *p, POINT *hot) { (void)p; (void)hot; return NULL; }
/* ImageList_Read and ImageList_Write: an image list in a stream, as
 * Windows writes it (Delphi keeps its forms' image lists this way):
 *
 *   ILHEAD     "IL", version 0x101, the image count, capacity and growth,
 *              the image size, background colour, ILC_* flags and the
 *              four overlay images (-1: none), 28 bytes
 *   BMP file   the images, four to a row (cx * 4 by cy * rows)
 *   BMP file   their mask, laid out the same (1: transparent), when the
 *              flags have ILC_MASK
 *
 * The streams are IStreams; only their Read and Write are used. */
typedef HRESULT (WINAPI *StmIO)(void *stm, void *buf, ULONG n, ULONG *done);
#define IL_TILES 4

#pragma pack(push, 2)
typedef struct { WORD magic, version, cur, max, grow, cx, cy; COLORREF bk; WORD flags; SHORT ovls[4]; } ILHEAD;
#pragma pack(pop)

static BOOL stm_read(void *stm, void *buf, ULONG n)
{
    ULONG done = 0;
    StmIO rd = (StmIO)(*(void ***)stm)[3];
    return SUCCEEDED(rd(stm, buf, n, &done)) && done == n;
}

static BOOL stm_write(void *stm, const void *buf, ULONG n)
{
    ULONG done = 0;
    StmIO wr = (StmIO)(*(void ***)stm)[4];
    return SUCCEEDED(wr(stm, (void *)buf, n, &done)) && done == n;
}

/* A BMP file from @stm: its pixels as 0x00RRGGBB (alpha too for 32-bit),
 * top row first; NULL if it cannot be read */
static DWORD *read_bmp(void *stm, int *w, int *h, int *bpp)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    if (!stm_read(stm, &fh, sizeof(fh)) || fh.bfType != 0x4D42 || !stm_read(stm, &ih, sizeof(ih)) ||
        ih.biSize < sizeof(ih) || ih.biWidth <= 0 || !ih.biHeight || ih.biWidth > 16384 || ABS(ih.biHeight) > 16384 ||
        (ih.biCompression != BI_RGB && ih.biCompression != BI_BITFIELDS))
        return NULL;
    BYTE skip[256];
    for (ULONG left = ih.biSize - sizeof(ih); left; ) {          /* (a V4/V5 header's rest) */
        ULONG k = MIN(left, sizeof(skip));
        if (!stm_read(stm, skip, k)) return NULL;
        left -= k;
    }
    int b = ih.biBitCount, colors = b <= 8 ? (ih.biClrUsed ? (int)ih.biClrUsed : 1 << b) : 0;
    if (b != 1 && b != 4 && b != 8 && b != 16 && b != 24 && b != 32) return NULL;
    RGBQUAD pal[256];
    if (colors > 256 || (colors && !stm_read(stm, pal, 4u * (ULONG)colors))) return NULL;
    DWORD masks[3];
    if (ih.biCompression == BI_BITFIELDS && !stm_read(stm, masks, sizeof(masks))) return NULL;
    /* the bits start where the file header says (any gap is skipped) */
    LONG at = (LONG)(sizeof(fh) + ih.biSize + 4u * (ULONG)colors + (ih.biCompression == BI_BITFIELDS ? 12 : 0));
    for (LONG gap = (LONG)fh.bfOffBits - at; gap > 0; ) {
        ULONG k = MIN((ULONG)gap, sizeof(skip));
        if (!stm_read(stm, skip, k)) return NULL;
        gap -= (LONG)k;
    }
    int W = ih.biWidth, H = ABS(ih.biHeight);
    size_t row = ((size_t)W * b + 31) / 32 * 4;
    BYTE *bits = malloc(row * H);
    DWORD *px = malloc((size_t)W * H * 4);
    if (!bits || !px || !stm_read(stm, bits, (ULONG)(row * H))) { free(bits); free(px); return NULL; }
    for (int y = 0; y < H; y++) {
        const BYTE *r = bits + row * (ih.biHeight > 0 ? H - 1 - y : y);
        DWORD *d = px + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            DWORD c;
            if (b == 32) c = ((const DWORD *)r)[x];
            else if (b == 24) c = (DWORD)r[3 * x] | (DWORD)r[3 * x + 1] << 8 | (DWORD)r[3 * x + 2] << 16;
            else if (b == 16) { WORD v = ((const WORD *)r)[x]; c = (DWORD)(v & 0x1F) << 3 | (DWORD)(v >> 5 & 0x1F) << 11 | (DWORD)(v >> 10 & 0x1F) << 19; }
            else {
                int i = b == 8 ? r[x] : b == 4 ? (r[x / 2] >> (x & 1 ? 0 : 4)) & 15 : (r[x / 8] >> (7 - x % 8)) & 1;
                RGBQUAD q = i < colors ? pal[i] : (RGBQUAD){ 0, 0, 0, 0 };
                c = (DWORD)q.rgbBlue | (DWORD)q.rgbGreen << 8 | (DWORD)q.rgbRed << 16;
            }
            d[x] = c;
        }
    }
    free(bits);
    *w = W; *h = H; *bpp = b;
    return px;
}

CC HIMAGELIST WINAPI ImageList_Read(struct IStream *stream)
{
    ILHEAD hd;
    if (!stream || !stm_read(stream, &hd, sizeof(hd)) || hd.magic != 0x4C49 || !hd.cx || !hd.cy) return NULL;
    int w, h, bpp, mw = 0, mh = 0, mbpp;
    DWORD *p = read_bmp(stream, &w, &h, &bpp), *m = NULL;
    if (!p) return NULL;
    if ((hd.flags & ILC_MASK) && !(m = read_bmp(stream, &mw, &mh, &mbpp))) { free(p); return NULL; }
    HIMAGELIST hl = ImageList_Create(hd.cx, hd.cy, hd.flags, MAX(hd.cur, hd.max), hd.grow);
    IL *l = il(hl);
    if (!l || !reserve(l, hd.cur)) { ImageList_Destroy(hl); free(p); free(m); return NULL; }
    /* an alpha channel counts only when it varies (0 or 255 throughout: a plain bitmap) */
    int alpha = 0;
    if (bpp == 32) {
        int zero = 0, full = 0;
        for (int i = 0; i < w * h && !alpha; i++) {
            DWORD a = p[i] >> 24;
            if (a == 0) zero = 1; else if (a == 255) full = 1; else alpha = 1;
        }
        alpha |= zero && full;
    }
    for (int k = 0; k < hd.cur; k++) {
        int ox = k % IL_TILES * hd.cx, oy = k / IL_TILES * hd.cy;
        DWORD *d = img(l, k);
        for (int y = 0; y < hd.cy; y++)
            for (int x = 0; x < hd.cx; x++) {
                int sx = ox + x, sy = oy + y;
                DWORD c = sx < w && sy < h ? p[(size_t)sy * w + sx] : 0;
                if (alpha) {
                    int a = (int)(c >> 24);                  /* (stored premultiplied, as Windows draws it) */
                    if (a && a < 255) {
                        int r = MIN(255, (int)((c >> 16) & 0xFF) * 255 / a), g = MIN(255, (int)((c >> 8) & 0xFF) * 255 / a), b = MIN(255, (int)(c & 0xFF) * 255 / a);
                        c = (DWORD)a << 24 | (DWORD)r << 16 | (DWORD)g << 8 | (DWORD)b;
                    }
                } else {
                    int clear = m && sx < mw && sy < mh && (m[(size_t)sy * mw + sx] & 0xFFFFFF);
                    c = clear ? 0 : (c | 0xFF000000u);
                }
                d[(size_t)y * hd.cx + x] = c;
            }
    }
    l->count = hd.cur;
    l->bk = hd.bk;
    for (int k = 0; k < 4; k++)
        if (hd.ovls[k] >= 0 && hd.ovls[k] < hd.cur) l->overlay[k + 1] = hd.ovls[k];
    free(p); free(m);
    return hl;
}

/* One BMP file of @w x @h: 32-bit @px (top row first), or the 1-bit mask
 * of where they are transparent */
static BOOL write_bmp(void *stm, const DWORD *px, int w, int h, BOOL mask)
{
    int b = mask ? 1 : 32, colors = mask ? 2 : 0;
    size_t row = ((size_t)w * b + 31) / 32 * 4;
    BITMAPFILEHEADER fh = { 0x4D42, 0, 0, 0, 0 };
    fh.bfOffBits = (DWORD)(sizeof(fh) + sizeof(BITMAPINFOHEADER) + 4 * colors);
    fh.bfSize = fh.bfOffBits + (DWORD)(row * h);
    BITMAPINFOHEADER ih = { sizeof(ih), w, h, 1, (WORD)b, BI_RGB, (DWORD)(row * h), 0, 0, 0, 0 };
    RGBQUAD pal[2] = { { 0, 0, 0, 0 }, { 255, 255, 255, 0 } };
    BYTE *bits = calloc(row, h);
    if (!bits) return FALSE;
    for (int y = 0; y < h; y++) {
        BYTE *r = bits + row * (h - 1 - y);                         /* bottom-up */
        for (int x = 0; x < w; x++) {
            DWORD c = px[(size_t)y * w + x];
            if (mask) { if (!(c >> 24)) r[x / 8] |= (BYTE)(0x80 >> (x % 8)); }
            else ((DWORD *)r)[x] = c;
        }
    }
    BOOL ok = stm_write(stm, &fh, sizeof(fh)) && stm_write(stm, &ih, sizeof(ih)) &&
              (!colors || stm_write(stm, pal, sizeof(pal))) && stm_write(stm, bits, (ULONG)(row * h));
    free(bits);
    return ok;
}

CC BOOL WINAPI ImageList_Write(HIMAGELIST h, struct IStream *stream)
{
    IL *l = il(h);
    if (!l || !stream) return FALSE;
    ILHEAD hd = { 0x4C49, 0x101, (WORD)l->count, (WORD)MAX(l->count, l->cap), (WORD)l->grow, (WORD)l->cx, (WORD)l->cy,
                  l->bk, (WORD)(l->flags | ILC_MASK), { -1, -1, -1, -1 } };
    for (int k = 0; k < 4; k++) hd.ovls[k] = (SHORT)l->overlay[k + 1];
    int rows = MAX(1, (l->count + IL_TILES - 1) / IL_TILES), w = l->cx * IL_TILES, ht = l->cy * rows;
    DWORD *px = calloc((size_t)w * ht, 4);
    if (!px) return FALSE;
    for (int k = 0; k < l->count; k++)
        for (int y = 0; y < l->cy; y++)
            memcpy(px + (size_t)(k / IL_TILES * l->cy + y) * w + k % IL_TILES * l->cx, img(l, k) + (size_t)y * l->cx, 4 * (size_t)l->cx);
    BOOL ok = stm_write(stream, &hd, sizeof(hd)) && write_bmp(stream, px, w, ht, FALSE) && write_bmp(stream, px, w, ht, TRUE);
    free(px);
    return ok;
}
/* ---- IImageList / IImageList2: the same list through COM (shell32's
 * SHGetImageList hands it out; .NET programs such as ShareX call GetIcon
 * on it through the vtable) ---- */
#define S_OK_ 0L
#define E_NOINTERFACE_ ((HRESULT)0x80004002L)
#define E_NOTIMPL_ ((HRESULT)0x80004001L)
#define E_INVALIDARG_ ((HRESULT)0x80070057L)
#define E_FAIL_ ((HRESULT)0x80004005L)
#define OK(b) ((b) ? S_OK_ : E_FAIL_)

static const GUID IID_IUnknown_ = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID IID_IImageList_ = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50 } };
static const GUID IID_IImageList2_ = { 0x192B9D83, 0x50FC, 0x457B, { 0x90, 0xA0, 0x2B, 0x82, 0xA8, 0xB5, 0xDA, 0xE1 } };

static HRESULT il_query(IL *l, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG_;
    *out = NULL;
    if (!il((HIMAGELIST)l)) return E_INVALIDARG_;
    if (!iid || !memcmp(iid, &IID_IUnknown_, sizeof(GUID)) || !memcmp(iid, &IID_IImageList_, sizeof(GUID)) ||
        !memcmp(iid, &IID_IImageList2_, sizeof(GUID))) {
        InterlockedIncrement(&l->refs);
        *out = l;
        return S_OK_;
    }
    return E_NOINTERFACE_;
}

static HRESULT WINAPI m_QueryInterface(IL *l, const GUID *iid, void **out) { return il_query(l, iid, out); }
static ULONG WINAPI m_AddRef(IL *l) { return (ULONG)InterlockedIncrement(&l->refs); }
static ULONG WINAPI m_Release(IL *l)
{
    LONG n = InterlockedDecrement(&l->refs);
    if (n <= 0) ImageList_Destroy((HIMAGELIST)l);
    return n > 0 ? (ULONG)n : 0;
}
static HRESULT WINAPI m_Add(IL *l, HBITMAP bm, HBITMAP mask, int *i)
{
    int r = ImageList_Add((HIMAGELIST)l, bm, mask);
    if (i) *i = r;
    return OK(r >= 0);
}
static HRESULT WINAPI m_ReplaceIcon(IL *l, int i, HICON icon, int *out)
{
    int r = ImageList_ReplaceIcon((HIMAGELIST)l, i, icon);
    if (out) *out = r;
    return OK(r >= 0);
}
static HRESULT WINAPI m_SetOverlayImage(IL *l, int i, int ov) { return OK(ImageList_SetOverlayImage((HIMAGELIST)l, i, ov)); }
static HRESULT WINAPI m_Replace(IL *l, int i, HBITMAP bm, HBITMAP mask) { return OK(ImageList_Replace((HIMAGELIST)l, i, bm, mask)); }
static HRESULT WINAPI m_AddMasked(IL *l, HBITMAP bm, COLORREF key, int *i)
{
    int r = ImageList_AddMasked((HIMAGELIST)l, bm, key);
    if (i) *i = r;
    return OK(r >= 0);
}
static HRESULT WINAPI m_Draw(IL *l, IMAGELISTDRAWPARAMS *p)
{
    if (!p) return E_INVALIDARG_;
    p->himl = (HIMAGELIST)l;
    return OK(ImageList_DrawIndirect(p));
}
static HRESULT WINAPI m_Remove(IL *l, int i) { return OK(ImageList_Remove((HIMAGELIST)l, i)); }
static HRESULT WINAPI m_GetIcon(IL *l, int i, UINT flags, HICON *out)
{
    if (!out) return E_INVALIDARG_;
    *out = ImageList_GetIcon((HIMAGELIST)l, i, flags);
    return OK(*out);
}
static HRESULT WINAPI m_GetImageInfo(IL *l, int i, IMAGEINFO *info) { return OK(ImageList_GetImageInfo((HIMAGELIST)l, i, info)); }
static HRESULT WINAPI m_Copy(IL *l, int di, IL *src, int si, UINT flags)
{
    return OK(ImageList_Copy((HIMAGELIST)l, di, (HIMAGELIST)src, si, flags));
}
static HRESULT WINAPI m_Merge(IL *l, int i1, IL *b, int i2, int dx, int dy, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG_;
    *out = NULL;
    IL *n = il(ImageList_Merge((HIMAGELIST)l, i1, (HIMAGELIST)b, i2, dx, dy));
    if (!n) return E_FAIL_;
    HRESULT hr = il_query(n, iid, out);
    m_Release(n);
    return hr;
}
static HRESULT WINAPI m_Clone(IL *l, const GUID *iid, void **out)
{
    if (!out) return E_INVALIDARG_;
    *out = NULL;
    IL *n = il(ImageList_Duplicate((HIMAGELIST)l));
    if (!n) return E_FAIL_;
    HRESULT hr = il_query(n, iid, out);
    m_Release(n);
    return hr;
}
static HRESULT WINAPI m_GetImageRect(IL *l, int i, RECT *r)
{
    IMAGEINFO info;
    if (!r || !ImageList_GetImageInfo((HIMAGELIST)l, i, &info)) return E_FAIL_;
    *r = info.rcImage;
    return S_OK_;
}
static HRESULT WINAPI m_GetIconSize(IL *l, int *cx, int *cy) { return OK(ImageList_GetIconSize((HIMAGELIST)l, cx, cy)); }
static HRESULT WINAPI m_SetIconSize(IL *l, int cx, int cy) { return OK(ImageList_SetIconSize((HIMAGELIST)l, cx, cy)); }
static HRESULT WINAPI m_GetImageCount(IL *l, int *n)
{
    if (!n) return E_INVALIDARG_;
    *n = ImageList_GetImageCount((HIMAGELIST)l);
    return S_OK_;
}
static HRESULT WINAPI m_SetImageCount(IL *l, UINT n) { return OK(ImageList_SetImageCount((HIMAGELIST)l, n)); }
static HRESULT WINAPI m_SetBkColor(IL *l, COLORREF c, COLORREF *old)
{
    COLORREF o = ImageList_SetBkColor((HIMAGELIST)l, c);
    if (old) *old = o;
    return S_OK_;
}
static HRESULT WINAPI m_GetBkColor(IL *l, COLORREF *c)
{
    if (!c) return E_INVALIDARG_;
    *c = ImageList_GetBkColor((HIMAGELIST)l);
    return S_OK_;
}
static HRESULT WINAPI m_BeginDrag(IL *l, int i, int dx, int dy) { return OK(ImageList_BeginDrag((HIMAGELIST)l, i, dx, dy)); }
static HRESULT WINAPI m_EndDrag(IL *l) { (void)l; ImageList_EndDrag(); return S_OK_; }
static HRESULT WINAPI m_DragEnter(IL *l, HWND h, int x, int y) { (void)l; return OK(ImageList_DragEnter(h, x, y)); }
static HRESULT WINAPI m_DragLeave(IL *l, HWND h) { (void)l; return OK(ImageList_DragLeave(h)); }
static HRESULT WINAPI m_DragMove(IL *l, int x, int y) { (void)l; return OK(ImageList_DragMove(x, y)); }
static HRESULT WINAPI m_SetDragCursorImage(IL *l, IL *src, int i, int dx, int dy)
{
    (void)l;
    return OK(ImageList_SetDragCursorImage((HIMAGELIST)src, i, dx, dy));
}
static HRESULT WINAPI m_DragShowNolock(IL *l, BOOL show) { (void)l; return OK(ImageList_DragShowNolock(show)); }
static HRESULT WINAPI m_GetDragImage(IL *l, POINT *p, POINT *hot, const GUID *iid, void **out)
{
    (void)l; (void)p; (void)hot; (void)iid;
    if (out) *out = NULL;
    return E_FAIL_;
}
static HRESULT WINAPI m_GetItemFlags(IL *l, int i, DWORD *flags)
{
    (void)l; (void)i;
    if (!flags) return E_INVALIDARG_;
    *flags = 0;
    return S_OK_;
}
static HRESULT WINAPI m_GetOverlayImage(IL *l, int ov, int *i)
{
    IL *x = il((HIMAGELIST)l);
    if (!x || !i || ov < 1 || ov > 15) return E_INVALIDARG_;
    *i = x->overlay[ov];
    return S_OK_;
}
/* IImageList2 */
static HRESULT WINAPI m_Resize(IL *l, int cx, int cy) { return OK(ImageList_SetIconSize((HIMAGELIST)l, cx, cy)); }
static HRESULT WINAPI m_GetOriginalSize(IL *l, int i, DWORD flags, int *cx, int *cy)
{
    (void)i; (void)flags;
    return OK(ImageList_GetIconSize((HIMAGELIST)l, cx, cy));
}
static HRESULT WINAPI m_SetOriginalSize(IL *l, int i, int cx, int cy) { (void)l; (void)i; (void)cx; (void)cy; return S_OK_; }
static HRESULT WINAPI m_SetCallback(IL *l, void *cb) { (void)l; (void)cb; return E_NOTIMPL_; }
static HRESULT WINAPI m_GetCallback(IL *l, const GUID *iid, void **out)
{
    (void)l; (void)iid;
    if (out) *out = NULL;
    return E_NOTIMPL_;
}
static HRESULT WINAPI m_ForceImagePresent(IL *l, int i, DWORD flags) { (void)l; (void)i; (void)flags; return S_OK_; }
static HRESULT WINAPI m_DiscardImages(IL *l, int a, int b, DWORD flags) { (void)l; (void)a; (void)b; (void)flags; return S_OK_; }
static HRESULT WINAPI m_PreloadImages(IL *l, IMAGELISTDRAWPARAMS *p) { (void)l; (void)p; return S_OK_; }
static HRESULT WINAPI m_GetStatistics(IL *l, void *stats) { (void)l; (void)stats; return E_NOTIMPL_; }
static HRESULT WINAPI m_Initialize(IL *l, int cx, int cy, UINT flags, int initial, int grow)
{
    (void)initial;
    IL *x = il((HIMAGELIST)l);
    if (!x || cx <= 0 || cy <= 0) return E_INVALIDARG_;
    ImageList_SetIconSize((HIMAGELIST)x, cx, cy);
    x->flags = flags;
    x->grow = grow > 0 ? grow : 4;
    return S_OK_;
}
static HRESULT WINAPI m_Replace2(IL *l, int i, HBITMAP bm, HBITMAP mask, void *unk, DWORD flags)
{
    (void)unk; (void)flags;
    return OK(ImageList_Replace((HIMAGELIST)l, i, bm, mask));
}
static HRESULT WINAPI m_ReplaceFromImageList(IL *l, int i, IL *src, int si, void *unk, DWORD flags)
{
    (void)unk; (void)flags;
    return OK(ImageList_Copy((HIMAGELIST)l, i, (HIMAGELIST)src, si, 0));
}

static const void *const g_il_vtbl[] = {
    m_QueryInterface, m_AddRef, m_Release,
    m_Add, m_ReplaceIcon, m_SetOverlayImage, m_Replace, m_AddMasked, m_Draw, m_Remove, m_GetIcon,
    m_GetImageInfo, m_Copy, m_Merge, m_Clone, m_GetImageRect, m_GetIconSize, m_SetIconSize,
    m_GetImageCount, m_SetImageCount, m_SetBkColor, m_GetBkColor, m_BeginDrag, m_EndDrag,
    m_DragEnter, m_DragLeave, m_DragMove, m_SetDragCursorImage, m_DragShowNolock, m_GetDragImage,
    m_GetItemFlags, m_GetOverlayImage,
    m_Resize, m_GetOriginalSize, m_SetOriginalSize, m_SetCallback, m_GetCallback, m_ForceImagePresent,
    m_DiscardImages, m_PreloadImages, m_GetStatistics, m_Initialize, m_Replace2, m_ReplaceFromImageList,
};

/* CLSID_ImageList: a new empty list (IImageList2::Initialize sizes it) */
CC HRESULT WINAPI ImageList_CoCreateInstance(const void *clsid, void *outer, const void *iid, void **out)
{
    (void)clsid;
    if (!out) return E_INVALIDARG_;
    *out = NULL;
    if (outer) return (HRESULT)0x80040110L;                     /* CLASS_E_NOAGGREGATION */
    IL *l = il(ImageList_Create(16, 16, ILC_COLOR32, 4, 4));
    if (!l) return (HRESULT)0x8007000EL;                        /* E_OUTOFMEMORY */
    HRESULT hr = il_query(l, iid, out);
    m_Release(l);
    return hr;
}

CC HRESULT WINAPI HIMAGELIST_QueryInterface(HIMAGELIST h, REFIID iid, void **out) { return il_query((IL *)h, iid, out); }
