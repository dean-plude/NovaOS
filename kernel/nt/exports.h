/*
 * exports.h — what a Windows driver can import from NovaOS
 *
 * The one list the export tables are made from (ntoskrnl.c includes it
 * with NT_EXPORT defined to build them; tools that compare a .sys file's
 * imports with it read the same lines).  Each line names the module a
 * driver imports from and the export; the function is nt_<Name> in
 * kernel/nt/, compiled with the Microsoft x64 calling convention (NT_API).
 * A driver importing anything not listed does not load: the boot log names
 * the image and the missing export.
 *
 * Kept in alphabetical order within each module.
 */
/* ntoskrnl.exe */
NT_EXPORT(ntoskrnl.exe, DbgPrint)
NT_EXPORT(ntoskrnl.exe, DbgPrintEx)
NT_EXPORT(ntoskrnl.exe, IoCreateDevice)
NT_EXPORT(ntoskrnl.exe, IoCreateSymbolicLink)
NT_EXPORT(ntoskrnl.exe, IoDeleteDevice)
NT_EXPORT(ntoskrnl.exe, IoDeleteSymbolicLink)
NT_EXPORT(ntoskrnl.exe, IofCallDriver)
NT_EXPORT(ntoskrnl.exe, IofCompleteRequest)
NT_EXPORT(ntoskrnl.exe, RtlCompareMemory)
NT_EXPORT(ntoskrnl.exe, RtlCopyUnicodeString)
NT_EXPORT(ntoskrnl.exe, RtlInitUnicodeString)
NT_EXPORT(ntoskrnl.exe, memcmp)
NT_EXPORT(ntoskrnl.exe, memcpy)
NT_EXPORT(ntoskrnl.exe, memmove)
NT_EXPORT(ntoskrnl.exe, memset)
NT_EXPORT(ntoskrnl.exe, strlen)
NT_EXPORT(ntoskrnl.exe, wcslen)
/* hal.dll */
NT_EXPORT(hal.dll, KeStallExecutionProcessor)
