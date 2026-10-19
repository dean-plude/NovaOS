// NovaOS: what a function in the parts of the XAML core that do not build
// yet does when the built core calls it (build.py's unbuilt(), which makes
// each such function's thunk): says so, once per function, on the
// debugger's output and the program's, then answers "not implemented"
// (or, for a constructor or a function returning an object, stops the
// program, naming itself).  The name is the function's decorated name.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *s_reported[1024];
static unsigned s_count;
static SRWLOCK s_lock = SRWLOCK_INIT;

static void say(const char *what, const char *name)
{
    char line[1200];
    snprintf(line, sizeof(line), "XAML core: %s not built yet: %s\n", what, name);
    OutputDebugStringA(line);
    fputs(line, stdout);
    fflush(stdout);
}

extern "C" void nova_xaml_unbuilt_report(const char *name)
{
    AcquireSRWLockExclusive(&s_lock);
    bool seen = false;
    for (unsigned i = 0; i < s_count && !seen; i++)
        seen = s_reported[i] == name;
    if (!seen && s_count < sizeof(s_reported) / sizeof(s_reported[0]))
        s_reported[s_count++] = name;
    ReleaseSRWLockExclusive(&s_lock);
    if (!seen)
        say("function", name);
}

extern "C" __declspec(noreturn) void nova_xaml_unbuilt_stop(const char *name)
{
    say("function (stopping: no answer stands in for it)", name);
    ExitProcess(0xC0000002);  // STATUS_NOT_IMPLEMENTED
}
