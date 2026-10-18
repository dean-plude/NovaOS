/*
 * um.h — NovaOS user-mode subsystem: Windows programs in ring 3
 *
 * Runs unmodified PE32+ executables from drive C: in their own address
 * spaces.  The system DLLs they import (ntdll, kernel32, msvcrt — built
 * from userland/ and installed in C:\Windows\System32) are loaded by the
 * kernel loader like any other DLL; they reach the kernel through NT
 * system calls (um_syscall.c) with the real NT signatures.
 *
 * A process has threads, a private page table (upper half shared with the
 * kernel), a handle table (files on C:, console, synchronization objects,
 * threads, sockets, windows) and a console that carries its standard
 * input/output to a Terminal window.
 *
 * Lifetime: the spawner holds a reference until UmRelease(); the process
 * struct is freed once it has exited, its thread is off the CPU, and it is
 * released.  UmPoll() (desktop thread) does the reclaiming.
 */

#pragma once

#include "../include/types.h"
#include "../fs/ramfs.h"

typedef struct UmProcess UmProcess;
typedef struct UmConsole UmConsole;

/* CPU features for user code (SSE) and the NT system services. */
void UmInit(void);
/* Reclaim exited processes (call regularly from the desktop loop). */
void UmPoll(void);
/* The active window's process gets the foreground boost (desktop loop). */
void UmUpdateForeground(void);
/* The mouse messages programs get next come from a pen: its packet's
 * number (tablet.h; 0: the mouse).  Each goes out with it in MSG's padding
 * (after message), and user32 makes WM_POINTER* of them (desktop loop). */
void UmSetInputPen(UINT32 serial);
/* The display mode changed: WM_DISPLAYCHANGE to every program window */
void UmGuiDisplayChanged(int w, int h);
void UmGuiDpiChanged(void);            /* a monitor's DPI changed: WM_NOVA_DPI to every window */
void UmGuiDevicesChanged(UINT32 after);   /* devices came or went (events after seq @after): WM_DEVICECHANGE to every window */

/* Find a program by name: a path, or a bare name searched in @cwd,
 * C:\Programs and C:\Windows\System32 (".com", then ".exe", added if
 * missing, as cmd's PATHEXT has them), then as C:\Programs\NAME\NAME.exe. */
RamNode   *UmFindProgram(RamNode *cwd, const char *name);

/* Start @exe with command line @cmdline (UTF-8/ASCII), current directory
 * @cwd, standard handles on @con (NULL: output is discarded and input is
 * at end of file).  NULL with @err set on failure. */
UmProcess *UmSpawn(RamNode *exe, const char *cmdline, RamNode *cwd, UmConsole *con,
                   char *err, int err_cap);
/* UmSpawn as administrator without asking: the App Store's installers and
 * unpacking (Windows' Store installs as the system) */
UmProcess *UmSpawnElevated(RamNode *exe, const char *cmdline, RamNode *cwd, UmConsole *con,
                           char *err, int err_cap);
/* UmSpawn without waiting (from the desktop thread, under the desktop
 * lock): the program is mapped on a worker thread.  Poll with UmSpawnPoll
 * until it returns true (then *proc is the process, or NULL with @err set;
 * the job is gone); UmSpawnAbandon gives up on it (the process is ended
 * if it started).  NULL if out of memory. */
typedef struct UmSpawnJob UmSpawnJob;
UmSpawnJob *UmSpawnStart(RamNode *exe, const char *cmdline, RamNode *cwd, UmConsole *con);
/* ... run as administrator once UAC's prompt agrees (the Terminal's sudo,
 * as Windows 11's sudo runs a command in the same window) */
UmSpawnJob *UmSpawnStartElevated(RamNode *exe, const char *cmdline, RamNode *cwd, UmConsole *con);
bool        UmSpawnPoll(UmSpawnJob *j, UmProcess **proc, char *err, int err_cap);
void        UmSpawnAbandon(UmSpawnJob *j);
/* Start a program to run on its own (UmSpawn + UmDetach) without waiting
 * for it to load; a failure is logged.  False if out of memory. */
bool        UmSpawnDetached(RamNode *exe, const char *cmdline, RamNode *cwd);
/* ... as administrator once UAC's prompt agrees (an .msi opened from the desktop) */
bool        UmSpawnDetachedElevated(RamNode *exe, const char *cmdline, RamNode *cwd);
/* ... as a service: in no account's sign-in, so a sign-out leaves it running */
bool        UmSpawnService(RamNode *exe, const char *cmdline, RamNode *cwd);
/* Whether @f is a 16-bit DOS program: an MZ image with no Windows header
 * after its DOS one, or a .COM file that is not an MZ image */
bool        UmIsDosProgram(RamNode *f);
/* The program that runs @exe with command line @cmdline: @exe itself, or
 * for a DOS program the DOS host, as Windows starts ntvdm.exe for one
 * (C:\Windows\System32\ntvdm.exe, its command line in *@host_cmd:
 * ntvdm.exe -run "PATH" ARGUMENTS, for the caller to kfree; NULL when
 * @exe runs as itself).  Every way of starting a program goes through it. */
RamNode    *UmDosHost(RamNode *exe, const char *cmdline, char **host_cmd);
/* Whether @exe is a Windows GUI program (PE subsystem 2, 32- or 64-bit):
 * one the shell starts on its own, with no console window */
bool        UmIsGuiProgram(RamNode *exe);
/* Ask the process to end with @status (it stops at its next kernel exit). */
void       UmKill(UmProcess *p, UINT32 status);
/* True once the process has exited; *status gets its exit code and
 * @why (may be NULL) a description for abnormal ends (crash, killed). */
bool       UmHasExited(UmProcess *p, UINT32 *status, char *why, int why_cap);
/* The spawner is done with the process (kill it first if still running). */
void       UmRelease(UmProcess *p);
/* Let the process run on its own (started from the desktop, no console):
 * it is reclaimed when it exits. */
void       UmDetach(UmProcess *p);
/* Keep a process made by CreateProcess after its creator closes its
 * handles (the Terminal window of a CREATE_NEW_CONSOLE program) */
void       UmHold(UmProcess *p);
void       UmUnhold(UmProcess *p);

UINT32      UmPid(const UmProcess *p);

/* The image cache (um.c): the modules whose pages are kept for the next
 * process that loads them, their size, and the loads that took them */
void        um_image_cache_stats(UINT32 *files, UINT64 *bytes, UINT64 *reused);
const char *UmName(const UmProcess *p);

/* Process list, for tasklist: fills up to @max entries. */
typedef struct {
    UINT32 pid; char name[32]; UINT32 mem_kb; UINT32 threads; bool exited; UINT32 parent_pid;
    UINT64 cpu_time;        /* CPU time its threads ran, ended ones too (100 ns) */
    UINT64 create_time;     /* 100 ns since 1601 */
    UINT32 handles;         /* open handles */
    UINT32 commit_kb;       /* committed memory (resident or not) */
    UINT8  base_priority;   /* its priority class's base (8: NORMAL) */
} UmProcInfo;
int  UmList(UmProcInfo *out, int max);
/* The threads of process @pid (SystemProcessInformation's thread records) */
typedef struct {
    UINT32 tid; UINT64 cpu_time, start; UINT8 priority, base_priority, state;   /* state: ThreadState */
} UmThreadInfo;
int  UmThreadList(UINT32 pid, UmThreadInfo *out, int max);
/* taskkill: false if no running process has @pid. */
bool UmKillPid(UINT32 pid);
/* Fast user switching: a process runs in the sign-in of the account it
 * started under (its creator's, or the screen's when the desktop starts
 * it; services in none).  The caller's (the screen's for a kernel thread),
 * and ending every program of @rid's sign-in (how many) */
UINT32 UmCurrentSession(void);
int  UmEndSession(UINT32 rid);
void UmKillConsole(UmConsole *con, UINT32 status);   /* every program on @con */

/* -----------------------------------------------------------------------
 * Consoles: one per Terminal session that runs programs
 * ----------------------------------------------------------------------- */
/* An input record, laid out as Windows' INPUT_RECORD (20 bytes) */
typedef struct {
    UINT16 type, pad;                   /* CON_KEY_EVENT, ... */
    union {
        struct { UINT32 down; UINT16 repeat, vk, scan, ch; UINT32 ctrl; } key;
        UINT32 raw[4];
    };
} UmConInput;
#define CON_KEY_EVENT     0x0001
#define CON_MOUSE_EVENT   0x0002
#define CON_WINDOW_EVENT  0x0004
#define CON_FOCUS_EVENT   0x0010
/* dwControlKeyState */
#define CON_RIGHT_ALT     0x0001
#define CON_LEFT_ALT      0x0002
#define CON_RIGHT_CTRL    0x0004
#define CON_LEFT_CTRL     0x0008
#define CON_SHIFT         0x0010
#define CON_ENHANCED_KEY  0x0100
/* Console modes (SetConsoleMode) */
#define CON_ENABLE_PROCESSED_INPUT  0x0001
#define CON_ENABLE_LINE_INPUT       0x0002
#define CON_ENABLE_ECHO_INPUT       0x0004
#define CON_ENABLE_WINDOW_INPUT     0x0008
#define CON_ENABLE_VT_INPUT         0x0200
#define CON_ENABLE_VT_PROCESSING    0x0004   /* output */
#define CON_IN_DEFAULT   0x01E7  /* processed, line, echo, insert, quick edit, extended flags, auto position */
#define CON_OUT_DEFAULT  0x0007  /* processed, wrap at end of line, VT processing */

UmConsole *UmConsoleNew(void);
/* Program output into a libvterm emulator.  libvterm drops a character whose
 * first byte ends one write and whose others start the next, so the
 * unfinished UTF-8 character at the end of a write waits in @pend (4 bytes,
 * @pend_n of them used) for the rest. */
struct VTerm;
void       UmVtermWrite(struct VTerm *vt, char *pend, UINT8 *pend_n, const char *data, size_t len);
void       UmConsoleRelease(UmConsole *c);
/* Drain program output (Terminal side).  Returns bytes copied. */
int        UmConsoleRead(UmConsole *c, char *buf, int cap);
/* Queue keyboard input for the program (a line including "\r\n"). */
void       UmConsoleWrite(UmConsole *c, const char *data, int len);
/* End of input (Ctrl+Z): the program's next read returns 0 bytes. */
void       UmConsoleEof(UmConsole *c);
/* A program is waiting for keyboard input right now. */
bool       UmConsoleWantsInput(UmConsole *c);
/* Raw input: queue records / one key press (@ch: its character or 0) */
int        UmConsolePushInput(UmConsole *c, const UmConInput *recs, int n);
void       UmConsoleKey(UmConsole *c, UINT16 vk, UINT16 scan, UINT16 ch, UINT32 ctrl, bool down);
/* The modes the program set (CON_ENABLE_*) */
UINT32     UmConsoleInputMode(UmConsole *c);
UINT32     UmConsoleOutputMode(UmConsole *c);
/* The Terminal's size in character cells */
UINT32     UmConsoleCursorStyle(UmConsole *c);
void       UmConsoleSetSize(UmConsole *c, int cols, int rows);
/* A pseudo console: what its host writes (keys, VT key sequences) as key
 * records, echoed while the program reads lines; a new size, the screen
 * drawn again for the host */
void       UmConsolePseudoInput(UmConsole *c, const char *data, int len);
void       UmConsolePseudoResize(UmConsole *c, int cols, int rows);
/* Windows virtual-key code for a set-1 scan code (E0-prefixed: @ext) */
UINT32     UmScancodeToVk(UINT8 sc, bool ext);
/* A key went down: a program's RegisterHotKey combination?  (it is then
 * sent WM_HOTKEY and the key goes no further); @mods MOD_ALT 1, MOD_CONTROL
 * 2, MOD_SHIFT 4, MOD_WIN 8 */
bool       UmGuiHotkey(UINT8 sc, bool ext, UINT32 mods, bool repeat);

/* -----------------------------------------------------------------------
 * Hooks for the architecture code (interrupts disabled)
 * ----------------------------------------------------------------------- */
/* Only the NT services the subsystem implements are open to programs. */
bool UmSyscallAllowed(UINT64 num);
/* True for the services that run without the big kernel lock */
bool UmSyscallLockFree(UINT64 num);
/* Run service @num for the current program (interrupts enabled). */
UINT64 UmSyscall(UINT64 num, UINT64 a1, UINT64 a2, UINT64 a3, UINT64 a4);
/* On every return to user mode: ends the thread if its process was killed. */
void UmReturnToUser(void);
/* The same from an interrupt taken in user mode (@frame: InterruptFrame) */
void UmReturnToUserFrame(void *frame);
/* A CPU exception in user mode (@frame: the InterruptFrame): passed on to
 * the program's exception handlers (SEH) through its stack. */
void UmUserException(void *frame, UINT64 cr2);
/* The current user thread raised an exception it cannot handle. */
int  UmGuardFault(UINT64 va);           /* a guard page touched: 1 handled, -1/-2 raise, 0 not one */
void um_registry_add_cpus(UINT32 n);         /* the processor keys, for the CPUs started */
void um_registry_set_dword(const char *path, const char *name, UINT32 val);   /* an installer's registration */
bool um_registry_get_dword(const char *path, const char *name, UINT32 *out);
void um_registry_set_sz(const char *path, const char *name, const char *val);
bool um_registry_get_sz(const char *path, const char *name, char *out, int cap);
/* Changes with every key or value written: a cheap "has anything changed?" */
UINT32 um_registry_generation(void);
void um_registry_set_bin(const char *path, const char *name, const void *data, UINT32 len);
int  um_registry_get_bin(const char *path, const char *name, void *out, int cap);   /* its length, or -1 */
/* The event log (um_eventlog.c): an event from the kernel, an EVENTLOGRECORD
 * of @type (1 error, 2 warning, 4 information) from @source for the log
 * @log, which the EventLog service writes down; and CTL_EVENTLOG */
void   EvLogReport(const char *log, UINT16 type, const char *source, UINT32 id, int n, const char *const *strings);
UINT64 UmEventLogCtl(UINT64 arg, UINT64 ptr);
void   EvLogBoot(void);                      /* Kernel-General 12, once the registry is up */
/* Any-type value access, for kernel-mode Zw* registry calls (nt/zw.c) */
int  um_registry_get_value(const char *path, const char *name, UINT32 *type, void *out, int cap);
void um_registry_set_value(const char *path, const char *name, UINT32 type, const void *data, UINT32 len);
bool um_registry_delete_value(const char *path, const char *name);
bool um_registry_delete_tree(const char *path);    /* a key and everything below it */
bool um_registry_create_key(const char *path);
bool um_registry_key_exists(const char *path);
/* Enumeration for kernel-mode ZwQueryKey / ZwEnumerateKey / ZwEnumerateValueKey */
typedef struct {
    UINT64 wtime;                                  /* last write, 100 ns since 1601 */
    UINT32 subkeys, max_name;                      /* lengths in bytes */
    UINT32 values, max_value_name, max_data;
} UmRegKeyInfo;
bool um_registry_query_key(const char *path, UmRegKeyInfo *info);
int  um_registry_enum_key(const char *path, UINT32 index, UINT16 *name, int cap, UINT64 *wtime);
int  um_registry_enum_value(const char *path, UINT32 index, UINT16 *name, int ncap, int *nlen,
                            UINT32 *type, void *out, int cap);
/* The first-boot setup's answers (apps/welcome.c): UserName, FirstBootDone */
#define UM_SETUP_KEY "Machine\\SOFTWARE\\NovaOS\\Setup"
/* The user's SID: HKEY_CURRENT_USER is "User\\" UM_USER_SID */
#define UM_USER_SID "S-1-5-21-1000-2000-3000-1001"

/* Local accounts (um_accounts.c): the SAM, profiles and the session */
#define UM_ACCOUNT_NAME_MAX 20
#define UM_PASSWORD_MAX     256
#define UM_ACCT_ADMIN       0x1u          /* in Administrators */
#define UM_ACCT_DISABLED    0x2u
typedef struct {
    UINT32 rid, flags;
    bool   password;                      /* has one */
    char   name[UM_ACCOUNT_NAME_MAX + 1];
    char   full[64];                      /* display name ("" for none) */
    char   profile[96];                   /* C:\Users\name ("" until the first sign-in) */
} UmAccount;
int    um_accounts_list(UmAccount *out, int cap);          /* by RID */
bool   um_account_get(UINT32 rid, UmAccount *a);
bool   um_account_find(const char *name, UmAccount *a);    /* any case */
bool   um_account_name_ok(const char *name);
UINT32 um_account_add(const char *name, const char *pw, UINT32 flags, UINT32 *rid);   /* NTSTATUS */
UINT32 um_account_delete(UINT32 rid);
UINT32 um_account_delete_profile(UINT32 rid);         /* the folder, hive and ProfileList entry */
UINT32 um_account_set_password(UINT32 rid, const char *pw);  /* "" removes it */
bool   um_account_rename(UINT32 rid, const char *name);
bool   um_account_check(UINT32 rid, const char *pw);         /* (a wrong one waits a second) */
bool   um_account_verify(UINT32 rid, const char *pw);        /* ... without waiting (the sign-in screen's) */
UINT32 um_account_logon(const char *name, const char *pw, UmAccount *a);
bool   um_account_load_profile(UINT32 rid, char *path, int cap);
void   um_account_sid(UINT32 rid, UINT8 sid[28]);
void   um_account_sid_string(UINT32 rid, char *out, int cap);
UINT32 um_account_rid_of_sid(const UINT8 *sid);              /* 0: not a local account */
UINT32 um_session_rid(void);              /* the account signed in on the screen */
void   um_session_set(UINT32 rid);        /* ... signs in, or is switched to (wm/logon.c) */
bool   um_session_signed_in(UINT32 rid);  /* @rid has a sign-in (on the screen, or switched away from) */
int    um_sessions(UINT32 *rids, int cap);   /* the accounts signed in, in the order they did */
void   um_session_end(UINT32 rid);        /* @rid signed out (wm/logon.c, once its programs are ended) */
UINT32 um_account_set_flags(UINT32 rid, UINT32 flags);       /* the account type (UM_ACCT_ADMIN) or disabled; NTSTATUS */
/* The account picture: a PNG's C: path ("" for none: the initial in a
 * circle), as Windows keeps it under ...\AccountPicture\Users\<SID> */
bool   um_account_picture(UINT32 rid, char *path, int cap);
void   um_account_set_picture(UINT32 rid, const char *path);
bool   um_account_signin_required(void);  /* the sign-in screen comes before the desktop */
void UmFault(UINT32 status, UINT64 rip, UINT64 addr) __attribute__((noreturn));
void UmFaultAt(UINT32 status, UINT64 rip, UINT64 addr, UINT64 sp) __attribute__((noreturn));
/* Crash reports in C:\NovaOS\Crashes (um_crash.c) */
#define UM_CRASH_PATH 80
const char *UmCrashReport(const UmProcess *p);   /* the crashed program's report ("" if none) */
void UmCrashPoll(void);                 /* write the reports waiting (desktop thread, from UmPoll) */
RamNode *UmCrashNewest(void);           /* the newest report; caller holds FsLock; writes the queued ones first */
void UmCrashKernel(void);               /* a kernel fault is halting this CPU: save its report on the disk */
void UmCrashKernelFound(const char *text, UINT32 len);   /* at boot: the last start's kernel crash report */
/* A 32-bit program's system call (int 0x2E) keeps its registers in @frame */
void UmNoteSyscallFrame(void *frame);

/* -----------------------------------------------------------------------
 * The desktop lock: the window system, used by the desktop thread and
 * programs' window services.  It includes the file-system lock (ramfs),
 * which the file services take alone.  Order: desktop, then files.
 * ----------------------------------------------------------------------- */
void DesktopLock(void);
void DesktopUnlock(void);
/* The desktop lock without the file-system one: the desktop thread's loop,
 * which takes FsLock only around the work that may touch files (input,
 * drawing, built-in windows' timers), so programs' file I/O goes on
 * meanwhile */
void DesktopLockAlone(void);
void DesktopUnlockAlone(void);
void FsLock(void);
void FsUnlock(void);
/* Shared: reading and writing open files' contents, each under its own
 * lock as well (um_syscall.c); never taken twice by one thread */
void FsLockShared(void);
void FsUnlockShared(void);
struct Thread *DesktopLockOwner(void);     /* diagnostics */

/* Save the registry and drive C: to disk now (before a restart or shutdown). */
void UmSaveAll(void);
/* Timer tick: advances the clocks in KUSER_SHARED_DATA. */
void UmTimerTick(UINT64 ticks);
/* The keyboard layout in effect (wm/kbdlayout.c): its HKL, published in
 * KUSER_SHARED_DATA at UM_KUSD_KBD_HKL (past Windows' own fields: a
 * NovaOS one), where user32 reads it on every key */
#define UM_KUSD_KBD_HKL 0xF00
/* The machine's free memory in pages, kept current on every timer tick
 * (a NovaOS field too; the total is Windows' NumberOfPhysicalPages at
 * 0x2E8): GlobalMemoryStatusEx and NtQuerySystemInformation read both */
#define UM_KUSD_AVAIL_PAGES 0xF08
void UmSharedKeyboard(UINT32 hkl);
/* The number of online CPUs changed: update what programs see. */
void UmCpuCountChanged(void);
/* Log every program thread's state (serial), for diagnosing hangs. */
void UmDumpAll(void);

/* A page fault at @va in the current program: back a committed page that
 * was never touched (demand-zero).  True if the access can be retried. */
bool UmDemandFault(UINT64 va);
bool UmDemandWait(UINT64 va);
/* An instruction fetch at @va from a data page: in a program running with
 * DEP off, the page becomes executable.  True if the fetch can be retried. */
bool UmDepFault(UINT64 va);

/* Log the failing system calls of programs named @name ("" or NULL: off) */
void UmSetTrace(const char *name);
/* Is @p a program being traced ("trace +NAME" when @all: every call)? */
bool UmTraced(const UmProcess *p, bool all);
