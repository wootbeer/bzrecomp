#!/bin/sh
# Regenerate the symbol file, the generated part of the recomp config, and
# RecompiledFuncs/ from the ROM.
# Usage: tools/regen.sh <n64sym output for the boot segment>
set -e
cd "$(dirname "$0")/.."
ROM="Battlezone - Rise of the Black Dogs (USA).z64"
mkdir -p build
python3 tools/find_functions.py "$ROM" "$1" BattlezoneSyms/battlezone.us.rev0.syms.toml build/find_functions_report.txt
python3 tools/update_config.py "$ROM"
./lib/N64ModernRuntime/N64Recomp/build/N64Recomp battlezone.us.rev0.toml
