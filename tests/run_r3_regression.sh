#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./tests/run_core_regression.sh
./tests/run_r1_regression.sh

# R2 invariants retained under R3: one AuraSourceCore, FIFO, no legacy owner store.
if grep -RInE 'TysAuraCasterCore|aura_caster_core|usedSlotMask|OwnerMemory|ownerMemory' src build; then
  echo 'R3_FAIL=LEGACY_CASTER_CORE_PRESENT' >&2
  exit 1
fi
grep -q 'findOldestPending' src/aura_source_core.cpp
grep -q 'p.serial < best->serial' src/aura_source_core.cpp
grep -q 'TARGET_GUID_PLUS_SPELL_ID_PLUS_CASTER_GUID' src/aura_caster.cpp

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic \
  tests/combo_duration_core_regression.cpp src/combo_duration_core.cpp \
  -o /tmp/aura6d4_r3_combo_test
/tmp/aura6d4_r3_combo_test
rm -f /tmp/aura6d4_r3_combo_test

grep -q 'CMSG_CAST_SPELL_OPCODE' src/aura_cast_timing.cpp
grep -q 'TIME_SOURCE_LOCAL_COMBO_SCALED' src/aura_cast_timing.cpp
grep -q 'PREDICTED_EXPIRED_AURA_STILL_PRESENT' src/aura_state.cpp
grep -q 'targetPredictedRemainingAvailable' src/aura_state.cpp
grep -q 'setString(L, "name", spellName(spellId))' src/aura_state.cpp
grep -q 'AURA6-D4-R3' src/aura_state.cpp
grep -q '1.4.0-AURA6D4-R3' src/dllmain.cpp
grep -q 'API_VERSION = "28"' src/dllmain.cpp
grep -q '组合点计时已工作' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.lua
grep -q 'Version: 3.0.0-AURA6D4-R3-CN' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.toc
if grep -RInE 'Aura.TargetDuration|TargetDurationCore|aura_target_duration' src build; then
  echo 'R3_FAIL=SECOND_TARGET_DURATION_STORE_PRESENT' >&2
  exit 1
fi

echo 'AURA6_D4_R3_TARGET_DURATION_ARCH=PASS'
