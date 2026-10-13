#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
./tests/run_core_regression.sh
if grep -RInE 'Nampower|NAMPOWER|nampower' src build --exclude='THIRD_PARTY*'; then
  echo 'R1_FAIL=RUNTIME_COMPAT_REFERENCE_PRESENT' >&2
  exit 1
fi
if grep -RInE 'WoW112::SPELL_GO|Aura.Caster.External' src; then
  echo 'R1_FAIL=LEGACY_PROVIDER_OR_LEAF_HOOK_PRESENT' >&2
  exit 1
fi
grep -q 'NET_MESSAGE_DISPATCH = 0x00537AA0UL' src/wow112_offsets.h
grep -q 'NET_SEND = 0x005379A0UL' src/wow112_offsets.h
grep -q 'WORLD_TICK = 0x0066FD50UL' src/wow112_offsets.h
grep -q 'subscribeIncoming(&onIncomingPacket)' src/aura_caster.cpp
grep -q 'perOpcodeSpellGoHookInstalled",false' src/aura_caster.cpp
echo 'AURA6_D4_R1_NATIVE_BUS_REGRESSION=PASS'
