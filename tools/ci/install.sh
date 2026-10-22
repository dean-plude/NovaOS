#!/usr/bin/env bash
# Shared runner dependencies. No toolchain on jobs that only consume an image.
set -euo pipefail
profile=${1:-runtime}
base=(qemu-system-x86 qemu-system-gui ovmf mtools dosfstools acpica-tools
      pulseaudio pulseaudio-utils python3-pil ntfs-3g curl p7zip-full ffmpeg)
case "$profile" in
    build) base+=(cmake make nasm clang lld llvm libc++-dev gcc-mingw-w64-x86-64 xorriso ccache dosbox) ;;
    graphics) base+=(gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 g++-mingw-w64-x86-64
        g++-mingw-w64-i686 xvfb xauth mesa-vulkan-drivers libgl1-mesa-dri libegl1
        seabios ipxe-qemu ninja-build pkg-config libglib2.0-dev libpixman-1-dev
        libsdl2-dev libepoxy-dev libgbm-dev libdrm-dev libvulkan-dev libpng-dev
        glslang-tools bison flex python3-mako python3-yaml python3-venv gdb mingw-w64-tools) ;;
    corpus) base+=(cmake make gcc-mingw-w64-x86-64) ;;
    vkd3d) base+=(gcc-mingw-w64-x86-64 gcc-mingw-w64-i686 g++-mingw-w64-x86-64 g++-mingw-w64-i686
        mingw-w64-tools glslang-tools ninja-build) ;;
    runtime) ;;
    *) echo "unknown dependency profile $profile" >&2; exit 2 ;;
esac
# A runner's apt can stall for the whole job (devices: 60 minutes without output): bound each attempt and try three times.
# timeout wraps apt-get itself so a stalled process is killed (and releases the dpkg lock) before the next attempt.
apt_opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30 -o DPkg::Lock::Timeout=60)
for attempt in 1 2 3; do
    if sudo timeout -k 10 120 apt-get "${apt_opts[@]}" update -qq &&
        sudo DEBIAN_FRONTEND=noninteractive timeout -k 10 480 apt-get "${apt_opts[@]}" install -y -qq --no-install-recommends "${base[@]}"; then break; fi
    [ "$attempt" = 3 ] && { echo "apt install failed three times" >&2; exit 1; }
    echo "apt install stalled or failed: trying once more" >&2
    sudo pkill -x apt-get || true
    sleep 5
done
if [ "$profile" = graphics ] || [ "$profile" = vkd3d ]; then
    pip install --user --break-system-packages -q 'meson==1.9.0' pycotap
    echo "$HOME/.local/bin" >> "$GITHUB_PATH"
fi
if [ "$profile" = build ]; then
    echo /usr/lib/ccache >> "$GITHUB_PATH"
    echo "CCACHE_DIR=$HOME/.ccache" >> "$GITHUB_ENV"
fi
