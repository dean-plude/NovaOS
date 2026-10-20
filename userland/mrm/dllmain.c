/* MRM.dll's DllMain: MRT Core has none of its own (Visual C++'s runtime
 * supplies an empty one); nothing to set up or tear down */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reason; (void)reserved;
    return TRUE;
}
