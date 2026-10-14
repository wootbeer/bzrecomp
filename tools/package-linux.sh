#!/usr/bin/env bash
# Builds a Release copy of the game for Linux (and Steam Deck) and packages it
# as an AppImage, in a .tar.gz with the README, license and third-party
# license notices -- the Linux counterpart of tools/package-windows.ps1.
#
# Run from the repo root after the usual one-time steps (N64Recomp built and
# run, so RecompiledFuncs/, RecompiledPatches/ and rsp/ exist):
#
#   tools/package-linux.sh 0.1.0-beta
#
# Output: dist/BattlezoneRecompiled-<version>-linux.tar.gz. Needs clang, cmake,
# ninja and the Linux build dependencies (see BUILDING.md); downloads
# linuxdeploy into the build folder the first time.
#
# The AppImage bundles the libraries the game links (SDL2 and the like) but
# uses the system's GTK, glib and graphics stack, which every desktop Linux
# and SteamOS has. It runs on systems with a glibc at least as new as the one
# it's built on.
set -euo pipefail

version="${1:-beta}"
build_dir="build-release-linux"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

for required in RecompiledFuncs RecompiledPatches rsp; do
    [ -d "$required" ] || { echo "$required/ is missing -- run the N64Recomp / RSPRecomp steps first." >&2; exit 1; }
done

cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build "$build_dir" --target BattlezoneRecompiled

linuxdeploy="$build_dir/linuxdeploy-x86_64.AppImage"
if [ ! -x "$linuxdeploy" ]; then
    curl -fL -o "$linuxdeploy" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$linuxdeploy"
fi

appdir="$build_dir/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/128x128/apps"
cp "$build_dir/BattlezoneRecompiled" "$appdir/usr/bin/"
cp -r assets "$appdir/usr/bin/"
cp icons/app.png "$appdir/usr/share/icons/hicolor/128x128/apps/BattlezoneRecompiled.png"
cat > "$appdir/usr/share/applications/BattlezoneRecompiled.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Battlezone: Rise of the Black Dogs Recompiled
Exec=BattlezoneRecompiled
Icon=BattlezoneRecompiled
Categories=Game;
Terminal=false
EOF
# The game loads assets/ relative to the working directory.
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here="$(dirname "$(readlink -f "$0")")"
cd "$here/usr/bin" || exit 1
exec ./BattlezoneRecompiled "$@"
EOF
chmod +x "$appdir/AppRun"

# The system's GTK stack (the ROM file picker) stays the system's: a bundled
# copy breaks on themes and pixbuf loaders.
exclude=()
for lib in 'libgtk-3.so*' 'libgdk-3.so*' 'libgdk_pixbuf-2.0.so*' 'libglib-2.0.so*' 'libgio-2.0.so*' \
           'libgobject-2.0.so*' 'libgmodule-2.0.so*' 'libpango*.so*' 'libcairo*.so*' 'libatk*.so*' \
           'libatspi.so*' 'libepoxy.so*' 'libharfbuzz.so*' 'libfribidi.so*' 'libthai.so*' 'libdatrie.so*' \
           'libgraphite2.so*' 'libpixman-1.so*' 'libmount.so*' 'libblkid.so*' 'libselinux.so*' \
           'libffi.so*' 'libpcre2-8.so*' 'libdbus-1.so*' 'libsystemd.so*'; do
    exclude+=(--exclude-library "$lib")
done

stage="dist/BattlezoneRecompiled-$version-linux"
rm -rf "$stage"
mkdir -p "$stage/licenses"
(
    export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 LDAI_OUTPUT="$root/$stage/BattlezoneRecompiled-x86_64.AppImage"
    "$root/$linuxdeploy" --appdir "$appdir" "${exclude[@]}" \
        --desktop-file "$appdir/usr/share/applications/BattlezoneRecompiled.desktop" \
        --icon-file "$appdir/usr/share/icons/hicolor/128x128/apps/BattlezoneRecompiled.png" \
        --output appimage
)

cp README.md COPYING "$stage/"
# License notices for the third-party code built into the game or bundled.
while read -r name path; do
    if [ -f "$path" ]; then cp "$path" "$stage/licenses/$name.txt"; else echo "License file not found: $path" >&2; fi
done <<'EOF'
N64ModernRuntime lib/N64ModernRuntime/COPYING
N64Recomp lib/N64ModernRuntime/N64Recomp/LICENSE
RT64 lib/rt64/LICENSE
plume lib/rt64/src/contrib/plume/LICENSE
hlslpp lib/rt64/src/contrib/hlslpp/LICENSE
imgui lib/rt64/src/contrib/imgui/LICENSE.txt
implot lib/rt64/src/contrib/implot/LICENSE
im3d lib/rt64/src/contrib/im3d/LICENSE
ddspp lib/rt64/src/contrib/ddspp/LICENSE
nativefiledialog-extended lib/rt64/src/contrib/nativefiledialog-extended/LICENSE
stb lib/rt64/src/contrib/stb/LICENSE
xxHash lib/rt64/src/contrib/xxHash/LICENSE
zstd lib/rt64/src/contrib/zstd/LICENSE
re-spirv lib/rt64/src/contrib/re-spirv/LICENSE
miniz lib/N64ModernRuntime/thirdparty/miniz/LICENSE
o1heap lib/N64ModernRuntime/thirdparty/o1heap/LICENSE
RmlUi lib/RecompFrontend/recompui/lib/RmlUi/LICENSE.txt
lunasvg lib/RecompFrontend/recompui/lib/lunasvg/LICENSE
plutovg lib/RecompFrontend/recompui/lib/lunasvg/plutovg/LICENSE
FreeType lib/RecompFrontend/recompui/lib/freetype-windows-binaries/LICENSE.TXT
GamepadMotionHelpers lib/RecompFrontend/lib/GamepadMotionHelpers/LICENSE
SlotMap lib/SlotMap/README.md
SDL2 lib/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt
EOF

tar -C dist -czf "dist/BattlezoneRecompiled-$version-linux.tar.gz" "BattlezoneRecompiled-$version-linux"
echo "dist/BattlezoneRecompiled-$version-linux.tar.gz"
