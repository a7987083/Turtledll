#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic \
  tests/los_pair_cache_regression.cpp src/los_pair_cache.cpp \
  -o "$TMP/los_pair_cache_regression"
"$TMP/los_pair_cache_regression"

grep -Fq 'DEFAULT_TTL_MS = 50U' src/los_pair_cache.h
grep -Fq 'SLOT_COUNT = 128U' src/los_pair_cache.h
grep -Fq 'TysLosPairCache::tryGet' src/dllmain.cpp
grep -Fq 'TysLosPairCache::put' src/dllmain.cpp
grep -Fq 'GetTickCount()' src/dllmain.cpp
grep -Fq 'los_pair_cache.obj' build/build.sh

# LOS1 remains explicit-call-only: no timer/thread/hook was added for LOS.
! grep -Eq 'CreateThread.*(LOS|InSight)|SetTimer.*(LOS|InSight)|MH_CreateHook.*(LOS|InSight)' src/dllmain.cpp src/los_pair_cache.cpp

echo 'LOS_PAIRCACHE50=PASS'
