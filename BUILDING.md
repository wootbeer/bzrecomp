# Building Guide

You need your own copy of the game: Battlezone: Rise of the Black Dogs, USA
(hashes in the README). The ROM is never committed to this repository; the
recompiled code is generated from it locally.

## 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/wootbeer/bzrecomp.git
# if you forgot --recurse-submodules:
git submodule update --init --recursive
```

This pulls in `lib/N64ModernRuntime`, `lib/RecompFrontend` and `lib/rt64`
(plus rt64's own nested submodules), several hundred MB in all.

## 2. Install dependencies

### Windows

- Visual Studio 2022 with the "Desktop development with C++" workload,
  including the "C++ Clang Compiler for Windows" and "C++ CMake tools for
  Windows" components.
- WSL with `clang`, `lld` and `make` installed in it (for Ubuntu:
  `sudo apt install clang lld make`). The small MIPS patch library in
  `patches/` has to be cross-compiled for the N64, and the Windows builds of
  clang don't include the MIPS backend, so CMake runs that one step through
  `wsl.exe`.

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install cmake ninja-build libsdl2-dev libgtk-3-dev libvulkan-dev lld llvm clang
```

## 3. Put the ROM in place

Place your dump at the repo root, named exactly
`Battlezone - Rise of the Black Dogs (USA).z64`. It must be big-endian `.z64`
(header bytes `80 37 12 40`). To convert a `.n64` or `.v64` dump:

```bash
python3 tools/normalize_rom.py "your dump.n64" "Battlezone - Rise of the Black Dogs (USA).z64"
```

## 4. Patch the submodules and build the recompiler

This port carries a few changes to its submodules in `lib-patches/`, and
N64Recomp needs its own before it can recompile the game (MIPS divide-by-zero
results, and jump tables it can't decode by itself). Apply them, then build
N64Recomp from the patched submodule:

```bash
cmake -P cmake/apply_lib_patches.cmake
cmake -S lib/N64ModernRuntime/N64Recomp -B lib/N64ModernRuntime/N64Recomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build lib/N64ModernRuntime/N64Recomp/build --target N64RecompCLI RSPRecomp
```

(On Windows, add `-DCMAKE_C_COMPILER=...clang-cl.exe -DCMAKE_CXX_COMPILER=...clang-cl.exe`
as in btgarecomp's guide, and run from an x64 Developer PowerShell.)

## 5. Generate the recompiled code

```bash
./lib/N64ModernRuntime/N64Recomp/build/N64Recomp battlezone.us.rev0.toml
```

This writes `RecompiledFuncs/` (gitignored). The main build generates the RSP
audio microcode (`rsp/`, via RSPRecomp) and the patch library itself.

## 6. Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target BattlezoneRecompiled
./build/BattlezoneRecompiled
```

Run it from a folder containing `assets/` (the repo root works): the game loads
`assets/` relative to the working directory. On first launch it asks for the
ROM and checks it against this release's hash (XXH3-64 `0xf5f240b373301b9d` of
the normalized file).

## Regenerating the analysis (developers)

The symbol file (`BattlezoneSyms/`) and the generated tail of
`battlezone.us.rev0.toml` come from the tools in `tools/`, run over the ROM:

```bash
# n64sym (github.com/shygoo/n64sym) on the boot segment, for libultra names
python3 -c "r=open('Battlezone - Rise of the Black Dogs (USA).z64','rb').read(); open('seg1.bin','wb').write(r[0x1000:0x4a540])"
n64sym seg1.bin -s -h 0x80028400 > n64sym_boot.txt
tools/regen.sh n64sym_boot.txt
```

`tools/regen.sh` runs the function finder (`find_functions.py`), rewrites the
generated config (`update_config.py`: busy-wait hooks and jump tables) and runs
N64Recomp. `pip install rabbitizer` is the only Python dependency.
