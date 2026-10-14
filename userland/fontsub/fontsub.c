/*
 * fontsub.dll — font subsetting for documents that embed fonts.  NovaOS
 * does not subset yet: CreateFontPackage fails, and its callers (Skia's
 * PDF output in LibreOffice) then embed the whole font instead.
 * MergeFontPackage likewise fails.
 */
#include <windows.h>

#define FONTSUB __declspec(dllexport)
#define ERR_GENERIC_ 1000               /* fontsub.h's ERR_GENERIC */

typedef void *(__cdecl *CFP_ALLOCPROC)(size_t);
typedef void *(__cdecl *CFP_REALLOCPROC)(void *, size_t);
typedef void (__cdecl *CFP_FREEPROC)(void *);

FONTSUB ULONG __cdecl CreateFontPackage(const unsigned char *src, ULONG src_size, unsigned char **dst,
                                        ULONG *dst_size, ULONG *written, USHORT flags, USHORT ttc_index,
                                        USHORT format, USHORT language, USHORT platform, USHORT encoding,
                                        const USHORT *keep, USHORT keep_count, CFP_ALLOCPROC alloc,
                                        CFP_REALLOCPROC realloc_, CFP_FREEPROC free_, void *reserved)
{
    (void)src; (void)src_size; (void)flags; (void)ttc_index; (void)format; (void)language; (void)platform;
    (void)encoding; (void)keep; (void)keep_count; (void)alloc; (void)realloc_; (void)free_; (void)reserved;
    if (dst) *dst = NULL;
    if (dst_size) *dst_size = 0;
    if (written) *written = 0;
    return ERR_GENERIC_;
}

FONTSUB ULONG __cdecl MergeFontPackage(const unsigned char *merge, ULONG merge_size, const unsigned char *delta,
                                       ULONG delta_size, unsigned char **dst, ULONG *dst_size, ULONG *written,
                                       USHORT mode, CFP_ALLOCPROC alloc, CFP_REALLOCPROC realloc_,
                                       CFP_FREEPROC free_, void *reserved)
{
    (void)merge; (void)merge_size; (void)delta; (void)delta_size; (void)mode; (void)alloc; (void)realloc_;
    (void)free_; (void)reserved;
    if (dst) *dst = NULL;
    if (dst_size) *dst_size = 0;
    if (written) *written = 0;
    return ERR_GENERIC_;
}
