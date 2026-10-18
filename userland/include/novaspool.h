/*
 * novaspool.h — how winspool.drv talks to the print spooler
 *
 * The Print Spooler service (userland/programs/spoolsv.c) keeps the print
 * queue.  winspool.drv, in each program, reaches it over the named pipe
 * \\.\pipe\spoolss (Windows' own spooler endpoint, which Windows speaks
 * MS-RPRN RPC over; NovaOS sends the fixed messages below instead), one
 * request and one reply per TransactNamedPipe:
 *
 *   SPL_START  a job begins on a printer: the spooler numbers it and names
 *              its spool file (spool\PRINTERS\NNNNN.SPL), which the program
 *              then writes itself (WritePrinter)
 *   SPL_PAGE   a page of it is written (the queue's page count)
 *   SPL_END    the job is complete: the spooler sends it to the printer's
 *              port and deletes it
 *   SPL_ABORT  the job is dropped
 *   SPL_ENUM   the jobs waiting on a printer (EnumJobs)
 *   SPL_SETJOB pause, resume or cancel a job (SetJob)
 *
 * The printers themselves are registry keys (NOVA_SPL_PRINTERS) that
 * winspool reads and writes directly; the spooler reads a printer's key
 * when it prints a job.  The Microsoft Print to PDF printer is made at
 * boot (kernel/um/um_registry.c).
 */
#pragma once
#include <windows.h>

#define NOVA_SPL_PIPE      L"\\\\.\\pipe\\spoolss"
#define NOVA_SPL_DIR       L"C:\\Windows\\System32\\spool\\PRINTERS"
#define NOVA_SPL_PRINTERS  L"SYSTEM\\CurrentControlSet\\Control\\Print\\Printers"
#define NOVA_SPL_PDF       L"Microsoft Print to PDF"
#define NOVA_SPL_PDF_DRIVER L"Microsoft Print To PDF"
#define NOVA_SPL_NAME      128
#define NOVA_SPL_MAX_JOBS  32

enum { SPL_START = 1, SPL_PAGE, SPL_END, SPL_ABORT, SPL_ENUM, SPL_SETJOB };

typedef struct {
    DWORD op;
    DWORD job;                          /* SPL_PAGE...SPL_SETJOB */
    DWORD arg;                          /* SPL_END: the job's size in bytes; SPL_SETJOB: JOB_CONTROL_* */
    WCHAR printer[NOVA_SPL_NAME];
    WCHAR document[NOVA_SPL_NAME];
    WCHAR output[MAX_PATH];             /* DOC_INFO_1's pOutputFile (a file port's destination) */
    WCHAR datatype[32];
    WCHAR user[64];
} SplRequest;

/* A job as the spooler keeps it (and as its shadow file, NNNNN.SHD, holds it) */
typedef struct {
    DWORD id;
    DWORD status;                       /* JOB_STATUS_* */
    DWORD pages;
    DWORD size;
    SYSTEMTIME submitted;
    WCHAR printer[NOVA_SPL_NAME];
    WCHAR document[NOVA_SPL_NAME];
    WCHAR output[MAX_PATH];
    WCHAR datatype[32];
    WCHAR user[64];
    DWORD complete;                     /* SPL_END came: ready to print */
} SplJob;

typedef struct {
    DWORD error;                        /* ERROR_SUCCESS or a Win32 error */
    DWORD job;                          /* SPL_START: the new job's number */
    WCHAR path[MAX_PATH];               /* SPL_START: its spool file */
    DWORD count;                        /* SPL_ENUM: jobs[] filled */
    SplJob jobs[NOVA_SPL_MAX_JOBS];
} SplReply;
