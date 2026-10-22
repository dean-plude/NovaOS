#!/usr/bin/env bash
# Stage what tools/selftest.py --suite graphics puts on drive C::
#   DIR/7zip       7-Zip (C:\Programs\7-Zip), which the App Store unpacks with
#   DIR/downloads  the Mesa 3D and DXVK archives the App Store lists, and
#                  Venus's venus.7z (tools/build_venus.py; VENUS_7Z names one
#                  already built) and VKD3D-Proton's vkd3d-proton.7z
#                  (tools/build_vkd3d_proton.py; VKD3D_7Z names one already
#                  built) (C:\Downloads: the Store installs them without a
#                  network)
#   DIR/tests      tools/gltest, tools/d3dtest, tools/d3d12test, tools/hlsltest, tools/d2dtest, tools/dwtest, tools/dw3test and tools/dw6test,
#                  64- and 32-bit,
#                  and d2dtest's reference image (C:\Tests)
# Needs curl, 7z (p7zip-full) and MinGW-w64 (x86-64 and i686), and what
# tools/build_venus.py and tools/build_vkd3d_proton.py need unless VENUS_7Z
# and VKD3D_7Z are set.
#     tools/ci/stage-graphics.sh DIR [CACHE]
# CACHE (default DIR/cache) keeps the downloads between runs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$1"
CACHE="${2:-$OUT/cache}"
mkdir -p "$OUT/7zip" "$OUT/downloads" "$OUT/tests" "$CACHE"

# Resolve runtime downloads through the same JSON catalog as Store fixtures.
# Assignment propagates catalog lookup errors instead of an empty grep result.
RUNTIME_URLS_TEXT=$(python3 - "$ROOT" <<'PYTHON'
import sys
sys.path.insert(0, sys.argv[1] + '/tools')
from appcorpus import catalog_entry
for name in ('Mesa 3D', 'DXVK'):
    print(catalog_entry(name)[0])
PYTHON
)
mapfile -t RUNTIME_URLS <<< "$RUNTIME_URLS_TEXT"
URLS=("https://github.com/ip7z/7zip/releases/download/26.03/7z2603-x64.exe"
      "${RUNTIME_URLS[@]}")
for u in "${URLS[@]}"; do
  f="$CACHE/$(basename "$u")"
  [ -s "$f" ] || curl -sSLf --retry 4 -o "$f" "$u"
done
7z x -y -o"$OUT/7zip" "$CACHE/$(basename "${URLS[0]}")" >/dev/null
for u in "${RUNTIME_URLS[@]}"; do cp "$CACHE/$(basename "$u")" "$OUT/downloads/"; done
if [ -z "${VENUS_7Z:-}" ]; then
  python3 "$ROOT/tools/build_venus.py" "$CACHE/venus"
  VENUS_7Z="$CACHE/venus/venus.7z"
fi
cp "$VENUS_7Z" "$OUT/downloads/venus.7z"
if [ -z "${VKD3D_7Z:-}" ]; then
  python3 "$ROOT/tools/build_vkd3d_proton.py" "$CACHE/vkd3d"
  VKD3D_7Z="$CACHE/vkd3d/vkd3d-proton.7z"
fi
cp "$VKD3D_7Z" "$OUT/downloads/vkd3d-proton.7z"

for arch in x86_64 i686; do
  sfx=""
  if [ "$arch" = i686 ]; then sfx=32; fi
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/gltest$sfx.exe" "$ROOT/tools/gltest/gltest.c" \
    -lopengl32 -lgdi32 -luser32 -lm
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/d3dtest$sfx.exe" "$ROOT/tools/d3dtest/d3dtest.c" \
    -ld3d9 -ld3d11 -ld3dcompiler_47 -ldxgi -luser32 -lgdi32 -lole32
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/d3d12test$sfx.exe" "$ROOT/tools/d3d12test/d3d12test.c" \
    -ld3dcompiler_47 -ldxgi -ldxguid -luser32 -lgdi32
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/hlsltest$sfx.exe" "$ROOT/tools/hlsltest/hlsltest.c" \
    -ld3dcompiler_47 -ld3d11 -ldxguid
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/d2dtest$sfx.exe" "$ROOT/tools/d2dtest/d2dtest.c" \
    -ld2d1 -luser32 -lgdi32 -luuid
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/dwtest$sfx.exe" "$ROOT/tools/dwtest/dwtest.c" \
    -ldwrite -lgdi32 -luser32
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/dw3test$sfx.exe" "$ROOT/tools/dw3test/dw3test.c" -ldwrite
  $arch-w64-mingw32-gcc -O2 -o "$OUT/tests/dw6test$sfx.exe" "$ROOT/tools/dw6test/dw6test.c" -ldwrite
done
python3 "$ROOT/tools/d2dtest/png2bmp.py" "$ROOT/tools/d2dtest/d2dref.png" "$OUT/tests/d2dref.bmp"
ls -l "$OUT/downloads" "$OUT/tests"
