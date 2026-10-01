#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

./tests/run_core_regression.sh
./tests/run_r1_regression.sh

# R1 transport invariants stay frozen: shared NativeBus + one ClearCastingSpell hook.
grep -q 'subscribeIncoming(&onIncoming)' src/spell_cast_core.cpp
grep -q 'subscribeOutgoing(&onOutgoing)' src/spell_cast_core.cpp
grep -q 'subscribeWorldTick(&onTick)' src/spell_cast_core.cpp
if [ "$(grep -c 'MH_CreateHook' src/spell_cast_core.cpp)" -ne 1 ]; then
  echo 'CAST1_R2_FAIL=UNEXPECTED_DIRECT_HOOK_COUNT' >&2
  exit 1
fi
grep -q 'chainedTarget(WoW112::UNIT_CLEAR_CASTING_SPELL)' src/spell_cast_core.cpp
grep -q 'setBool(L, "perOpcodeHooks", false)' src/spell_cast_core.cpp
grep -q 'setBool(L, "objectManagerPolling", false)' src/spell_cast_core.cpp

# Dynamic FrameScript event ownership from FIX1 remains mandatory.
grep -q 'claimEventSlot' src/spell_cast_core.cpp
grep -q 'if (data\[i \* 4\] != 0) continue' src/spell_cast_core.cpp
grep -q 'setBool(L, "dynamicEventSlots", true)' src/spell_cast_core.cpp
if grep -q 'data\[id \* 4\]' src/spell_cast_core.cpp; then
  echo 'CAST1_R2_FAIL=FIXED_EVENT_SLOT_WRITE_PRESENT' >&2
  exit 1
fi

# Unified CastState: live phase and durable terminal result are separate axes.
grep -q 'enum Result' src/spell_cast_core.cpp
grep -q 'RESULT_SUCCESS' src/spell_cast_core.cpp
grep -q 'RESULT_INTERRUPTED' src/spell_cast_core.cpp
grep -q 'RESULT_EXPIRED' src/spell_cast_core.cpp
grep -q 'if (r->result == RESULT_NONE) r->result = fallbackResult' src/spell_cast_core.cpp
grep -q 'setString(L, "phase", phaseText(r))' src/spell_cast_core.cpp
grep -q 'setString(L, "result", resultText(r.result))' src/spell_cast_core.cpp
grep -q 'setString(L, "lastEvent", eventText((int)r.lastEventId))' src/spell_cast_core.cpp
grep -q 'setNumber(L, "worldGeneration", r.worldGeneration)' src/spell_cast_core.cpp

# Event-handler reads must see SUCCESS rather than a stale same-sequence PENDING.
grep -q 'r->sequence != g_pending.sequence' src/spell_cast_core.cpp

# Use the client engine millisecond clock, same low-32-bit domain as Lua GetTime().
grep -q 'OS_GET_ASYNC_TIME_MS' src/spell_cast_core.cpp
grep -q 'CLIENT_ENGINE_MS' src/spell_cast_core.cpp

# New API31 read-side surface.
grep -q 'Cast.State.Status' src/dllmain.cpp
grep -q 'Cast.State.Get' src/dllmain.cpp
grep -q 'Cast.State.GetByGuid' src/dllmain.cpp
grep -q 'Cast.State.List' src/dllmain.cpp
grep -q 'DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2"' src/dllmain.cpp
grep -q 'API_VERSION = "31"' src/dllmain.cpp
grep -q '20260830-v140-cast1r2-unified-cast-state' src/dllmain.cpp

# R2 Chinese acceptance panel is event-triggered, not a game-state polling loop.
grep -q 'Version: 2.0.0-CAST1-R2-CN' AddOn/TaiYangCastDiag/TaiYangCastDiag.toc
grep -q 'CAST1-R2 完整通过' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua
grep -q 'Cast.State.GetByGuid' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua
if grep -q 'SetScript("OnUpdate"' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua; then
  echo 'CAST1_R2_FAIL=DIAG_ONUPDATE_PRESENT' >&2
  exit 1
fi
texluac -p AddOn/TaiYangCastDiag/TaiYangCastDiag.lua

echo 'CAST1_R2_UNIFIED_CAST_STATE=PASS'
