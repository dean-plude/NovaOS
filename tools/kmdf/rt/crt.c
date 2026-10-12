/*
 * crt.c — the compiler-support routines MSVC's kernel-mode runtime gives a
 * driver, which the framework's objects call: a pure virtual call bug
 * checks, and CR8 (the task priority register, which is the IRQL on x64)
 * is read and written by instruction
 */
#include <ntddk.h>

#define DRIVER_VERIFIER_DETECTED_VIOLATION 0xC4

int __cdecl _purecall(void)
{
    KeBugCheckEx(DRIVER_VERIFIER_DETECTED_VIOLATION, 0x1000, 0, 0, 0);
    return 0;
}

/* declared as clang's intrin.h has them (the IRQL fits either width) */
unsigned long __readcr8(void)
{
    unsigned __int64 v;
    __asm__ __volatile__("mov %%cr8, %0" : "=r"(v));
    return (unsigned long)v;
}

void __writecr8(unsigned int v)
{
    __asm__ __volatile__("mov %0, %%cr8" : : "r"((unsigned __int64)v) : "memory");
}
