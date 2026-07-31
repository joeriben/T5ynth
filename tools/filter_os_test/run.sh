#!/usr/bin/env bash
# Build + run the filter aliasing test and CPU benchmark, then analyze.
# Drives the REAL LadderFilter.h / CutoffWarpFilter.h offline (JUCE-free
# shim) at base rate vs 2×/4×/8× oversampled. See HANDOVER_FILTER_OVERSAMPLING.md.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out

CXXFLAGS="-std=c++17 -O2 -I . -I ../../src/dsp"

echo "── building ──"
g++ $CXXFLAGS -ffast-math filter_os_test.cpp -o filter_os_test
g++ $CXXFLAGS              filter_bench.cpp   -o filter_bench

echo "── rendering WAVs (out/) ──"
./filter_os_test out

echo "── aliasing metric + spectrograms ──"
python3 analyze.py out

echo "── CPU benchmark ──"
./filter_bench

echo "── done. WAVs + PNGs in out/ ──"
