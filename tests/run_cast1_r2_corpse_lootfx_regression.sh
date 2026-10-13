#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BASE=c5855262c5ed768f2b981e9da6c0bd6d9e38b1f7

git diff --quiet "$BASE" -- src/spell_cast_core.cpp src/spell_cast_core.h

grep -Fq 'constexpr const char* DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2";' src/dllmain.cpp
grep -Fq 'constexpr const char* API_VERSION = "31";' src/dllmain.cpp
grep -Fq 'CORPSE_CREATE_MODEL_CALLSITE = 0x0061FA6AUL' src/corpse_marker.cpp
grep -Fq 'CORPSE_FLAG_MDL = "Particles\\TaiYangCorpse\\LootFX.mdl"' src/corpse_marker.cpp
! grep -q '61FC9F' src/corpse_marker.cpp
! grep -q 'ObjectManager' src/corpse_marker.cpp
! grep -q 'CorpseMarker.Status' src/dllmain.cpp

echo 'CAST1_R2_CORPSE_LOOTFX_STABLE=PASS'
