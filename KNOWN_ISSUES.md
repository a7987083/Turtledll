# KNOWN_ISSUES

## Active blockers

### 1. Current recovery source has not been rebuilt

The previously produced recovery DLL predates the latest exact Cooldown/UnitState/Spatial source edits.

Reason: GitHub CI source reconstruction still uses incomplete/truncated `recovery/archive.parts`; xz previously failed with `Unexpected end of input` before source compilation.

Impact: no DLL generated from the current branch should be claimed until this archive/build path is repaired.

### 2. UnitState source still needs final Status-surface alignment

The final DLL `UnitState.Status` surface has now been mapped exactly. The current `unit_state_core.cpp` still contains legacy/invented public fields/counters such as descriptorReconciles/descriptorClears/descriptorEmptyPreserves/descriptorUnbinds and lacks some exact final fields such as trackCalls/capacityFailures/worldGeneration/reconcilePending.

Next action: rewrite the Status bookkeeping/output to the exact binary-confirmed surface documented in `recovery/API36_UNITSTATE_STATUS_EXACT.md`.

### 3. PLAYER_LEAVING_WORLD lifecycle funnel is not yet wired into recovered UnitState source

Final DLL behavior is confirmed: PLAYER_LEAVING_WORLD calls the UnitState reset routine and advances `worldGeneration`.

The recovered source tree does not currently contain the historical NativeBus implementation file in the overlay directory, so the lifecycle callback must be restored through the authentic API33 event infrastructure rather than by inventing a new hook.

### 4. Cooldown helpers are not fully integrated into `cooldown_core.cpp`

Exact kind/source classifier and STARTED/CHANGED/READY transition helpers are present as recovery helpers/evidence, but the main recovered core still predates full integration.

### 5. Runtime verification is pending

No current API34-37 reconstructed source has been validated in a live WoW/Turtle client after the latest edits. Runtime-only edge behavior may still differ.

## Non-blocking uncertainties

- Exact Lua return/table nuances for some Spatial wrappers still require target-vs-recovery runtime comparison.
- Backstab S5-R2 ~105° observation is experimental/incomplete and special-target-related; it is not a confirmed threshold or global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction work, not the original lost source text.
