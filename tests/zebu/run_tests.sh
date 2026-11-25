#!/usr/bin/env bash
# ZEBU Slicer: unit tests of the GUI-independent ZEBU modules (no full build needed).
#   tests/zebu/run_tests.sh [path to an OrcaSlicer checkout's resources/profiles, for the import test]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CXX="${CXX:-g++}"
FLAGS=(-std=c++17 -O1 -Wall -Wextra -I"$ROOT/src/slic3r/Utils" -I"$ROOT/deps_src")

"$CXX" "${FLAGS[@]}" "$ROOT/tests/zebu/test_moonraker.cpp" "$ROOT/src/slic3r/Utils/ZebuMoonraker.cpp" -o "$OUT/t_moon"
"$OUT/t_moon"
"$CXX" "${FLAGS[@]}" "$ROOT/tests/zebu/test_update.cpp" "$ROOT/src/slic3r/Utils/ZebuUpdate.cpp" -o "$OUT/t_upd"
"$CXX" "${FLAGS[@]}" -O2 -I"$ROOT/src" "$ROOT/tests/zebu/test_curved_tops.cpp" "$ROOT/src/libslic3r/Zebu/CurvedTops.cpp" \
    "$ROOT/src/libslic3r/Zebu/HeadUnderside.cpp" -o "$OUT/t_curved"
"$OUT/t_curved" "$ROOT/resources/zebu/heads"
"$OUT/t_upd"
"$CXX" "${FLAGS[@]}" -O2 -I"$ROOT/src" "$ROOT/tests/zebu/test_curved_tops.cpp" "$ROOT/src/libslic3r/Zebu/CurvedTops.cpp" \
    "$ROOT/src/libslic3r/Zebu/HeadUnderside.cpp" -o "$OUT/t_curved"
"$OUT/t_curved" "$ROOT/resources/zebu/heads"

if [ $# -ge 1 ]; then
    # Fake OrcaSlicer data folder: <profiles> as its "system" copy + a few user presets.
    FAKE="$OUT/orca"
    mkdir -p "$FAKE/user/default/machine" "$FAKE/user/default/filament" "$FAKE/user/default/process"
    ln -s "$(cd "$1" && pwd)" "$FAKE/system"
    cp "$ROOT/tests/zebu/data/machine/"*.json "$FAKE/user/default/machine/"
    cp "$ROOT/tests/zebu/data/filament/"*.json "$FAKE/user/default/filament/"
    cp "$ROOT/tests/zebu/data/process/"*.json "$FAKE/user/default/process/"
    cp "$ROOT/tests/zebu/data/OrcaSlicer.conf" "$FAKE/"
    "$CXX" "${FLAGS[@]}" "$ROOT/tests/zebu/test_orca_import.cpp" "$ROOT/src/slic3r/Utils/ZebuOrcaImport.cpp" -o "$OUT/t_imp"
    "$OUT/t_imp" "$FAKE" "$OUT/zebu_user"
fi
echo "ZEBU tests passed"
