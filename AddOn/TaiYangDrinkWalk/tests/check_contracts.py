#!/usr/bin/env python3
"""Static compatibility contracts for the WoW 1.12 DrinkWalk release.

These checks complement Lua 5.0 parsing; they do not simulate the WoW client.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
lua = (root / "TaiYangDrinkWalk.lua").read_text(encoding="utf-8")
toc = (root / "TaiYangDrinkWalk.toc").read_text(encoding="utf-8")
fubar = (root / "FuBar.lua").read_text(encoding="utf-8")
version = re.search(r'local VERSION = "([^"]+)"', lua)
assert version, "VERSION missing"
assert f"## Version: {version.group(1)}" in toc, "TOC/UI versions diverge"
assert "## Interface: 11200" in toc, "Vanilla interface was changed"
assert "EffectiveSpamOpenMS(" not in lua, "dangling removed function reference"
assert "TaiYangShenDian, \"DrinkWalk.UseItem\"" in lua, "native dispatch signature changed"
assert "DW.reloadVerifyPending = true" in lua, "reload recovery flag missing"
assert 'local restorePending = DW.reloadVerifyPending and DW.state == "VERIFY"' in lua, "world-entry one-shot guard missing"
assert 'DW.reloadVerifyPending = nil' in lua, "one-shot cleanup missing"
assert 'ResetEnergyRuntime("RESURRECT")' in lua, "resurrection energy reset missing"
assert 'ResetEnergyRuntime("ENTER_WORLD")' in lua, "world-entry energy reset missing"
assert "TaiYangDrinkWalkFu.overrideMenu = true" in fubar, "FuBar custom menu missing"
assert "## SavedVariables: TaiYangDrinkWalkDiagDB, TaiYangDrinkWalkFuDB" in toc, "settings names changed"
for evt in ("VARIABLES_LOADED", "BAG_UPDATE", "PLAYER_ENTERING_WORLD",
            "PLAYER_DEAD", "PLAYER_ALIVE", "PLAYER_UNGHOST",
            "ZONE_CHANGED_NEW_AREA", "UNIT_MANA", "UNIT_ENERGY"):
    assert f'eventFrame:RegisterEvent("{evt}")' in lua, f"event missing: {evt}"
print(f"DrinkWalk {version.group(1)} contract checks passed")
