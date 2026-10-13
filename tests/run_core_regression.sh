#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/tys_aura6_source_core_regression"
g++ -std=c++17 -O2 aura_source_core_regression.cpp ../src/aura_source_core.cpp -o "$OUT"
"$OUT"
