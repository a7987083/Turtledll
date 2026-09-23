# KNOWN_ISSUES

## Active blockers

### 1. Full latest API37 build is not finished

Two stage-specific compile tests now exist:

- exact UnitState overlay build: `d44302b3227c5a13ae9132db4d1b080512dede81820f32a60aeb0782f4c92099`
- exact Cooldown integration build: `828e7c683ad6c623c37f1b38a95fc5dd74e0242bc7946af13254dad863fa3907`

Neither build contains the latest UnitState + Cooldown + Spatial source simultaneously. Do not label either DLL as the final recovered API37 DLL.

### 2. Spatial latest source still needs compile integration

The S5-R1F1 geometry/range source is recovered, but it still needs conversion/integration into the same no-STL/no-default-lib overlay used for the successful UnitState and Cooldown compile tests.

Next action: integrate Spatial, build all three latest modules together, then run static/API/string regression.

### 3. GitHub Actions source reconstruction is still broken

`recovery/archive.parts` remains incomplete/truncated. The existing workflow reconstructs an XZ/base64 archive and fails before source compilation when the archive is incomplete.

Impact: GitHub CI cannot yet reproduce the successful local compile-test path.

### 4. Runtime verification is pending

The latest reconstructed API34-37 behavior has not yet been validated in a live WoW/Turtle client. Runtime-only edge behavior may still differ even where static recovery and compile validation succeed.

## Resolved / reduced issues

### Cooldown exact integration

The previous issue that exact kind/source and STARTED/CHANGED/READY helpers were not wired into `cooldown_core.cpp` is resolved at compile-test level.

Current implementation includes:

- packet sources 1..5 plus deadline source 6 and explicit source 7
- packet-source-preserving dirty reconciliation
- SpellRec recovery fields used for GCD/SPELL classification
- `TYS_COOLDOWN_STARTED/CHANGED/READY`
- CLEAR/CHEAT ready and SPELL->GCD diagnostic paths
- no-STL/no-default-lib compilation

Live runtime verification remains open.

## Non-blocking uncertainties

- Exact Lua return/table nuances for some Spatial wrappers still require target-vs-recovery runtime comparison.
- Some Cooldown packet edge cases still require live validation, especially ordering when several packet causes coalesce before the next world tick.
- Turtle/Tortoise 1.18.1 source is supporting server/protocol evidence only; it must not override final target DLL disassembly where the two differ.
- Backstab S5-R2 ~105° observation is experimental/incomplete and special-target-related; it is not a confirmed threshold or global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction work, not the original lost source text.
