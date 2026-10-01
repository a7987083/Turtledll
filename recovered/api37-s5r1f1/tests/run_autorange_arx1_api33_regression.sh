#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BASE=ebd07508bb09d2b6a06cae9cb5f7d271258cbe51

./tests/run_core_regression.sh
./tests/run_r1_regression.sh

# Frozen native cores must remain byte-identical to the LFX1/API32 baseline.
git diff --quiet "$BASE" -- \
  src/aura_native.cpp src/aura_native.h \
  src/aura_state.cpp src/aura_state.h \
  src/aura_duration.cpp src/aura_duration.h \
  src/aura_source_core.cpp src/aura_source_core.h \
  src/aura_caster.cpp src/aura_caster.h \
  src/aura_cast_timing.cpp src/aura_cast_timing.h \
  src/combo_duration_core.cpp src/combo_duration_core.h \
  src/native_bus.cpp src/native_bus.h \
  src/spell_cast_core.cpp src/spell_cast_core.h \
  src/corpse_marker.cpp src/corpse_marker.h

grep -Fq 'DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2-LFX1-ARX1-DW1-LOS1"' src/dllmain.cpp
grep -Fq 'API_VERSION = "33"' src/dllmain.cpp
grep -Fq '20260830-v140-arx1-api33-dw1-los-paircache50' src/dllmain.cpp

grep -Fq 'Unit.Guid' src/dllmain.cpp
grep -Fq 'Unit.InSight' src/dllmain.cpp
grep -Fq 'GroundProbe.UnitStateByGuid' src/dllmain.cpp
grep -Fq 'CWORLD_INTERSECT = 0x00672170UL' src/wow112_offsets.h
grep -Fq 'CWORLD_INTERSECT_LOS_FLAGS = 0x00100111UL' src/wow112_offsets.h
grep -Fq 'OFF_MOVEMENT_COLLISION_HEIGHT = 0xB4UL' src/wow112_offsets.h

# ARX1/LOS1 remains explicit-call-only: no extra hook owner, timer, or worker.
[ "$(grep -c 'MH_CreateHook' src/dllmain.cpp)" -eq "$(git show "$BASE":src/dllmain.cpp | grep -c 'MH_CreateHook')" ]
! grep -Eq 'CreateThread.*InSight|SetTimer.*InSight' src/dllmain.cpp src/los_pair_cache.cpp
grep -Fq 'DEFAULT_TTL_MS = 50U' src/los_pair_cache.h

# UnitState reads the canonical 1.12.1 global UpdateFields indices.
grep -Fq 'UNIT_FIELD_HEALTH_INDEX = 0x16UL' src/wow112_offsets.h
grep -Fq 'UNIT_FIELD_MAXHEALTH_INDEX = 0x1CUL' src/wow112_offsets.h
grep -Fq 'UNIT_DYNAMIC_FLAGS_INDEX = 0x8FUL' src/wow112_offsets.h
grep -Fq 'UNIT_DYNFLAG_DEAD = 0x20UL' src/wow112_offsets.h

# CAST1-R2 architecture invariants still present.
grep -q 'subscribeIncoming(&onIncoming)' src/spell_cast_core.cpp
grep -q 'subscribeOutgoing(&onOutgoing)' src/spell_cast_core.cpp
grep -q 'subscribeWorldTick(&onTick)' src/spell_cast_core.cpp
[ "$(grep -c 'MH_CreateHook' src/spell_cast_core.cpp)" -eq 1 ]
grep -q 'setBool(L, "objectManagerPolling", false)' src/spell_cast_core.cpp

# LootFX remains unchanged from API32.
grep -Fq 'CORPSE_CREATE_MODEL_CALLSITE = 0x0061FA6AUL' src/corpse_marker.cpp
grep -Fq 'GATHER_CREATE_MODEL_CALLSITE = 0x0061FC9FUL' src/corpse_marker.cpp

echo 'AUTORANGE_ARX1_API33=PASS'

# AutoRange Stage2.31 no longer calls external DLL surfaces.
grep -Fq 'Stage2.31-ARX1-API33-Independent' AddOn/AutoRange/AutoRange.toc
grep -Fq 'Unit.Guid' AddOn/AutoRange/AutoRange.lua
grep -Fq 'Unit.InSight' AddOn/AutoRange/AutoRange.lua
grep -Fq 'GroundProbe.UnitStateByGuid' AddOn/AutoRange/AutoRange.lua
grep -Fq 'TYS_CAST_START' AddOn/AutoRange/AutoRange.lua
! grep -Eq 'UNIT_CASTEVENT|UnitExists[[:space:]]*\(|UnitIsDead[[:space:]]*\(|(^|[^A-Za-z0-9_])UnitPosition[[:space:]]*\(|UnitXP[[:space:]]*\(|GetPlayerFacing[[:space:]]*\(|UnitFacing[[:space:]]*\(|SuperAPI' AddOn/AutoRange/AutoRange.lua AddOn/AutoRange/AutoRange.toc AddOn/AutoRange/FuBar.lua
texluac -p AddOn/AutoRange/AutoRange.lua
texluac -p AddOn/AutoRange/FuBar.lua
grep -Fq 'DYNLOS50' AddOn/TaiYangAutoRangeARXDiag/TaiYangAutoRangeARXDiag.toc
grep -Fq 'LOS every 50 ms' AddOn/TaiYangAutoRangeARXDiag/TaiYangAutoRangeARXDiag.lua || grep -Fq 'every 50 ms' AddOn/TaiYangAutoRangeARXDiag/TaiYangAutoRangeARXDiag.lua
texluac -p AddOn/TaiYangAutoRangeARXDiag/TaiYangAutoRangeARXDiag.lua

echo 'AUTORANGE_ARX1_ADDON_INDEPENDENCE=PASS'
