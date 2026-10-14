# Key design decisions

NovaOS runs unmodified Windows programs and drivers on a kernel of its own.
This page lists the decisions that shape it, what each one means in the
code, and where to look. The [README](../../README.md#how-it-works) has the
short version; [inside.md](../inside.md) tours the parts.

## Clean-room, but ABI-faithful

Every DLL, service and driver interface is NovaOS's own code (apart from
the third-party libraries listed in the README's licence section), but
everything a Windows binary can observe follows Windows 10 1903 (build
18362) x64:

- **System-call numbers.** Every NT service NovaOS has that Windows has is
  at its Windows 10 1903 number (0x000 to 0x1CF), so programs that make
  system calls without `ntdll` still work (`kernel/ke/syscall.h`).
- **Structure layouts**: the TEB, PEB, process parameters, loader data,
  `KUSER_SHARED_DATA`, `CONTEXT` and `EXCEPTION_RECORD`, and `NTSTATUS`
  codes.
- **The GDT**: Windows' selectors (0x33 for 64-bit code, 0x23 for 32-bit
  code, 0x2B for data, 0x53 for a 32-bit TEB), because programs read and
  far-jump through them.

`abitest` (`userland/programs/abitest.c`, run by the core self-tests)
writes out each offset and number as the Windows value and fails if
NovaOS moves one.

## NovaOS's private system calls

NovaOS's own services, the kernel half of `user32`, `gdi32`, sockets,
audio, the GPU and the loader, are numbered from 0x200, past Windows'
last number; the table holds 0x300 entries. They are exported from
`ntdll` as `NtNova*` functions and only NovaOS's own DLLs call them, so a
Windows program never meets them unless it goes looking.

## Both bitnesses, natively

x64 code runs in long mode. x86 code runs in compatibility mode in the
same process model, with its own 32-bit `ntdll` (`userland/ntdll/ntdll_wow.c`)
that rebuilds each system call's arguments in their 64-bit layout and
enters the kernel with `int 0x2E`, the way Windows' WoW64 layer does. Every
system DLL is built twice, into `System32` and `SysWOW64`, and a 32-bit
process's PEB, TEBs, stacks and DLLs are placed below 2 GiB.

## The window system lives in the program

`user32` keeps each process's window tree, controls, menus and dialogs in
the process itself. Each top-level window's client area is a bitmap in
the program's own memory, drawn with `gdi32`; the kernel's window manager
(`kernel/um/um_gui.c`, `kernel/wm/`) composites only top-level windows from
those bitmaps and turns input into messages. The kernel also draws the
pointer: a program's `SetCursor` shape (animated `.ani` cursors included)
over its own windows, the desktop's arrow elsewhere, and the system
pointers (`IDC_*`) from vector outlines, sharp at 200%.

## CPU rendering first, the GPU where there is one

- **2D**: the desktop and GDI draw on the CPU into a back buffer in RAM, in
  logical pixels at an integer HiDPI scale, then copy it to the screen.
- **Display adapters**: a VBE "DISPI" driver sets modes at run time and
  flips pages on QEMU's standard VGA, QXL, virtio-vga, VMware and
  bochs-display adapters and VirtualBox's VBoxVGA; Cirrus gets 640x480 and
  800x600; anything else keeps the UEFI framebuffer in the boot mode. A
  Windows display-only (WDDM) driver can take the screen over through
  NovaOS's `dxgkrnl` stand-in (`kernel/nt/dxgkrnl.c`).
- **3D on the CPU**: OpenGL 4.5 on Mesa's llvmpipe is built in; Vulkan on
  lavapipe, with DXVK and VKD3D-Proton for Direct3D 8 to 12, comes from the
  App Store.
- **3D on a GPU**: today only under QEMU, through a 3D virtio-gpu
  (`virtio-vga-gl`), where Mesa's Venus (Vulkan) and virgl (OpenGL) run on
  the host's GPU. A real GPU, starting with the reference laptop's Intel
  Iris Xe through Intel's own Windows driver, is the next step and is not
  built yet ([intel-gpu.md](intel-gpu.md)).

## Drive C: in memory, saved to disk

Drive C: is a RAM disk (`kernel/fs/ramfs.c`). `kernel/fs/persist.c` saves
what changes to a disk volume a second after C: goes quiet, and restores
it at boot. The volume is the first of: a FAT or NTFS volume labelled
`NOVADATA` (Setup makes one), an empty disk (formatted as FAT32
`NOVADATA`), or the FAT volume NovaOS booted from.

- On **NTFS** (Setup's default), C: is the whole volume, with creation
  times, security descriptors and hard links.
- On **FAT32**, C: is the folder `\NOVA\C`; hard links are saved as copies
  and joined again at boot.
- **System files** come from the kernel image and are never saved, so a
  new build or an update always brings its own DLLs and programs.
- **The save** copies what changed under the file-system lock, then writes
  it on its own thread without that lock or the big kernel lock
  (`savetest`).
- **Low memory**: the contents of saved files nothing holds are let go of,
  least recently used first, and read back when wanted, as Windows drops
  cached file pages; large files are restored at boot without being read
  until then (`cachetest`).
- **Power cuts**: a save cut off part way is put right at the next boot
  (NTFS volumes are repaired from their MFT records; on FAT, clusters no
  file reaches are reclaimed).

## SMP with fine-grained locks and a big kernel lock

Every CPU the ACPI MADT reports (up to 16) runs threads, with a run queue per CPU.
Kernel code that has no lock of its own runs under a big kernel lock
(BKL), which belongs to threads and is dropped while a thread waits. The
busy paths run without it, under their own locks: the scheduler, timers
and IPIs, waits and events, memory, sockets, the GUI and the desktop's
drawing, files, the registry, the console, and starting processes and
threads. The AHCI, NVMe and USB storage drivers take a lock per disk.
The rules and the lock order are in `kernel/ke/smp.h`.

`smpstress scaling` measures how file and registry throughput grow with
the number of CPUs; the nightly run checks they scale at least half as
well as bare system calls in the same run (`tools/ci/check-smpstress.py`).
The Terminal's `profile` command shows where the CPUs spend their time,
including time spent waiting for the BKL.

## Windows drivers, the Windows way

NovaOS's own drivers are compiled into the kernel and play the part of
Windows' inbox drivers. Beside them, real Windows driver packages run
unchanged on an NT kernel-mode driver interface (`kernel/nt/`): a kernel PE
loader, `ntoskrnl.exe` and `hal.dll` exports in the Microsoft x64 ABI, KMDF
and UMDF built from Microsoft's MIT-licensed source, and NDIS, Storport,
USB and display-only WDDM hosts. Drivers are matched to devices by hardware
ID from the driver store, can be downloaded by hardware ID, and load only
under Windows' kernel-mode signing policy (`kernel/nt/ci.c`). See
[windows-drivers.md](windows-drivers.md) for the design and
[drivers.md](../drivers.md) for what works today.
