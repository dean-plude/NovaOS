/* Decode PNG files with the kernel's own decoder (kernel/gdi/png.c) and
 * print "<width>x<height>" for each, or "FAIL <path>". */
#include "../../kernel/gdi/png.c"
#include <stdio.h>
#include <stdlib.h>

void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void *kzalloc(size_t n) { return calloc(1, n); }

int main(int argc, char **argv)
{
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) { printf("FAIL %s\n", argv[i]); bad++; continue; }
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *data = malloc((size_t)len);
        if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) { printf("FAIL %s\n", argv[i]); bad++; fclose(f); continue; }
        fclose(f);
        UINT32 *px = NULL;
        int w = 0, h = 0;
        if (!PngIsPng(data, (size_t)len) || !PngDecode(data, (size_t)len, &px, &w, &h)) {
            printf("FAIL %s\n", argv[i]);
            bad++;
        } else {
            printf("%dx%d %s\n", w, h, argv[i]);
            free(px);
        }
        free(data);
    }
    return bad ? 1 : 0;
}
