/* winspool.h — the print spooler API NovaOS's winspool.drv implements */
#pragma once
#include <windows.h>
_NOVA_BEGIN

#ifdef NOVA_BUILD_WINSPOOL
#define SPLAPI_ __declspec(dllexport) __stdcall
#else
#define SPLAPI_ __declspec(dllimport) __stdcall
#endif

typedef struct { LPWSTR pDescription, pName, pComment; DWORD Flags; } PRINTER_INFO_1W, *PPRINTER_INFO_1W, *LPPRINTER_INFO_1W;
typedef struct { LPSTR pDescription, pName, pComment; DWORD Flags; } PRINTER_INFO_1A, *PPRINTER_INFO_1A, *LPPRINTER_INFO_1A;
typedef struct {
    LPWSTR pServerName, pPrinterName, pShareName, pPortName, pDriverName, pComment, pLocation;
    DEVMODEW *pDevMode;
    LPWSTR pSepFile, pPrintProcessor, pDatatype, pParameters;
    PVOID pSecurityDescriptor;
    DWORD Attributes, Priority, DefaultPriority, StartTime, UntilTime, Status, cJobs, AveragePPM;
} PRINTER_INFO_2W, *PPRINTER_INFO_2W, *LPPRINTER_INFO_2W;
typedef struct {
    LPSTR pServerName, pPrinterName, pShareName, pPortName, pDriverName, pComment, pLocation;
    DEVMODEA *pDevMode;
    LPSTR pSepFile, pPrintProcessor, pDatatype, pParameters;
    PVOID pSecurityDescriptor;
    DWORD Attributes, Priority, DefaultPriority, StartTime, UntilTime, Status, cJobs, AveragePPM;
} PRINTER_INFO_2A, *PPRINTER_INFO_2A, *LPPRINTER_INFO_2A;
typedef struct { LPWSTR pPrinterName, pServerName; DWORD Attributes; } PRINTER_INFO_4W, *PPRINTER_INFO_4W, *LPPRINTER_INFO_4W;
typedef struct { LPSTR pPrinterName, pServerName; DWORD Attributes; } PRINTER_INFO_4A, *PPRINTER_INFO_4A, *LPPRINTER_INFO_4A;
typedef struct { LPWSTR pPrinterName, pPortName; DWORD Attributes, DeviceNotSelectedTimeout, TransmissionRetryTimeout; } PRINTER_INFO_5W, *PPRINTER_INFO_5W, *LPPRINTER_INFO_5W;
typedef struct { LPSTR pPrinterName, pPortName; DWORD Attributes, DeviceNotSelectedTimeout, TransmissionRetryTimeout; } PRINTER_INFO_5A, *PPRINTER_INFO_5A, *LPPRINTER_INFO_5A;

typedef struct { LPWSTR pDocName, pOutputFile, pDatatype; } DOC_INFO_1W, *PDOC_INFO_1W, *LPDOC_INFO_1W;
typedef struct { LPSTR pDocName, pOutputFile, pDatatype; } DOC_INFO_1A, *PDOC_INFO_1A, *LPDOC_INFO_1A;
typedef struct { LPWSTR pDatatype; DEVMODEW *pDevMode; ACCESS_MASK DesiredAccess; } PRINTER_DEFAULTSW, *PPRINTER_DEFAULTSW, *LPPRINTER_DEFAULTSW;
typedef struct { LPSTR pDatatype; DEVMODEA *pDevMode; ACCESS_MASK DesiredAccess; } PRINTER_DEFAULTSA, *PPRINTER_DEFAULTSA, *LPPRINTER_DEFAULTSA;

typedef struct {
    DWORD JobId;
    LPWSTR pPrinterName, pMachineName, pUserName, pDocument, pDatatype, pStatus;
    DWORD Status, Priority, Position, TotalPages, PagesPrinted;
    SYSTEMTIME Submitted;
} JOB_INFO_1W, *PJOB_INFO_1W, *LPJOB_INFO_1W;

typedef struct { LPWSTR pName; } DRIVER_INFO_1W, *PDRIVER_INFO_1W;
typedef struct { DWORD cVersion; LPWSTR pName, pEnvironment, pDriverPath, pDataFile, pConfigFile; } DRIVER_INFO_2W, *PDRIVER_INFO_2W;
typedef struct { LPWSTR pName; } PORT_INFO_1W, *PPORT_INFO_1W;
typedef struct { LPWSTR pPortName, pMonitorName, pDescription; DWORD fPortType, Reserved; } PORT_INFO_2W, *PPORT_INFO_2W;
typedef struct { DWORD Flags; LPWSTR pName; struct { LONG cx, cy; } Size; struct { LONG left, top, right, bottom; } ImageableArea; } FORM_INFO_1W, *PFORM_INFO_1W;

#define PRINTER_ENUM_DEFAULT     0x00000001
#define PRINTER_ENUM_LOCAL       0x00000002
#define PRINTER_ENUM_CONNECTIONS 0x00000004
#define PRINTER_ENUM_NAME        0x00000008
#define PRINTER_ENUM_REMOTE      0x00000010
#define PRINTER_ENUM_SHARED      0x00000020
#define PRINTER_ENUM_NETWORK     0x00000040
#define PRINTER_ENUM_ICON8       0x00800000

#define PRINTER_ATTRIBUTE_QUEUED           0x00000001
#define PRINTER_ATTRIBUTE_DIRECT           0x00000002
#define PRINTER_ATTRIBUTE_DEFAULT          0x00000004
#define PRINTER_ATTRIBUTE_SHARED           0x00000008
#define PRINTER_ATTRIBUTE_NETWORK          0x00000010
#define PRINTER_ATTRIBUTE_LOCAL            0x00000040
#define PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS  0x00000100
#define PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST 0x00000200
#define PRINTER_ATTRIBUTE_ENABLE_BIDI      0x00000800

#define PRINTER_STATUS_PAUSED   0x00000001
#define PRINTER_STATUS_ERROR    0x00000002
#define PRINTER_STATUS_OFFLINE  0x00000080
#define PRINTER_STATUS_PRINTING 0x00000400

#define PRINTER_ACCESS_ADMINISTER 0x00000004
#define PRINTER_ACCESS_USE        0x00000008
#define PRINTER_ALL_ACCESS        0x000F000C

#define PRINTER_CONTROL_PAUSE  1
#define PRINTER_CONTROL_RESUME 2
#define PRINTER_CONTROL_PURGE  3

#define JOB_CONTROL_PAUSE   1
#define JOB_CONTROL_RESUME  2
#define JOB_CONTROL_CANCEL  3
#define JOB_CONTROL_RESTART 4
#define JOB_CONTROL_DELETE  5

#define JOB_STATUS_PAUSED     0x00000001
#define JOB_STATUS_ERROR      0x00000002
#define JOB_STATUS_DELETING   0x00000004
#define JOB_STATUS_SPOOLING   0x00000008
#define JOB_STATUS_PRINTING   0x00000010
#define JOB_STATUS_OFFLINE    0x00000020
#define JOB_STATUS_PRINTED    0x00000080
#define JOB_STATUS_DELETED    0x00000100

/* DocumentProperties modes */
#define DM_UPDATE      1
#define DM_COPY        2
#define DM_PROMPT      4
#define DM_MODIFY      8
#define DM_IN_BUFFER   DM_MODIFY
#define DM_IN_PROMPT   DM_PROMPT
#define DM_OUT_BUFFER  DM_COPY

/* DEVMODE fields (the printer's) */
#define DM_ORIENTATION   0x00000001
#define DM_PAPERSIZE     0x00000002
#define DM_PAPERLENGTH   0x00000004
#define DM_PAPERWIDTH    0x00000008
#define DM_SCALE         0x00000010
#define DM_COPIES        0x00000100
#define DM_DEFAULTSOURCE 0x00000200
#define DM_PRINTQUALITY  0x00000400
#define DM_COLOR         0x00000800
#define DM_DUPLEX        0x00001000
#define DM_YRESOLUTION   0x00002000
#define DM_COLLATE       0x00008000
#define DM_FORMNAME      0x00010000
#define DMORIENT_PORTRAIT  1
#define DMORIENT_LANDSCAPE 2
#define DMPAPER_LETTER  1
#define DMPAPER_LEGAL   5
#define DMPAPER_A3      8
#define DMPAPER_A4      9
#define DMPAPER_A5      11
#define DMCOLOR_MONOCHROME 1
#define DMCOLOR_COLOR      2
#define DMDUP_SIMPLEX 1
#define DMRES_HIGH (-4)
#define DM_SPECVERSION 0x0401

/* DeviceCapabilities */
#define DC_FIELDS       1
#define DC_PAPERS       2
#define DC_PAPERSIZE    3
#define DC_MINEXTENT    4
#define DC_MAXEXTENT    5
#define DC_BINS         6
#define DC_DUPLEX       7
#define DC_SIZE         8
#define DC_EXTRA        9
#define DC_VERSION      10
#define DC_DRIVER       11
#define DC_BINNAMES     12
#define DC_ENUMRESOLUTIONS 13
#define DC_FILEDEPENDENCIES 14
#define DC_TRUETYPE     15
#define DC_PAPERNAMES   16
#define DC_ORIENTATION  17
#define DC_COPIES       18
#define DC_COLLATE      22
#define DC_COLORDEVICE  32
#define DC_NUP          33
#define DC_MEDIATYPES   35

#ifndef ERROR_INVALID_PRINTER_NAME
#define ERROR_INVALID_PRINTER_NAME 1801
#endif
#ifndef ERROR_PRINTER_ALREADY_EXISTS
#define ERROR_PRINTER_ALREADY_EXISTS 1802
#endif
#ifndef ERROR_INVALID_DATATYPE
#define ERROR_INVALID_DATATYPE 1804
#endif
#ifndef ERROR_PRINT_CANCELLED
#define ERROR_PRINT_CANCELLED 63
#endif
#ifndef ERROR_SPOOL_FILE_NOT_FOUND
#define ERROR_SPOOL_FILE_NOT_FOUND 3002
#endif
#ifndef ERROR_UNKNOWN_PRINTER_DRIVER
#define ERROR_UNKNOWN_PRINTER_DRIVER 1797
#endif
#ifndef ERROR_CANCELLED
#define ERROR_CANCELLED 1223
#endif
#ifndef RPC_S_SERVER_UNAVAILABLE
#define RPC_S_SERVER_UNAVAILABLE 1722
#endif

BOOL SPLAPI_ EnumPrintersW(DWORD flags, LPWSTR name, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned);
BOOL SPLAPI_ EnumPrintersA(DWORD flags, LPSTR name, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned);
BOOL SPLAPI_ OpenPrinterW(LPWSTR name, LPHANDLE h, LPPRINTER_DEFAULTSW defaults);
BOOL SPLAPI_ OpenPrinterA(LPSTR name, LPHANDLE h, LPPRINTER_DEFAULTSA defaults);
BOOL SPLAPI_ ClosePrinter(HANDLE h);
BOOL SPLAPI_ GetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed);
BOOL SPLAPI_ GetPrinterA(HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed);
BOOL SPLAPI_ SetPrinterW(HANDLE h, DWORD level, LPBYTE buf, DWORD cmd);
HANDLE SPLAPI_ AddPrinterW(LPWSTR server, DWORD level, LPBYTE buf);
BOOL SPLAPI_ DeletePrinter(HANDLE h);
DWORD SPLAPI_ StartDocPrinterW(HANDLE h, DWORD level, LPBYTE info);
DWORD SPLAPI_ StartDocPrinterA(HANDLE h, DWORD level, LPBYTE info);
BOOL SPLAPI_ StartPagePrinter(HANDLE h);
BOOL SPLAPI_ EndPagePrinter(HANDLE h);
BOOL SPLAPI_ WritePrinter(HANDLE h, LPVOID buf, DWORD cb, LPDWORD written);
BOOL SPLAPI_ EndDocPrinter(HANDLE h);
BOOL SPLAPI_ AbortPrinter(HANDLE h);
BOOL SPLAPI_ EnumJobsW(HANDLE h, DWORD first, DWORD n, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned);
BOOL SPLAPI_ GetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed);
BOOL SPLAPI_ SetJobW(HANDLE h, DWORD id, DWORD level, LPBYTE buf, DWORD cmd);
BOOL SPLAPI_ GetDefaultPrinterW(LPWSTR buf, LPDWORD cch);
BOOL SPLAPI_ GetDefaultPrinterA(LPSTR buf, LPDWORD cch);
BOOL SPLAPI_ SetDefaultPrinterW(LPCWSTR name);
BOOL SPLAPI_ SetDefaultPrinterA(LPCSTR name);
LONG SPLAPI_ DocumentPropertiesW(HWND w, HANDLE h, LPWSTR name, DEVMODEW *out, DEVMODEW *in, DWORD mode);
LONG SPLAPI_ DocumentPropertiesA(HWND w, HANDLE h, LPSTR name, DEVMODEA *out, DEVMODEA *in, DWORD mode);
int SPLAPI_ DeviceCapabilitiesW(LPCWSTR dev, LPCWSTR port, WORD cap, LPWSTR out, const DEVMODEW *mode);
int SPLAPI_ DeviceCapabilitiesA(LPCSTR dev, LPCSTR port, WORD cap, LPSTR out, const DEVMODEA *mode);
BOOL SPLAPI_ EnumPrinterDriversW(LPWSTR srv, LPWSTR env, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned);
BOOL SPLAPI_ EnumPortsW(LPWSTR srv, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed, LPDWORD returned);

_NOVA_END
