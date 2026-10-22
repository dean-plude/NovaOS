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
# A runner's apt can stall for a whole job (devices: 60 minutes without output, twice in a row
# on 10-22). So: APT_DEB_CACHE (.github/actions/deps, an actions/cache keyed by runner image)
# keeps the .debs a profile downloaded and later jobs install them with dpkg, no network;
# apt itself gets connection timeouts and retries, each attempt is bounded and tried twice,
# and a stalled attempt prints which apt/dpkg processes were stuck.
cache=${APT_DEB_CACHE:-}
apt_opts=(-q -o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30
          -o DPkg::Lock::Timeout=120)
if [ -n "$cache" ]; then
    mkdir -p "$cache"
    apt_opts+=(-o Dir::Cache::archives="$cache")
fi
from_cache() {
    [ -n "$cache" ] && compgen -G "$cache/*.deb" >/dev/null || return 1
    echo "installing $(ls "$cache"/*.deb | wc -l) cached packages"
    timeout 600 sudo DEBIAN_FRONTEND=noninteractive dpkg -i "$cache"/*.deb >/dev/null
}
apt_install() {
    timeout 300 sudo apt-get "${apt_opts[@]}" update &&
        timeout 600 sudo DEBIAN_FRONTEND=noninteractive apt-get "${apt_opts[@]}" install -y -f \
            --no-install-recommends "${base[@]}"
}
if ! from_cache; then
    for attempt in 1 2; do
        if apt_install; then break; fi
        echo "apt install stalled or failed (attempt $attempt); apt and dpkg processes:" >&2
        ps -eo pid,etime,stat,args | grep -E '[a]pt|[d]pkg|[h]ttp' >&2 || true
        [ "$attempt" = 2 ] && { echo "apt install failed twice" >&2; exit 1; }
        sudo pkill -x apt-get || true
        sudo dpkg --configure -a || true
    done
    if [ -n "$cache" ]; then
        sudo rm -rf "$cache/partial" "$cache/lock"
        sudo chown -R "$(id -u):$(id -g)" "$cache"
    fi
fi
if [ "$profile" = graphics ] || [ "$profile" = vkd3d ]; then
    pip install --user --break-system-packages -q 'meson==1.9.0' pycotap
    echo "$HOME/.local/bin" >> "$GITHUB_PATH"
fi
if [ "$profile" = build ]; then
    echo /usr/lib/ccache >> "$GITHUB_PATH"
    echo "CCACHE_DIR=$HOME/.ccache" >> "$GITHUB_ENV"
fi
