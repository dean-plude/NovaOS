# NovaOS on a Mac

NovaOS is an x86-64 operating system.  A Mac runs it in one of three ways:

| Mac | How NovaOS runs | Speed |
|-----|-----------------|-------|
| Apple Silicon (M1 and later) | In QEMU or UTM, emulating an x86-64 PC (TCG) | Slow: every instruction is translated.  Booting takes minutes, not seconds |
| Intel | In QEMU or UTM with Apple's Hypervisor.framework (`-accel hvf`) | Close to native |
| Intel, booted directly | From a USB stick, on the bare hardware | Native, but few of the Mac's devices have drivers ([below](#on-a-real-intel-mac--untested)) |

Apple Silicon Macs cannot boot NovaOS on the hardware: they have no x86
CPU.  Hypervisor.framework on them runs only ARM guests, so `-accel hvf`
does not apply.

> **Status.** Nobody has yet followed this page on a real Mac.  The
> commands follow the build scripts and Homebrew's packages, and the
> sections marked *untested* are the most likely to need fixes; please
> report what breaks.  NovaOS is built and tested on Linux
> ([building.md](building.md)).

---

## Running the ready-made ISO (no build)

You can try NovaOS without building it.  You need QEMU, which brings the
UEFI firmware with it:

```bash
brew install qemu
mkdir nova && cd nova
curl -LO https://github.com/dean-plude/os/releases/latest/download/nova.iso
curl -LO https://github.com/dean-plude/os/releases/latest/download/nova.iso.sha256
shasum -a 256 -c nova.iso.sha256
```

Those URLs give the newest tagged release.  For the newest development
build of `main`, which CI republishes after every green push, use
`https://github.com/dean-plude/os/releases/download/latest/nova.iso` (and
`nova.iso.sha256`) instead.

The firmware's variable store has to be writable, so copy it first.
Homebrew ships one store file for both 32- and 64-bit x86
(`edk2-i386-vars.fd`).  `disk.img` is an empty disk to install onto; it
also keeps drive C: across restarts.  Make it 8 GB: Install NovaOS puts a
large EFI System Partition on it (room for the kernel twice, since an
update is staged beside the running one), and the file is sparse, so it
takes only the space actually written.

```bash
Q="$(brew --prefix qemu)/share/qemu"
cp "$Q/edk2-i386-vars.fd" /tmp/nova-vars.fd
qemu-img create -f raw disk.img 8G

qemu-system-x86_64 -machine q35 -m 2G -smp 4 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$Q/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/nova-vars.fd \
  -drive file=disk.img,format=raw \
  -cdrom nova.iso \
  -serial stdio
```

NovaOS boots live from the CD and opens **Install NovaOS**.  Install onto
`disk.img`, then leave out `-cdrom nova.iso` to start from the disk.  The
first start of the installed system asks for your name, time zone,
keyboard layout and display resolution.

Useful additions to the command line:

| Add | For |
|-----|-----|
| `-accel hvf -cpu host` | **Intel Macs only**: hardware virtualization.  Leave it out on Apple Silicon |
| `-device intel-hda -device hda-output,audiodev=snd0 -audiodev coreaudio,id=snd0` | Sound through the Mac's speakers |
| `-device intel-hda -device hda-micro,audiodev=snd0 -audiodev coreaudio,id=snd0` | Sound, and recording from the Mac's microphone (macOS asks for microphone access for your terminal) |
| `-display cocoa,zoom-to-fit=on` | Scaling the window on a Retina screen |
| `-smp 8` | More cores (NovaOS uses up to 16).  Under emulation, more cores help less |

The network needs nothing extra: QEMU's default network card on q35 is an
Intel e1000e, which NovaOS drives.

On Apple Silicon, give QEMU time.  The firmware alone takes a while before
the NovaOS boot logo appears; the serial log in the terminal shows that it
is progressing.  `-smp` above 4 rarely helps there.

### UTM (graphical) — *untested*

[UTM](https://mac.getutm.app) wraps QEMU in a Mac app.

1. **Create a New Virtual Machine**, then **Emulate** (Apple Silicon) or
   **Virtualize** (Intel Mac), then **Other**.
2. Boot device: **CD/DVD Image**, and pick the downloaded `nova.iso`.
3. Architecture **x86_64**, System **Standard PC (Q35 + ICH9)**, memory
   2048 MB or more, 4 cores.
4. Create an 8 GB drive.  Before saving, tick **Open VM Settings** and set:
   - **System**: **UEFI Boot** on.
   - **Drives**: interface **IDE** (on Q35 this is the AHCI SATA
     controller) or **NVMe**.  NovaOS has no built-in driver for VirtIO
     disks, so it would not see one.
   - **Display**: emulated display card **VGA** (the Bochs adapter, whose
     resolution NovaOS can change).  NovaOS also drives virtio-vga, QXL
     and VMware SVGA.
   - **Network**: emulated card **e1000e** (or **e1000**, or virtio-net).
   - **Sound**: **Intel HD Audio**.
5. Start it, install to the disk, then eject the ISO.

---

## Building on macOS — *untested*

The most reliable way to build on a Mac is inside a Linux container
(next section): the build is only tested on Linux, and a native macOS
build currently has a known problem.

### Build in a Linux container (recommended)

A Linux container runs the normal Linux build unchanged.  With Docker
Desktop, [OrbStack](https://orbstack.dev) or `colima`, from the top of
the repository:

```bash
docker run --rm -it -v "$PWD":/src -w /src ubuntu:24.04 bash -c '
  apt-get update &&
  DEBIAN_FRONTEND=noninteractive apt-get install -y \
      ca-certificates git cmake make nasm python3 \
      clang lld llvm libc++-dev gcc-mingw-w64-x86-64 \
      mtools dosfstools xorriso p7zip-full &&
  NOVA_NO_NETSURF=1 scripts/build.sh'
```

The build downloads Firefox, Mesa and Microsoft's Windows-Driver-Frameworks
source, so the container needs network access
([building.md](building.md#network-access-during-the-build)).  Those
downloads are cached in the container's `~/.cache/novaos`, which this
throwaway container does not keep; mount a host folder there
(`-v "$HOME/.cache/novaos":/root/.cache/novaos`) to reuse them.

On Apple Silicon the container is ARM Linux, which is fine: every
compiler the build uses is a cross compiler.  The results land in
`build/` on the Mac.  Start them with the QEMU command under
[Running your build](#running-your-build), not with `make run`, since the
build directory was configured inside the container.

### Native build

The build needs a cross toolchain, not Xcode's: Apple's `clang` has no
`lld-link`, and Apple's `ld` cannot link the kernel's ELF objects.  The
kernel and boot loader are built with GNU cross compilers from Homebrew,
and the Windows userland with Homebrew's LLVM.

**Known problem.**  The KMDF and UMDF builds (`tools/kmdf/build_kmdf.py`,
`tools/umdf/build_umdf.py`) read MinGW-w64's headers from the fixed Linux
path `/usr/share/mingw-w64/include`.  Homebrew installs them under its own
prefix, so a native build is expected to fail at that step until the scripts look
for the headers the way `userland/msvcp140/build.py` does.  Use the
container build above in the meantime.

#### Prerequisites

Install the Xcode command line tools (`xcode-select --install`) and
[Homebrew](https://brew.sh), then:

```bash
brew install cmake ninja nasm python git \
             llvm lld \
             x86_64-elf-gcc mingw-w64 \
             mtools dosfstools xorriso coreutils p7zip dosbox-staging \
             qemu
```

| Package | Used for |
|---------|----------|
| `llvm`, `lld` | `clang`, `clang++`, `lld-link`, `llvm-rc`, `llvm-lib`, `llvm-nm`: the Windows userland, NetSurf and the drivers; LLVM's libc++ headers, which the build finds next to `clang`, for HarfBuzz in `novatext.dll` |
| `x86_64-elf-gcc` | The kernel (CMake prefers it to clang) |
| `mingw-w64` | The UEFI boot loader (`x86_64-w64-mingw32-gcc`), and MinGW-w64's headers, which `msvcp140.dll` is compiled against |
| `nasm` | Kernel assembly |
| `p7zip` | `7z`, which unpacks the bundled Firefox installer and Mesa archive |
| `git` | Fetching the Windows-Driver-Frameworks source |
| `mtools`, `dosfstools` | `nova.img` (`mkfs.fat`, `mformat`, `mcopy`) and MS-DOS's boot disk |
| `dosbox-staging` | The MS-DOS build (`tools/msdos/build_msdos.py` runs a `dosbox` command headless); not tried on a Mac yet |
| `xorriso` | `scripts/create-iso.sh` |
| `coreutils` | GNU `truncate`, `stat -c` and `nproc`, which the scripts use and macOS lacks |
| `qemu` | Running NovaOS, and its UEFI firmware |

Homebrew's LLVM is "keg-only" (not on `PATH`, so Apple's `clang` would be
found instead), and GNU coreutils install with a `g` prefix.  Put both
first on `PATH` in the shell you build from, then check that `which`
lists all three under `$(brew --prefix)`:

```bash
export PATH="$(brew --prefix llvm)/bin:$(brew --prefix lld)/bin:$(brew --prefix coreutils)/libexec/gnubin:$PATH"
which clang lld-link truncate
```

#### Building

```bash
git clone https://github.com/dean-plude/os && cd os
Q="$(brew --prefix qemu)/share/qemu"
cmake -S . -B build -G "Unix Makefiles" \
    -DOVMF_CODE="$Q/edk2-x86_64-code.fd" \
    -DOVMF_VARS="$Q/edk2-i386-vars.fd"
make -C build -j"$(sysctl -n hw.ncpu)"
```

The `OVMF_*` options are needed because CMake only looks for the
firmware where Linux distributions put it.  `scripts/build.sh` works too,
with the same `PATH`.  The output is the same as on Linux: `bootx64.efi`,
`kernel.elf` and `nova.img` in `build/`.  `NOVA_NO_NETSURF=1` and
`NOVA_NO_WOW64=1` work as described in [building.md](building.md).

Errors from the kernel and userland build go to
`build/kernel_build-prefix/src/kernel_build-stamp/kernel_build-build-err.log`
(and `bootloader_build-...` for the boot loader), not to the terminal.

What else is likely to go wrong:

- **A different `clang` was picked up.**  `which clang` must print
  Homebrew's.  Delete `build/` after fixing `PATH`, since CMake caches
  the compiler.
- **`mkdosfs` not found, so no `nova.img`.**  Homebrew's `dosfstools`
  puts `mkfs.fat` in `$(brew --prefix)/sbin`; make sure that is on `PATH`
  (it is with `brew shellenv`).
- **The kernel or boot loader sub-build fails to configure.**  They are
  separate CMake projects configured for the host, and on macOS CMake may
  add Apple-only flags to them.  The `-configure-err.log` beside the
  build log shows the reason.

### Running your build

Run QEMU yourself, with at least 2 GB of memory.  `make run` works too
(it runs `scripts/run-qemu.sh` with the firmware set above, sound through
Core Audio), but it gives the machine only 256 MiB, which is less than
NovaOS now needs.

```bash
Q="$(brew --prefix qemu)/share/qemu"
cp "$Q/edk2-i386-vars.fd" /tmp/nova-vars.fd
[ -f build/nova-data.img ] || truncate -s 1G build/nova-data.img
qemu-system-x86_64 -machine q35 -m 2G -smp 4 \
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$Q/edk2-x86_64-code.fd" \
  -drive if=pflash,format=raw,unit=1,file=/tmp/nova-vars.fd \
  -drive file=build/nova.img,format=raw \
  -drive file=build/nova-data.img,format=raw \
  -serial stdio
```

On an Intel Mac, add `-accel hvf -cpu host`.  NovaOS formats the empty
`nova-data.img` on first boot and keeps drive C: there, so your files
survive rebuilds.  Copying programs onto it with `mmd` and `mcopy` works
as on Linux ([building.md](building.md#where-your-files-are-kept)).

GDB: Homebrew's `gdb` does not run on Apple Silicon, but a remote
debugger does not need to.  `brew install x86_64-elf-gdb`, add `-s -S`
to the QEMU command (or use `make run-debug`), then run
`x86_64-elf-gdb build/kernel.elf -ex 'target remote :1234'`.  LLDB
(`lldb build/kernel.elf`, then `gdb-remote 1234`) also works with QEMU's
stub.

---

## On a real Intel Mac — *untested*

NovaOS boots on UEFI PCs, and Intel Macs (2006 to 2020) have UEFI
firmware, so it should start from a USB stick.  Expect a minimal system:
most of the Mac's own devices have no NovaOS driver.

### Making the USB stick

`nova.iso` is a hybrid image (its EFI partition is in a GPT as well as on
the disc), so it can be written straight to a stick.  **This erases the
stick**; check the disk number carefully.  Find the stick in the
`diskutil list` output (for example `/dev/disk4`) and use its number in
the other three commands:

```bash
diskutil list
diskutil unmountDisk /dev/disk4
sudo dd if=nova.iso of=/dev/rdisk4 bs=4m
diskutil eject /dev/disk4
```

### Booting

- Macs with a **T2 chip** (most 2018 to 2020 models): first start macOS
  Recovery (hold Command-R), open **Utilities > Startup Security
  Utility**, and set **No Security** and **Allow booting from external
  media**.  NovaOS does not support Secure Boot.
- Hold **Option** while the Mac starts, and pick the **EFI Boot** disk.

### What to expect

| Part | Expected |
|------|----------|
| Display | Works at the resolution the firmware set (the UEFI framebuffer), with no resolution changes.  After sleep the screen may stay dark: no driver sets the mode again |
| External USB keyboard and mouse | Should work (xHCI, EHCI, OHCI and UHCI controllers, USB HID) |
| USB game controller | Wired Xbox 360 and Xbox One controllers and HID game pads should work (XInput and DirectInput); Bluetooth ones do not (no Bluetooth support) |
| Built-in keyboard and trackpad | Only on older models that connect them over USB internally; 2016 and later MacBooks use SPI, which NovaOS cannot drive.  Use an external USB keyboard and mouse |
| Internal SSD | Probably not seen on Macs with Apple's own NVMe controllers (2016 and later).  Older SATA Macs may see it, but **do not run Install NovaOS on a Mac whose disk you need**: it repartitions the disk |
| Keeping files | With no usable disk, drive C: lives in memory and is lost at shutdown |
| Network | None.  Mac Ethernet and Wi-Fi chips are Broadcom or Aquantia; NovaOS's network drivers are for Intel e1000/e1000e and virtio-net, and it has no Wi-Fi support yet |
| Sound | Unknown.  Macs use Intel HD Audio controllers with Cirrus Logic codecs, which the HD Audio driver may or may not set up.  T2 Macs route audio through the T2 and will be silent |
| Battery, power button | Read through ACPI; may work |
| Multiple cores | Should work |
| Boot log | Written into `EFI/NOVA/bootlog.txt` on the stick as NovaOS runs.  After a hang, read it in macOS: `diskutil mount disk4s2` (the stick's `NOVA_EFI` partition) |

To go back to macOS, restart.  NovaOS changes nothing on the Mac's disk
unless you install it.
