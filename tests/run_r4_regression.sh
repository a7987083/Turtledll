#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

./tests/run_core_regression.sh
./tests/run_r1_regression.sh

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic \
  tests/combo_duration_core_regression.cpp src/combo_duration_core.cpp \
  -o /tmp/aura6d4_r4_combo_test
/tmp/aura6d4_r4_combo_test
rm -f /tmp/aura6d4_r4_combo_test

# Final lifecycle invariants.
grep -q 'refreshOnly' src/aura_source_core.cpp
grep -q 'refreshPendingWrites' src/aura_source_core.h
grep -q 'staleRemoveIgnores' src/aura_source_core.cpp
grep -q 'resetWorldState' src/aura_source_core.cpp
grep -q 'TysAuraCaster::onWorldLeaving' src/dllmain.cpp
grep -q 'TysComboDurationCore::resetWorldState' src/aura_cast_timing.cpp
grep -q 'R4_REFRESH_FIFO_PLUS_WORLD_RESET_PLUS_STALE_REMOVE_GUARD' src/aura_caster.cpp
grep -q '1.4.0-AURA6D4-R4' src/dllmain.cpp
grep -q 'API_VERSION = "29"' src/dllmain.cpp
grep -q 'Version: 4.0.0-AURA6D4-R4-CN' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.toc
grep -q '刷新：割裂还没消失时再上一次' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.lua

if grep -RInE 'TysAuraCasterCore|aura_caster_core|usedSlotMask|OwnerMemory|ownerMemory|Aura.TargetDuration|TargetDurationCore|aura_target_duration' src build; then
  echo 'R4_FAIL=LEGACY_OR_SECOND_AURA_STORE_PRESENT' >&2
  exit 1
fi

echo 'AURA6_D4_R4_FINAL_LIFECYCLE_ARCH=PASS'
