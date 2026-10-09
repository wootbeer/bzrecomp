# One-time setup on Windows, before opening the project in Visual Studio:
#   1. converts your ROM dump to the .z64 the tools read (if needed),
#   2. applies lib-patches/ to the submodules,
#   3. builds N64Recomp and RSPRecomp from the patched submodule,
#   4. generates RecompiledFuncs/ from the ROM.
# Rerun it after pulling changes to the symbol file, the recomp config
# (battlezone.us.rev0.toml) or lib-patches/.
#
# Run from any PowerShell in the repo root (it sets up Visual Studio's x64
# build environment itself):
#
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#   .\tools\setup-windows.ps1 -Rom "C:\path\to\Battlezone - Rise of the Black Dogs (USA).n64"
#
# -Rom can be omitted once the .z64 is in the repo root.
param(
    [string]$Rom = "",
    [string]$ClangCl = $env:BZ_CLANGCL
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$romName = "Battlezone - Rise of the Black Dogs (USA).z64"

# Visual Studio's x64 build environment (compiler, Windows SDK, Ninja, CMake).
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio not found (no vswhere.exe)." }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "No Visual Studio install with the C++ tools found." }
if ($env:VSCMD_ARG_TGT_ARCH -ne "x64") {
    & (Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1") -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}
if (-not $ClangCl) { $ClangCl = Join-Path $vsPath "VC\Tools\Llvm\x64\bin\clang-cl.exe" }
if (-not (Test-Path $ClangCl)) {
    throw "clang-cl not found at $ClangCl -- install the 'C++ Clang Compiler for Windows' component, or pass -ClangCl <path>."
}

Set-Location $root

if (-not (Test-Path "lib\N64ModernRuntime\N64Recomp\CMakeLists.txt")) {
    throw "Submodules are missing -- run: git submodule update --init --recursive"
}

# 1. The ROM, as big-endian .z64 (tools\normalize_rom.py detects the byte order).
if ($Rom) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $python) { throw "Python 3 is needed to convert the ROM (or convert it yourself and put it in the repo root as '$romName')." }
    & $python.Source tools\normalize_rom.py $Rom $romName
    if ($LASTEXITCODE -ne 0) { throw "ROM conversion failed." }
}
if (-not (Test-Path $romName)) { throw "'$romName' not found in the repo root -- pass -Rom <your dump>." }

# 2. Submodule patches (each applies once; reruns skip them).
cmake -P cmake\apply_lib_patches.cmake
if ($LASTEXITCODE -ne 0) { throw "Applying lib-patches failed." }

# 3. The recompilers.
$recompBuild = "lib\N64ModernRuntime\N64Recomp\build"
cmake -S lib\N64ModernRuntime\N64Recomp -B $recompBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$ClangCl" "-DCMAKE_CXX_COMPILER=$ClangCl"
if ($LASTEXITCODE -ne 0) { throw "N64Recomp configure failed." }
cmake --build $recompBuild --target N64RecompCLI RSPRecomp
if ($LASTEXITCODE -ne 0) { throw "N64Recomp build failed." }

# 4. The recompiled game code. (The main build runs RSPRecomp for the audio
#    microcode and builds the patch library itself.)
& "$recompBuild\N64Recomp.exe" battlezone.us.rev0.toml | Out-File -Encoding utf8 "N64Recomp.log"
if ($LASTEXITCODE -ne 0) { Get-Content N64Recomp.log -Tail 20; throw "N64Recomp failed (see N64Recomp.log)." }

Write-Host ""
Write-Host "Done. Now open this folder in Visual Studio (File > Open > Folder),"
Write-Host "pick the 'Windows x64 Debug (clang-cl)' configuration, choose"
Write-Host "BattlezoneRecompiled.exe as the startup item, then build and press F5."
