#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Frozen Aura source/state/timing implementation must remain byte-for-byte unchanged.
if ! git diff --quiet 24d3e75 -- \
  src/aura_source_core.cpp src/aura_source_core.h \
  src/aura_caster.cpp src/aura_caster.h \
  src/aura_state.cpp src/aura_state.h \
  src/aura_cast_timing.cpp src/aura_cast_timing.h \
  src/combo_duration_core.cpp src/combo_duration_core.h \
  src/aura_duration.cpp src/aura_duration.h; then
  echo 'CAST1_R1_FAIL=FROZEN_AURA_CORE_CHANGED' >&2
  exit 1
fi

./tests/run_core_regression.sh
./tests/run_r1_regression.sh

CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic \
  tests/combo_duration_core_regression.cpp src/combo_duration_core.cpp \
  -o /tmp/cast1_r1_combo_test
/tmp/cast1_r1_combo_test
rm -f /tmp/cast1_r1_combo_test

# Shared transport/opcodes.
grep -q 'SMSG_CAST_RESULT_OPCODE = 0x0130UL' src/wow112_offsets.h
grep -q 'SMSG_SPELL_START_OPCODE = 0x0131UL' src/wow112_offsets.h
grep -q 'SMSG_SPELL_GO_OPCODE = 0x0132UL' src/wow112_offsets.h
grep -q 'SMSG_SPELL_FAILURE_OPCODE = 0x0133UL' src/wow112_offsets.h
grep -q 'MSG_CHANNEL_START_OPCODE = 0x0139UL' src/wow112_offsets.h
grep -q 'MSG_CHANNEL_UPDATE_OPCODE = 0x013AUL' src/wow112_offsets.h
grep -q 'SMSG_SPELL_DELAYED_OPCODE = 0x01E2UL' src/wow112_offsets.h
grep -q 'SMSG_SPELL_FAILED_OTHER_OPCODE = 0x02A6UL' src/wow112_offsets.h

# CAST1 must subscribe to NativeBus rather than hook leaf packet handlers.
grep -q 'subscribeIncoming(&onIncoming)' src/spell_cast_core.cpp
grep -q 'subscribeOutgoing(&onOutgoing)' src/spell_cast_core.cpp
grep -q 'subscribeWorldTick(&onTick)' src/spell_cast_core.cpp
grep -q 'setBool(L, "perOpcodeHooks", false)' src/spell_cast_core.cpp
grep -q 'setBool(L, "objectManagerPolling", false)' src/spell_cast_core.cpp
if grep -q 'OBJECT_MANAGER' src/spell_cast_core.cpp; then
  echo 'CAST1_R1_FAIL=OBJECT_MANAGER_REFERENCE_PRESENT' >&2
  exit 1
fi

# The only new direct hook is ClearCastingSpell, with the current-cast gate.
if [ "$(grep -c 'MH_CreateHook' src/spell_cast_core.cpp)" -ne 1 ]; then
  echo 'CAST1_R1_FAIL=UNEXPECTED_DIRECT_HOOK_COUNT' >&2
  exit 1
fi
grep -q 'UNIT_CLEAR_CASTING_SPELL' src/spell_cast_core.cpp
grep -q 'chainedTarget(WoW112::UNIT_CLEAR_CASTING_SPELL)' src/spell_cast_core.cpp
grep -q 'OFF_UNIT_CAST_SPELL' src/spell_cast_core.cpp
grep -q 'realStop = current != 0 && current == spellId' src/spell_cast_core.cpp

# Correct CAST_RESULT polarity: accepted status is not a failure.
grep -q 'if (status == 0)' src/spell_cast_core.cpp
grep -q '++g_castResultAccepted' src/spell_cast_core.cpp
grep -q '++g_castResultRejected' src/spell_cast_core.cpp

# SENT is a separate pending snapshot and must not overwrite an active record.
grep -q 'struct PendingCast' src/spell_cast_core.cpp
grep -q 'CastRecord sent = {}' src/spell_cast_core.cpp
if grep -n 'static void onOutgoing' -A35 src/spell_cast_core.cpp | grep -q 'acquireRecord'; then
  echo 'CAST1_R1_FAIL=SENT_OVERWRITES_ACTIVE_RECORD' >&2
  exit 1
fi

# Instant casts, channel re-stamps and world reset invariants.
grep -q 'if (!ch && castTime == 0)' src/spell_cast_core.cpp
grep -q 'CHANNEL_RESTAMP_WINDOW_MS' src/spell_cast_core.cpp
grep -q '++g_channelRestamps' src/spell_cast_core.cpp
grep -q 'zeroBytes(g_records, sizeof(g_records))' src/spell_cast_core.cpp
grep -q 'zeroBytes(&g_pending, sizeof(g_pending))' src/spell_cast_core.cpp

# CAST custom events must NOT occupy SuperWoW's fixed high 590..599 slots.
# Logical identifiers remain internal labels; emission uses dynamically claimed NULL slots.
grep -q 'EVENT_COUNT_EXPANDED = 700' src/spell_cast_core.cpp
grep -q 'claimEventSlot' src/spell_cast_core.cpp
grep -q 'if (data\[i \* 4\] != 0) continue' src/spell_cast_core.cpp
grep -q 'const int slot = slotForEvent(eventId)' src/spell_cast_core.cpp
grep -q 'slot, fmt' src/spell_cast_core.cpp
grep -q 'setBool(L, "dynamicEventSlots", true)' src/spell_cast_core.cpp
grep -q 'TysSpellCast::installEventNames(data)' src/aura_native.cpp
if grep -q 'data\[id \* 4\]' src/spell_cast_core.cpp; then
  echo 'CAST1_R1_FAIL=FIXED_EVENT_SLOT_WRITE_PRESENT' >&2
  exit 1
fi

# Remote abort parser must stop at the common guid+spell prefix; FAILED_OTHER
# has no trailing reason byte, and speculative reads corrupt the shared cursor.
if sed -n '/static void parseSpellFailure/,/^}/p' src/spell_cast_core.cpp | grep -q 'read(p, &reason)'; then
  echo 'CAST1_R1_FAIL=REMOTE_FAILURE_OPTIONAL_REASON_READ_PRESENT' >&2
  exit 1
fi

# Public API/version and build inclusion.
grep -q 'Cast.Status' src/dllmain.cpp
grep -q 'Cast.Get' src/dllmain.cpp
grep -q 'DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R1-FIX1"' src/dllmain.cpp
grep -q 'API_VERSION = "30"' src/dllmain.cpp
grep -q '20260830-v140-cast1r1-fix1-dynamic-events' src/dllmain.cpp
grep -q 'spell_cast_core.obj' build/build.sh

# Chinese event-driven acceptance addon.
grep -q 'Version: 1.2.0-CAST1-R1-FIX1-CN' AddOn/TaiYangCastDiag/TaiYangCastDiag.toc
grep -q 'CAST1 核心测试通过' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua
grep -q 'TYS_CAST_INTERRUPTED' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua
if grep -q 'SetScript("OnUpdate"' AddOn/TaiYangCastDiag/TaiYangCastDiag.lua; then
  echo 'CAST1_R1_FAIL=DIAG_ONUPDATE_PRESENT' >&2
  exit 1
fi
texluac -p AddOn/TaiYangCastDiag/TaiYangCastDiag.lua

echo 'CAST1_R1_NATIVE_SPELLCAST_CORE=PASS'
