#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./tests/run_core_regression.sh
./tests/run_r1_regression.sh
if grep -RInE 'TysAuraCasterCore|aura_caster_core|usedSlotMask|OwnerMemory|ownerMemory' src build; then
  echo 'R2_FAIL=LEGACY_CASTER_CORE_PRESENT' >&2
  exit 1
fi
grep -q 'findOldestPending' src/aura_source_core.cpp
grep -q 'p.serial < best->serial' src/aura_source_core.cpp
grep -q 'Query is read-side only' src/aura_source_core.h
grep -q 'TARGET_GUID_PLUS_SPELL_ID_PLUS_CASTER_GUID' src/aura_caster.cpp
grep -q 'READ_SIDE_NEVER_CONSUMES_PENDING' src/aura_caster.cpp
grep -q 'cleanupVisibleInstances' src/aura_state.cpp
grep -q 'Aura.Source.Status' src/dllmain.cpp
grep -q 'Aura.Source.Status' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.lua
grep -q 'R2 完整通过' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.lua
grep -q 'Version: 2.0.0-AURA6D4-R2-CN' AddOn/TaiYangAuraDiag/TaiYangAuraDiag.toc
echo 'AURA6_D4_R2_ARCH_REGRESSION=PASS'
