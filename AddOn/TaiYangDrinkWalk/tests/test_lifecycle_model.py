#!/usr/bin/env python3
"""Deterministic transition-model regression tests for DrinkWalk 0.6.2.

Models the lifecycle contract and verifies corresponding production guards.
This is NOT a WoW client emulator; it does not run the production Lua VM.
"""
from pathlib import Path
import unittest

SOURCE = (Path(__file__).resolve().parents[1] / "TaiYangDrinkWalk.lua").read_text(encoding="utf-8")

class Lifecycle:
    def __init__(self):
        self.state = "UNSYNCED"
        self.anchor = None
        self.pending = False
        self.energy_anchor = None
        self.dead = False
        self.generation = 0

    def power_switch(self):
        self.generation += 1
        self.energy_anchor = None

    def restore(self, *, requested, character_match, phase_state, anchor, saved_at, now):
        elapsed = now - saved_at
        valid = (requested and character_match and phase_state in ("LOCKED", "VERIFY")
                 and anchor is not None and 0 <= anchor <= now
                 and 0 <= elapsed <= 120000)
        if valid:
            self.state = "VERIFY"
            self.anchor = anchor
            self.pending = True
        return valid

    def clear(self):
        self.state = "UNSYNCED"
        self.anchor = None
        self.pending = False

    def world_entry(self):
        preserve = self.pending and self.state == "VERIFY"
        self.pending = False
        if not preserve:
            self.clear()
        self.energy_anchor = None

    def death(self):
        self.dead = True
        self.clear()
        self.energy_anchor = None

    def alive(self):
        self.dead = False
        self.clear()
        self.energy_anchor = None

    def zone_change(self):
        if self.anchor is not None and self.state != "UNSYNCED" and not self.pending:
            self.state = "VERIFY"

class Regression(unittest.TestCase):
    def test_source_has_matching_guards(self):
        for fragment in (
            'DW.anchorMS ~= nil and (DW.state == "LOCKED" or DW.state == "VERIFY")',
            '(db.phaseState == "LOCKED" or db.phaseState == "VERIFY")',
            'anchor and anchor >= 0 and anchor <= now',
            'elapsed and elapsed >= 0 and elapsed <= 120000',
            'local restorePending = DW.reloadVerifyPending and DW.state == "VERIFY"',
            'if not restorePending then ClearPhase("ENTER_WORLD") end',
            'ResetEnergyRuntime("ENTER_WORLD")',
            'ResetEnergyRuntime("RESURRECT")',
        ):
            self.assertIn(fragment, SOURCE)

    def test_reload_first_entry_only(self):
        x = Lifecycle()
        self.assertTrue(x.restore(requested=True, character_match=True, phase_state="LOCKED",
                                  anchor=6000, saved_at=7000, now=7100))
        x.world_entry()
        self.assertEqual((x.state, x.anchor, x.pending), ("VERIFY", 6000, False))
        x.world_entry()
        self.assertEqual((x.state, x.anchor), ("UNSYNCED", None))

    def test_invalid_reload_cases(self):
        base = dict(requested=True, character_match=True, phase_state="LOCKED",
                    anchor=6000, saved_at=7000, now=7100)
        for override in ({"requested": False}, {"character_match": False},
                         {"phase_state": "UNSYNCED"}, {"anchor": None},
                         {"anchor": -1}, {"anchor": 8000},
                         {"saved_at": 7101}, {"saved_at": -200000}):
            with self.subTest(override=override):
                x = Lifecycle()
                case = dict(base, **override)
                self.assertFalse(x.restore(**case))
                self.assertEqual(x.state, "UNSYNCED")

    def test_zone_change_does_not_consume_reload(self):
        x = Lifecycle()
        x.restore(requested=True, character_match=True, phase_state="VERIFY",
                  anchor=6000, saved_at=7000, now=7100)
        x.zone_change()
        self.assertTrue(x.pending)
        x.world_entry()
        self.assertEqual(x.state, "VERIFY")

    def test_resurrection_and_energy_switch(self):
        x = Lifecycle()
        x.state, x.anchor, x.energy_anchor = "LOCKED", 1000, 1000
        x.death()
        self.assertTrue(x.dead)
        self.assertEqual((x.state, x.anchor, x.energy_anchor), ("UNSYNCED", None, None))
        x.alive()
        self.assertFalse(x.dead)
        x.energy_anchor = 2000
        x.power_switch()
        self.assertIsNone(x.energy_anchor)
        self.assertEqual(x.generation, 1)

if __name__ == "__main__":
    unittest.main()
