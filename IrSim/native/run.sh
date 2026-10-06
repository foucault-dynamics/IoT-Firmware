#!/bin/sh
# Builds the firmware's real Iec6205621Reader and SimulatedIrHead for the
# laptop, with a tiny stand-in for the Arduino core (shim/), and runs them
# against fake meters. Needs g++ (MSYS2 on Windows, or any Linux/macOS g++).
#
#   sh IrSim/native/run.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
LIB="$HERE/../../lib"
OUT="${TMPDIR:-/tmp}/ir_reader_test"
g++ -std=c++17 -I"$HERE/shim" -I"$LIB/iec62056_21" -I"$LIB/ir_head" -I"$LIB/module" \
    -I"$LIB/node_config" -I"$LIB/reader" \
    "$HERE/reader_test.cpp" "$LIB/iec62056_21/iec62056_21.cpp" "$LIB/ir_head/simulated_ir_head.cpp" \
    -o "$OUT"
"$OUT"
