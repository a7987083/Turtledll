# KNOWN_ISSUES

## Active blockers

### 1. Exact UnitState selector helper is not fully reproduced

Final DLL evidence points to a selector helper around client resolver `0x00515940`, with explicit handling for `player`, `target`, `mouseover`, `pet`, `partyN`, `raidN` and distinct error paths such as `RESOLVE_UNIT_UNAVAILABLE`, `UNIT_NOT_FOUND`, `GUID_INVALID`, and `BAD_SELECTOR`.

Current recovery still uses the older/simpler token resolver path. This is the main known public-behavior gap after the UnitState.Get/Track/Untrack/Clear surface alignment.

### 2. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but its owning handler/xref has not yet been proven. Do not add it speculatively merely to close a string difference.

### 3. Remaining binary-size delta

- target: `380928` bytes
- current exact-surface candidate: `373760` bytes
- delta: `7168` bytes

The strict callable API set already matches `118/118` and the broad dotted-string set matches `129/129`; the remaining size difference must be investigated as possible implementation/detail differences, not padded.

### 4. GitHub Actions source reconstruction remains broken

`recovery/archive.parts` is incomplete/truncated, so the current workflow cannot reproduce the local source base. Local clang-cl/lld-link builds are the authoritative compile validation for now.

### 5. Runtime verification remains pending

The exact-surface candidate has not yet been tested in a live WoW/Turtle client. Cooldown event ordering/coalescing, UnitState selector/lifecycle/event timing and Spatial wrapper return behavior still require real-client regression.

## Resolved / reduced issues

- Latest Cooldown + UnitState + Spatial compile simultaneously.
- Strict callable API regression: target `118`, recovery `118`, missing `0`, extra `0`.
- Broad dotted strings: target `129`, recovery `129`, missing `0`, extra `0`.
- UnitState.Get no longer incorrectly requires Track and now exposes the final public table shape.
- UnitState.Untrack/Clear return semantics aligned to final DLL.
- Cooldown success status and UnitState success status aligned to final DLL.
- Cooldown and UnitState both use the existing PLAYER_LEAVING_WORLD lifecycle funnel.
- Current candidate SHA256 `6bd0239cd15e66486c47267f70ef9dc6f31cc70b6878e12f08ffa49cfaabb393`.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or a special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
