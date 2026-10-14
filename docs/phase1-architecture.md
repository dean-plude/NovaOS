# Boot flow, address space and the early kernel

How NovaOS gets from the UEFI firmware to a running kernel: the
bootloader, the virtual address layout, the kernel's start-up sequence,
memory management, interrupts, timers and the scheduler. Phase 1 of the
roadmap built this layer; it has grown since, and this page describes it
as the code has it now. For the rest of the system see
[inside.md](inside.md) and [design/architecture.md](design/architecture.md).

## Boot flow

The bootloader is a PE32+ EFI application (`bootloader/src/main.c`). The
firmware starts it as `\EFI\BOOT\BOOTX64.EFI` from the EFI System
Partition (ESP), or from the ISO on a CD or a USB stick.

```
UEFI firmware (OVMF in QEMU)
 └─ \EFI\BOOT\BOOTX64.EFI  (bootloader/src/main.c)
     1. Load the kernel ELF: \EFI\NOVA\kernel.elf, or on an installed disk
        a pending update's kernel.new (choose_kernel)
     2. Set up the GOP framebuffer
     3. Live media (CD, or the ISO on a USB stick): read kernel.elf and
        BOOTX64.EFI into memory for the installer; installed disk: make
        sure the firmware has a "NovaOS" boot entry
     4. Find the ACPI 2.0 RSDP; copy the SMBIOS tables
     5. Build the page tables (below)
     6. Allocate a 16 KiB boot stack and the BootInfo page
     7. Get the memory map and call ExitBootServices
     8. Fill BootInfo, load CR3, jump to the kernel's entry point
 └─ kernel.elf
     entry.asm (_start): switch to the 16 KiB stack at the end of .bss,
        zero .bss, call KiSystemStartup(BootInfo physical address)
     ke/main.c (KiSystemStartup): see "Kernel start-up" below
```

The handoff structure, `BootInfo`, is defined in
[`include/boot_protocol.h`](../include/boot_protocol.h) (protocol version
4). It carries the memory map, the framebuffer, the RSDP, where the kernel
was loaded, boot flags (live media, live USB, boot entry added, an update
being tried or one that failed), the installation files on live media and
the SMBIOS copy. Every address in it is physical; the kernel reads it
through the physmap. There is no initial ramdisk: the bootloader leaves
the `initrd` fields zero, and drive C:'s system files come from the kernel
image itself.

### Page tables the bootloader builds

`bootloader/src/paging.c` builds a 4-level page table with:

| Mapping | Virtual | Physical | Page size |
|---|---|---|---|
| Identity map | `0` to 64 GiB | `0` to 64 GiB | 2 MiB below 4 GiB, 1 GiB above |
| Physmap | `0xFFFF800000000000` + 64 GiB | `0` to 64 GiB | 1 GiB |
| Kernel image | `0xFFFFFFFF80200000` and up | `0x200000` and up | 4 KiB |

The identity map keeps the bootloader's own code and stack valid after
the CR3 switch (with more than 4 GiB of RAM the firmware may load it
above 4 GiB). The kernel's linker script (`kernel/linker.ld`) links it at
`KERNEL_VIRT_BASE + 0x200000` = `0xFFFFFFFF80200000` and loads it at
physical 2 MiB.

## Virtual address layout

Each process has its own PML4. The lower half (entries 0 to 255) is the
process's; the upper half (256 to 511) is copied from the kernel's, so
every kernel mapping is in every address space
(`paging_create_process_pt` in `kernel/arch/x86_64/paging.c`). A process
does not inherit the bootloader's identity map.

```
0x0000000000000000  ┌──────────────────────────────────┐
                    │ User space (128 TiB)             │
0x00007FFFFFFFFFFF  └──────────────────────────────────┘
                      non-canonical hole
0xFFFF800000000000  ┌──────────────────────────────────┐  PHYSMAP_BASE
                    │ Physmap: physical 0 to 64 GiB    │
0xFFFF801000000000  ├──────────────────────────────────┤
                      (unused)
0xFFFF806000000000  ├──────────────────────────────────┤  physmap + 384 GiB
                    │ Mapped heap blocks (128 GiB)     │
0xFFFF808000000000  └──────────────────────────────────┘
                      ...
0xFFFFF78000000000    KUSER_SHARED_DATA, for drivers (one page)
                      ...
0xFFFFFFFF00000000  ┌──────────────────────────────────┐
                    │ Windows driver images (1 GiB)    │
0xFFFFFFFF40000000  └──────────────────────────────────┘
0xFFFFFFFF80000000  ┌──────────────────────────────────┐  KERNEL_VIRT_BASE
0xFFFFFFFF80200000  │ Kernel: .text.entry, .text,      │
                    │ .rodata, .data, .bss and the     │
                    │ boot stack                       │
0xFFFFFFFFFFFFFFFF  └──────────────────────────────────┘
```

The constants are `PHYSMAP_BASE`, `PHYSMAP_SIZE` and `KERNEL_VIRT_BASE` in
`kernel/include/types.h`, `MAPPED_BASE` in `kernel/mm/vmm.c`, `DRV_BASE`
in `kernel/nt/ldr.c` and `UM_KI_USER_SHARED_DATA` in `kernel/um/um.c`.

The physmap lets the kernel reach any physical address below 64 GiB as
`PHYSMAP_BASE + phys` without making new page-table entries.

### A program's half

The fixed user-mode addresses are in `kernel/um/um_internal.h`:

| | 64-bit process | 32-bit process |
|---|---|---|
| `KUSER_SHARED_DATA` (read-only) | `0x7FFE0000` | `0x7FFE0000` |
| PEB, loader data, parameters, TEBs | from `0x7FFDF0000000` | from `0x7FEE0000` |
| First thread's stack top | `0x7FFDE0000000` (1 MiB) | `0x7FE00000` |
| `NtAllocateVirtualMemory` range | `0x10000000` to `0x7FFD00000000` | `0x110000` to `0x7FD00000` |
| Relocated DLLs | `0x180000000` to `0x7FF000000000` | `0x10000000` to `0x7F000000` |

System DLLs have fixed preferred bases, set in each DLL's
`userland/<name>/dll.json` (`base` for 64-bit, `base_x86` for 32-bit).

## Kernel start-up

`KiSystemStartup` (`kernel/ke/main.c`) brings the kernel up in this
order:

1. **Serial** (COM1) for the log, then the **framebuffer**.
2. **PMM** from the memory map; the firmware's SMBIOS copy; a check that
   the low pages the other CPUs' start-up trampoline needs (at `0x8000`)
   are free, made before the memory map can be reused; the
   installation-media and update flags.
3. **Paging**: take over the bootloader's tables and enable NX. **VMM**:
   the kernel heap.
4. **GDT and TSS**, CPU 0's KPCR (GS points at it in kernel mode), the
   **IDT**, the **HPET**, and the **local APIC** timer, calibrated.
5. **Scheduler**; from here boot runs under the big kernel lock.
6. The NT executive: **object manager**, **security reference monitor**
   and the system token, **process manager**, **registry**, the
   **system-call dispatcher** (`int 0x2E` and `SYSCALL`), **I/O manager**,
   **section objects**, the **PE loader** and its user-mode stub pages,
   the **VFS**, and **CSRSS**'s kernel half.
7. Self-tests of user-pointer probing and the process manager, and three
   boot test threads (`thread_a`, `thread_b`, `mem_test`).
8. **Devices**: drive C:'s RAM disk, PCI, the Plug and Play tree, ACPI,
   the I/O APIC, the AML interpreter, the display, disks and the volume
   that keeps drive C:, other drives, the network, audio, then the Windows
   program environment (`UmInit`).
9. **The other CPUs** start (`smp_start`).
10. **The desktop**: GDI, the window manager and shell, input (PS/2, USB,
    I2C-HID touchpads, Hyper-V's VMBus), the device-poll thread and the
    desktop thread. With no framebuffer NovaOS runs headless.
11. Interrupts on, any pending update finished or undone, the big kernel
    lock released, and CPU 0 becomes its idle thread.

## Memory management

### Physical memory (`kernel/mm/pmm.c`)

A bitmap with one bit per 4 KiB page (1 = used), one 64-bit word per 64
pages, so a scan skips a full word with one compare. It is built from the
memory map: everything marked used, conventional memory freed, then the
kernel image, the bitmap itself and the first 1 MiB marked used again.
512 free pages just below 16 MiB are set aside as a pool for devices that
need low memory.

### Kernel heap (`kernel/mm/vmm.c`)

- **Slabs** for 8 to 2048 bytes (powers of two): one 4 KiB page per slab,
  the free list kept inside the free objects. Each CPU keeps a small
  magazine of freed objects of each size, so most `kmalloc`/`kfree` pairs
  take no shared lock.
- **Large blocks** (over 2 KiB): a physically contiguous run from the PMM
  with a header page, addressed through the physmap.
- **Mapped blocks**: blocks of 4 MiB or more, and any large block when no
  contiguous run is free, are built from single pages mapped one after
  another into 2 MiB slots of the 128 GiB window at physmap + 384 GiB.
  That window is inside the same top-level entry as the physmap, so every
  address space sees it. Blocks grow in place into the slots after them,
  and freed slots are reused only after one TLB shootdown covers them all.

## Segments and interrupts

### GDT (`kernel/arch/x86_64/gdt.h`)

NovaOS uses Windows' own x64 layout (`KGDT64_*`), because programs see it:

| Slot | Descriptor | Selector programs see |
|---|---|---|
| 0x00 | Null | |
| 0x10 | Kernel code (64-bit) | |
| 0x18 | Kernel data | |
| 0x20 | User code, 32-bit (compatibility mode) | 0x23 |
| 0x28 | User data and stack | 0x2B |
| 0x30 | User code, 64-bit | 0x33 |
| 0x40 | TSS (16-byte descriptor) | |
| 0x50 | The 32-bit TEB, FS in 32-bit code | 0x53 |

A far jump to 0x23 runs 32-bit code inside a 64-bit program, as on
Windows. `SYSCALL`/`SYSRET` take their selectors from the STAR MSR and
need this order (STAR = `0x0023_0010_0000_0000`, as on Windows). 0x08 and
0x60-0x68 are left empty, for Windows' GDT limit of 0x6F.

### IDT vectors (`kernel/arch/x86_64/idt.h`)

All 256 vectors have interrupt gates (interrupts off on entry).

| Vector | Use |
|---|---|
| 0x00-0x1F | CPU exceptions; #DF, NMI, #MC and #DB on IST stacks 1-4 |
| 0x03 | Breakpoint, callable from user mode |
| 0x29 | `__fastfail` (user mode): the process ends with `STATUS_STACK_BUFFER_OVERRUN` |
| 0x2E | NT system call (`int 0x2E`, user mode); 32-bit programs' only way in |
| 0x2F | DPCs (Windows drivers) |
| 0x30 | Local APIC timer |
| 0x32 | ACPI SCI, through the I/O APIC |
| 0x33-0x34 | GPIO controllers (touchpad interrupts) |
| 0x35 | An I2C-HID device's own interrupt line |
| 0x36 | Hyper-V SynIC (VMBus) |
| 0x60-0xBF | Windows drivers' interrupts, line-based or MSI/MSI-X (IRQL 6-11) |
| 0xF0 | IPI: wake a halted CPU or switch to a woken thread |
| 0xF1 | IPI: TLB shootdown |
| 0xFF | APIC spurious interrupt |

Most of NovaOS's own drivers poll from the device-poll thread or their own
threads rather than taking interrupts.

### Exceptions in programs

A program's own faults go to its SEH handlers (`UmUserException` in
`kernel/um/um_exception.c`): the kernel builds a `CONTEXT` (with an XSAVE
area holding the AVX state when AVX is on, as `CONTEXT_XSTATE` on Windows)
and an `EXCEPTION_RECORD` on the user stack and points the thread at
`ntdll!KiUserExceptionDispatcher`. Floating-point faults are named from
the flags that are both raised and unmasked (x87 status word for #MF,
MXCSR for #XM). Program threads start with Windows' floating-point state:
x87 control word `0x27F` and MXCSR `0x1F80`. The scheduler saves each
thread's FPU state with `XSAVE`/`XRSTOR` where the CPU has them
(`kernel/arch/x86_64/xstate.c` enables x87, SSE and AVX state in XCR0),
otherwise with `fxsave`/`fxrstor`. QEMU's TCG never raises SSE
exceptions, so MXCSR mistakes show only under KVM or on real hardware.

## Timers

`kernel/arch/x86_64/apic.c` calibrates the local APIC timer and the TSC
over 10 ms against the HPET's main counter (`kernel/hal/hpet.c`, found
through the ACPI `HPET` table). Where the firmware hides the HPET, the
frequencies come from Hyper-V's frequency MSRs or CPUID leaf 0x15 (or
0x16), and the PIT's channel 2 is the last resort.

Each CPU's APIC timer is one-shot: in TSC-deadline mode where the CPU has
it (CPUID.1:ECX bit 24), otherwise counting down. At each interrupt the
scheduler arms it for whichever comes first: the next 10 ms tick (one grid
for all CPUs) or the earliest TSC deadline among the threads sleeping on
that CPU (`sched_sleep_until_tsc`, used by `NtDelayExecution` and timed
waits). A thread woken by its deadline goes to the front of the run queue
and preempts the running thread when its priority is at least as high, so
`Sleep(1)` ends within a fraction of a millisecond even with every CPU
busy (`sleeptest timer`). Tick work (the tick count, time slices) still
happens every 10 ms.

## Scheduler (`kernel/ke/scheduler.c`)

### Run queues and context switches

Each CPU has its own run queue (a circular list of ready threads, highest
current priority first) and its own list of timed sleepers, under the
queue's spinlock. A CPU with nothing to run steals a ready thread from
another CPU's queue; a thread that becomes ready wakes an idle CPU with
`IPI_WAKE`. The scheduler itself never takes the big kernel lock.

Thread states are `RUNNING`, `READY`, `WAITING`, `SUSPENDED` and `DEAD`. A
context switch saves the callee-saved registers and RFLAGS
(`ThreadContext`), switches stacks and updates the TSS's RSP0 and the
KPCR. A new thread's stack "returns" into `thread_trampoline`, which calls
the entry function (kept in R12, its argument in R13) and then
`sched_exit`. Kernel threads get 16 KiB stacks by default.

### Time slices

The quantum is 2 ticks (20 ms), and 6 ticks (60 ms) for the foreground
process: the process whose window is active, or the console program
running in the active Terminal (`UmUpdateForeground`). This is Windows'
quantum stretching for client systems.

### Priorities and boosts

Priorities are NT's: a base and a current priority, 1-15 dynamic and
16-31 real-time.

- **Wake-up boosts** (`BOOST_*` in `kernel/ke/scheduler.h`): a thread
  woken from a wait runs at its base plus the waker's increment, never
  above 15 and never for a real-time thread: +1 for an event, semaphore,
  mutex, alert or timed wait, +1 for file I/O, +2 for a named pipe, the
  network or a window message (keyboard and mouse input to a window
  included), +6 for console input. The boost decays one level per quantum
  the thread runs.
- **Foreground boost**: the foreground process's threads get +2 more on
  every wake-up (Windows' `PsPrioritySeparation`), still never above 15.
- **Preemption**: a woken thread preempts the running one when its
  priority is higher, or equal and a timer woke it. It goes first in its
  CPU's queue, and the CPU is told with `IPI_WAKE` if it is busy.
- **Balance set**: once a second, a thread ready for 3 s without running
  is raised to 15 for one quantum.
- **Program priorities** (`kernel/um/um_thread.c`): a thread's base is its
  process's priority class (IDLE 4, BELOW_NORMAL 6, NORMAL 8,
  ABOVE_NORMAL 10, HIGH 13, REALTIME 24) plus its `SetThreadPriority`
  increment (-2 to +2), with `THREAD_PRIORITY_IDLE` and `TIME_CRITICAL`
  saturating at 1 and 15 (16 and 31 for REALTIME). REALTIME and absolute
  priorities of 16 and up need `SeIncreaseBasePriorityPrivilege`;
  `kernel32` gives HIGH instead, as Windows does.
  `SetThreadPriorityBoost` and `SetProcessPriorityBoost` turn boosts off.
- **Kernel threads** run in the real-time range, above anything a program
  can ask for: 19 for the device-poll thread and the audio mixer, 18 for
  the network stack and the USB thread, 17 for the desktop and starting
  programs, 16 for bulk work (saving drive C:, ACPI, Setup). Each blocks or
  sleeps when it has nothing to do.
- **MMCSS** (`userland/avrt/avrt.c`, `sched_set_mmcss`):
  `AvSetMmThreadCharacteristics` puts a thread in the real-time range
  without the privilege. Audio tasks ("Pro Audio", "Audio", "Capture",
  "Playback", "Low Latency") run at 18; other tasks, and audio tasks at
  low priority, at 16. A thread found running 8 of the 10 ticks of a
  100 ms period drops to its own base until the period ends, Windows' 80%
  limit (`mmcsstest`). NovaOS's own sound threads in `winmm`, `dsound` and
  WASAPI register this way.

## Debugging with GDB

Build NovaOS ([building.md](building.md)), then start QEMU with its GDB
stub on port 1234, waiting for the debugger:

```bash
cmake --build build --target run-debug
```

or, calling the script directly (the third argument, the OVMF variables
file, must be given before `--gdb`):

```bash
./scripts/run-qemu.sh build/nova.img /usr/share/OVMF/OVMF_CODE.fd /usr/share/OVMF/OVMF_VARS.fd --gdb
```

In a second terminal:

```bash
gdb build/kernel.elf \
    -ex 'target remote :1234' \
    -ex 'hbreak KiSystemStartup' \
    -ex 'continue'
```

The kernel log goes to the serial port, which `run-qemu.sh` connects to
the terminal QEMU runs in. Early in boot it looks like this (addresses and
counts vary):

```
NovaOS Bootloader v0.1
...
[NovaOS] Serial console active
...
[PMM] Free: X MiB / Y MiB
[IDT] Initialized: 256 gates, IDTR base=0x...
[HPET] At 0xfed00000: 100000000 Hz, 64-bit main counter, 3 comparators
...
[Thread A] iteration 0 (TID=...)
[Thread B] iteration 0 (TID=...)
...
[MemTest] All allocations OK
```

## Build layout

The root `CMakeLists.txt` builds the two halves as separate CMake
projects with `ExternalProject_Add`, because they use different
toolchains:

- `kernel_build` (`kernel/CMakeLists.txt`): freestanding C with
  `x86_64-elf-gcc` if installed, otherwise clang; NASM for the assembly
  (elf64); linked with `kernel/linker.ld`. The Windows userland is built by
  `tools/build_userland.py` and embedded in the kernel image.
- `bootloader_build` (`bootloader/CMakeLists.txt`): MinGW-w64 GCC or
  clang with lld-link, producing a PE32+ EFI application.

The `disk_image` target puts both on a FAT disk image (`nova.img`) with
`scripts/create-disk.sh`; `run` and `run-debug` start it in QEMU.
[building.md](building.md) has the full build instructions.
