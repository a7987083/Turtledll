#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

grep -Fq '#include "drinkwalk_native.h"' src/dllmain.cpp
grep -Fq 'DrinkWalk.Status' src/dllmain.cpp
grep -Fq 'DrinkWalk.ResolveItem' src/dllmain.cpp
grep -Fq 'DrinkWalk.UseItem' src/dllmain.cpp
grep -Fq 'PACK_BAG_SLOT = 0x004F9820UL' src/wow112_offsets.h
grep -Fq 'ITEMMGR_GET_ITEM_BY_SLOT = 0x006228A0UL' src/wow112_offsets.h
grep -Fq 'ITEM_USE_NATIVE = 0x005D8D00UL' src/wow112_offsets.h
grep -Fq 'drinkwalk_native.obj' build/build.sh
grep -Fq 'PackBagSlot->GetItemBySlot' src/drinkwalk_native.cpp
grep -Fq 'CGItem::UseItem' src/drinkwalk_native.cpp

echo 'DRINKWALK_NATIVE_DW1_SOURCE=PASS'
