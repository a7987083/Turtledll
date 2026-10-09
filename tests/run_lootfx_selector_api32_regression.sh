#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BASE=c5855262c5ed768f2b981e9da6c0bd6d9e38b1f7

./tests/run_core_regression.sh
./tests/run_r1_regression.sh

git diff --quiet "$BASE" -- src/spell_cast_core.cpp src/spell_cast_core.h

grep -Fq 'DLL_VERSION = "1.4.0-AURA6D4-R4-CAST1R2-LFX1"' src/dllmain.cpp
grep -Fq 'API_VERSION = "32"' src/dllmain.cpp
grep -Fq '20260830-v140-cast1r2-lootfx-selector-api32' src/dllmain.cpp

grep -Fq 'CORPSE_CREATE_MODEL_CALLSITE = 0x0061FA6AUL' src/corpse_marker.cpp
grep -Fq 'GATHER_CREATE_MODEL_CALLSITE = 0x0061FC9FUL' src/corpse_marker.cpp
grep -Fq 'CREATE_MODEL_ENTRY = 0x00707350UL' src/corpse_marker.cpp
grep -Fq 'LootFX.Corpse.Set' src/corpse_marker.cpp
grep -Fq 'LootFX.Gather.Set' src/corpse_marker.cpp
grep -Fq 'restoreCall(g_gatherPatch)' src/corpse_marker.cpp
! grep -q 'ObjectManager' src/corpse_marker.cpp
! grep -q 'GUID' src/corpse_marker.cpp

grep -Fq 'LootFX.Corpse.Set' LOOTFX_SELECTOR_API32.md
grep -Fq 'LootFX.Gather.Set' LOOTFX_SELECTOR_API32.md

# v0.3 addon: Chinese corpse-only UI; gathering is force-restored to stock.
grep -Fq 'Version: 0.3.0-API32' AddOn/TYSLootFXSelector/TYSLootFXSelector.toc
grep -Fq '## Dependencies: !Libs' AddOn/TYSLootFXSelector/TYSLootFXSelector.toc
grep -Fq 'TYSLootFXSelector_FuBar.lua' AddOn/TYSLootFXSelector/TYSLootFXSelector.toc
grep -Fq 'TYSLootFXSelectorFuDB' AddOn/TYSLootFXSelector/TYSLootFXSelector.toc
grep -Fq 'DIREMAULCRYSTALGENERATOR.M2' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq 'HordeCTFflag_spell.M2' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq '尸体模型预设' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq '太阳神殿 · 尸体模型选择器' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq '选择模型' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq '默认旗帜' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq '原版光' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq 'LootFX.Corpse.Set' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
grep -Fq 'LootFX.Gather.Stock' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'LootFX.Gather.Set' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'MODEL PRESET MATRIX' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'SELECT M2' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'DEFAULT FLAG' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'STOCK FX' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'GATHER /' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
! grep -q 'AutoRange.M2Scan' AddOn/TYSLootFXSelector/TYSLootFXSelector.lua

grep -Fq 'FuBarPlugin-2.0' AddOn/TYSLootFXSelector/TYSLootFXSelector_FuBar.lua
grep -Fq 'defaultPosition = "RIGHT"' AddOn/TYSLootFXSelector/TYSLootFXSelector_FuBar.lua
grep -Fq 'self:SetText("尸体模型")' AddOn/TYSLootFXSelector/TYSLootFXSelector_FuBar.lua
grep -Fq 'TYSLootFXSelector_Toggle' AddOn/TYSLootFXSelector/TYSLootFXSelector_FuBar.lua

texluac -p AddOn/TYSLootFXSelector/TYSLootFXSelector.lua
texluac -p AddOn/TYSLootFXSelector/TYSLootFXSelector_FuBar.lua

echo 'LOOTFX_SELECTOR_API32_UI_V03_CN_CORPSE_ONLY=PASS'
