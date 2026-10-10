/*
 * inf.h — setupapi's INF reader, shared by inf.c and driver.c
 */
#pragma once
#include <windows.h>
#include <string.h>

#ifndef ERROR_READ_FAULT
#define ERROR_READ_FAULT 30
#endif
#ifndef ERROR_NOT_FOUND
#define ERROR_NOT_FOUND 1168
#endif

#define INF_MAGIC 0x31464E49u                   /* "INF1" */

typedef struct {
    DWORD magic;
    WCHAR *text;                                /* the whole file, UTF-16, NUL-terminated */
    WCHAR path[MAX_PATH];
} Inf;

/* Open @path (UTF-16 with a BOM, UTF-8 with one, or the ANSI code page);
 * NULL with the last error set */
Inf *inf_open(const WCHAR *path);
void inf_close(Inf *inf);
/* The value of @key in @section (sections of one name are one), quotes
 * taken off and %strkey% replaced from [Strings]; FALSE: no such line */
BOOL inf_get(const Inf *inf, const WCHAR *section, const WCHAR *key, WCHAR *out, int cap);
/* Call @fn for each line of @section: its key (the whole line when it has
 * no '=') and its value (as inf_get gives it); stops when @fn returns FALSE */
typedef BOOL (*inf_line_fn)(const WCHAR *key, const WCHAR *value, void *ctx);
void inf_lines(const Inf *inf, const WCHAR *section, inf_line_fn fn, void *ctx);
/* Field @n (0-based) of a comma-separated value */
void inf_field(const WCHAR *value, int n, WCHAR *out, int cap);

/* (inf.c's exports, used by driver.c) */
__declspec(dllexport) HANDLE WINAPI SetupOpenInfFileW(LPCWSTR name, LPCWSTR cls, DWORD style, PUINT err_line);
__declspec(dllexport) void WINAPI SetupCloseInfFile(HANDLE h);
