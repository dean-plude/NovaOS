/* dosrw.h — DOSRW, the DOS program the drive C: tests run (dostest on
 * NovaOS, tools/msdos/vdm_test.c in CI): it reads IN.TXT in its current
 * directory, writes OUT.TXT there ("DOS read: " and what it read), says
 * "DOSRW: wrote OUT.TXT" and ends with exit code 42 (1: no IN.TXT, 2:
 * OUT.TXT could not be made).  As a .COM, and as an .EXE (an MZ header in
 * front of the same code: CS:IP and SS:SP are FFF0h:..., which DOS
 * relocates to the program's PSP, so it runs as the .COM does).
 *
 *         org 100h
 *         mov dx,in_name
 *         mov ax,3D00h            ; open IN.TXT to read
 *         int 21h
 *         jc fail1
 *         mov bx,ax
 *         mov ah,3Fh              ; read up to 256 bytes
 *         mov cx,100h
 *         mov dx,buf
 *         int 21h
 *         jc fail1
 *         mov [len],ax
 *         mov ah,3Eh              ; close
 *         int 21h
 *         mov ah,3Ch              ; create OUT.TXT
 *         xor cx,cx
 *         mov dx,out_name
 *         int 21h
 *         jc fail2
 *         mov bx,ax
 *         mov ah,40h              ; "DOS read: "
 *         mov cx,prefix_end-prefix
 *         mov dx,prefix
 *         int 21h
 *         mov ah,40h              ; and what it read
 *         mov cx,[len]
 *         mov dx,buf
 *         int 21h
 *         mov ah,3Eh
 *         int 21h
 *         mov ah,9
 *         mov dx,msg
 *         int 21h
 *         mov ax,4C2Ah            ; exit code 42
 *         int 21h
 * fail1:  mov ax,4C01h
 *         int 21h
 * fail2:  mov ax,4C02h
 *         int 21h
 * in_name db "IN.TXT",0
 * out_name db "OUT.TXT",0
 * prefix  db "DOS read: "
 * prefix_end:
 * msg     db "DOSRW: wrote OUT.TXT",13,10,"$"
 * len     dw 0
 * buf:
 */
#ifndef NOVA_DOSRW_H
#define NOVA_DOSRW_H

#include <string.h>

static const unsigned char dosrw_com[141] = {
    0xBA, 0x5B, 0x01, 0xB8, 0x00, 0x3D, 0xCD, 0x21, 0x72, 0x47, 0x89, 0xC3, 0xB4, 0x3F, 0xB9, 0x00,
    0x01, 0xBA, 0x8D, 0x01, 0xCD, 0x21, 0x72, 0x39, 0xA3, 0x8B, 0x01, 0xB4, 0x3E, 0xCD, 0x21, 0xB4,
    0x3C, 0x31, 0xC9, 0xBA, 0x62, 0x01, 0xCD, 0x21, 0x72, 0x2C, 0x89, 0xC3, 0xB4, 0x40, 0xB9, 0x0A,
    0x00, 0xBA, 0x6A, 0x01, 0xCD, 0x21, 0xB4, 0x40, 0x8B, 0x0E, 0x8B, 0x01, 0xBA, 0x8D, 0x01, 0xCD,
    0x21, 0xB4, 0x3E, 0xCD, 0x21, 0xB4, 0x09, 0xBA, 0x74, 0x01, 0xCD, 0x21, 0xB8, 0x2A, 0x4C, 0xCD,
    0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0xB8, 0x02, 0x4C, 0xCD, 0x21, 0x49, 0x4E, 0x2E, 0x54, 0x58,
    0x54, 0x00, 0x4F, 0x55, 0x54, 0x2E, 0x54, 0x58, 0x54, 0x00, 0x44, 0x4F, 0x53, 0x20, 0x72, 0x65,
    0x61, 0x64, 0x3A, 0x20, 0x44, 0x4F, 0x53, 0x52, 0x57, 0x3A, 0x20, 0x77, 0x72, 0x6F, 0x74, 0x65,
    0x20, 0x4F, 0x55, 0x54, 0x2E, 0x54, 0x58, 0x54, 0x0D, 0x0A, 0x24, 0x00, 0x00,
};

/* DOSRW.EXE into @out (sizeof dosrw_com + 32 bytes); its size */
static unsigned dosrw_exe(unsigned char *out)
{
    static const unsigned short hdr[16] = {
        0x5A4D, (32 + sizeof(dosrw_com)) % 512, (32 + sizeof(dosrw_com) + 511) / 512,
        0,                                      /* no relocations */
        2,                                      /* a 32-byte header */
        0x0FF0, 0xFFFF,                         /* 64 KB, as a .COM gets */
        0xFFF0, 0xFFFE,                         /* SS:SP = PSP:FFFE */
        0, 0x0100, 0xFFF0,                      /* CS:IP = PSP:0100 */
        0x1C, 0, 0, 0,
    };
    for (int i = 0; i < 16; i++) { out[i * 2] = (unsigned char)hdr[i]; out[i * 2 + 1] = (unsigned char)(hdr[i] >> 8); }
    memcpy(out + 32, dosrw_com, sizeof(dosrw_com));
    return 32 + (unsigned)sizeof(dosrw_com);
}

#endif
