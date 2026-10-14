/* wevtapi.h — what wevtapi.c and evtx.c share (not exported) */
#pragma once
#include <windows.h>
#include <novaevt.h>

#define WEVTAPI __declspec(dllexport)

#define ERROR_EVT_INVALID_QUERY                15001
#define ERROR_EVT_CHANNEL_NOT_FOUND            15007
#define ERROR_EVT_PUBLISHER_METADATA_NOT_FOUND 15002
#define ERROR_EVT_MESSAGE_NOT_FOUND            15027
#ifndef ERROR_NO_MORE_ITEMS
#define ERROR_NO_MORE_ITEMS 259
#endif
#ifndef ERROR_NOT_FOUND
#define ERROR_NOT_FOUND 1168
#endif

/* EVT_VARIANT: a value, a count and a type */
typedef struct { ULONGLONG v; DWORD count, type; } EvtVariant;
enum { EvtVarTypeNull = 0, EvtVarTypeString = 1, EvtVarTypeByte = 4, EvtVarTypeUInt16 = 6, EvtVarTypeUInt32 = 8,
       EvtVarTypeUInt64 = 10, EvtVarTypeBoolean = 13, EvtVarTypeFileTime = 17, EvtVarTypeSid = 19,
       EvtVarTypeHexInt64 = 21 };

typedef DWORD (WINAPI *EVT_SUBSCRIBE_CALLBACK)(DWORD action, void *ctx, HANDLE event);

/* A growing UTF-16 string */
typedef struct { WCHAR *p; int len, cap; BOOL oom; } Sb;
void sb_put(Sb *b, const WCHAR *s, int n);
void sb_esc(Sb *b, const WCHAR *s);
void sb_num(Sb *b, ULONGLONG v, int hex, int width);

void *evt_alloc(size_t n);
void evt_free(void *p);

/* An event: its channel and a copy of its EVENTLOGRECORD */
typedef struct {
    DWORD magic;
    WCHAR channel[NOVA_EVT_LOG_CHARS];
    EVENTLOGRECORD rec;             /* (followed by the rest of it) */
} Event;

typedef struct { DWORD magic; WCHAR channel[NOVA_EVT_LOG_CHARS]; DWORD record; } Bookmark;
typedef struct { DWORD magic; WCHAR *names; DWORD pos; } Enum;     /* names: "a|b|" */

WCHAR *evt_message(const Event *e);                      /* (heap) or NULL */
void evt_level_name(const Event *e, WCHAR *out, int cap);
void evt_task_name(const Event *e, WCHAR *out, int cap);
void evt_xml(const Event *e, Sb *b, BOOL rendering);

/* evtx.c: @events as a .evtx file */
BOOL evtx_write(const WCHAR *path, const Event *const *events, DWORD n);

WEVTAPI BOOL WINAPI EvtExportLog(HANDLE session, LPCWSTR path, LPCWSTR query, LPCWSTR target, DWORD flags);
