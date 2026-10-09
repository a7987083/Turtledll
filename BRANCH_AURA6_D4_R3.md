# Branch: dev/aura6-d4-r3-target-duration

Parent: `stable/aura6-d4-r2` / `2413d83910a7f767debe9b5c9a781a6afbe19430`

Purpose: finish target Aura PREDICTED timing on top of the already-validated NativeBus + AuraSourceCore architecture.

Key rules:
- no Nampower/provider compatibility mode
- no direct SPELL_GO leaf hook
- no second target-duration cache
- UnitFields remains presence authority
- self exact timer remains BuffBar-based
- target timing is explicitly PREDICTED, never mislabeled EXACT
