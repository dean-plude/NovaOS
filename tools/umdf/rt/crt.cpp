/*
 * crt.cpp — the C++ runtime pieces the framework uses beyond its own
 * allocators: global new and delete on the process heap (the IFR log
 * buffer, tracingum.cpp, is a new[] array).  Allocation failure returns
 * NULL, as the framework (built without exceptions) checks for.
 */
#include <windows.h>

void *__cdecl operator new(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void *__cdecl operator new[](size_t n) { return HeapAlloc(GetProcessHeap(), 0, n ? n : 1); }
void __cdecl operator delete(void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
void __cdecl operator delete[](void *p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
