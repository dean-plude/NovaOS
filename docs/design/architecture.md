# Key design decisions

The short list is in the [README](../../README.md#key-design-decisions).

- **Clean-room, ABI-faithful (the "hybrid" path)**: every DLL and service is
  written from scratch, but syscall numbers, `NTSTATUS` codes and structure
  layouts (PEB, TEB, loader data, `CONTEXT`…) follow Windows 10 1903 x64, so
  unmodified binaries find what they expect.  See
  [docs/ROADMAP.md](docs/ROADMAP.md#the-pivotal-decision-how-to-get-the-win32-api-surface).
- **Native execution, both bitnesses**: x64 code runs in long mode; x86 code
  runs in compatibility mode with a 32-bit `ntdll` that converts each system
  call to the kernel's 64-bit form, the way Windows' WoW64 does.
- **The window system lives in the program**: `user32` keeps each program's
  window tree; the kernel's window manager composites only top-level
  windows, drawn from bitmaps the programs own.  The kernel also draws the
  pointer: a program's `SetCursor` shape (animated .ani cursors included)
  over its own windows, the desktop's arrow elsewhere, and the system
  pointers (I-beam, busy ring, resize arrows on window edges, hand, cross,
  "no" and the rest of `IDC_*`) from vector outlines, sharp at 200 %.
- **Software rendering**: GDI is a CPU rasterizer drawing into a back
  buffer in RAM at integer HiDPI scale.  On QEMU's standard VGA, QXL,
  virtio-vga and VMware adapters (and Bochs, VirtualBox's VBoxVGA) a VBE
  "DISPI" driver sets the resolution at run time (Settings > Display,
  `ChangeDisplaySettings`) and flips between two pages of video memory
  when both fit; Cirrus gets 800x600 and 640x480; elsewhere frames are
  copied to the UEFI framebuffer in the boot mode.  There is no 3D GPU driver.
- **Drive C: in memory, saved to FAT**: the RAM disk is saved to a FAT32
  volume a second after each change and restored at boot.  A file takes
  the memory its contents need (one written by appending gives back the
  rest of the buffer it grew into when it is closed; `mem` in the Terminal
  shows what C: takes).  When memory runs short, the contents of saved
  files nothing holds are let go of, those unused longest first, and read
  back from the volume when wanted, as Windows drops cached file pages;
  at boot the large files are restored without being read until then
  (`cachetest`).  System files
  come from the kernel image, so a new build always brings its own.  The
  save runs on its own thread and holds no lock while the disk is written
  (`savetest`), and a crash during a FAT save leaves each file old or new.
- **SMP with fine-grained locks**: the scheduler, memory,
  synchronization, sockets, the GUI, the program loader, files, the
  registry, the console and starting processes have their own locks, and
  file and registry throughput scale about 3x from one CPU to four
  (`smpstress scaling 3`); the SATA, NVMe and USB disk drivers take a lock
  per disk; the rest of the kernel keeps a big lock (rules
  and lock order in `kernel/ke/smp.h`; the Terminal's `profile` command
  shows where the CPUs spend their time).
- **Kernel-helper syscalls (0x01F0–0x01FF)** are private to NovaOS's own
  DLLs and invisible to Windows programs.
- **Windows drivers the Windows 11 way (planned)**: NovaOS's own drivers are
  built into the kernel; Windows driver packages will run unchanged on an
  NT kernel-mode driver interface (a kernel PE loader, `ntoskrnl.exe` and
  `hal.dll` exports in the Microsoft x64 ABI, KMDF and UMDF built from
  Microsoft's MIT source), matched by hardware ID from the driver store and
  downloaded by hardware ID, under Windows' kernel-mode signing policy
  (`kernel/nt/ci.c`).  See [windows-drivers.md](windows-drivers.md).
