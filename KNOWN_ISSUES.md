# KNOWN_ISSUES

## Active blockers

### 1. `UNIT_RESOLVER_UNAVAILABLE` ownership remains unresolved

The target contains this string, but a raw image scan found no direct absolute address xref to the string start or to any suffix position. Its owning handler is therefore still unproven. Do not add it speculatively.

### 2. Remaining binary-size/code delta

- target: `380928` bytes
- current candidate: `374272` bytes
- delta: `6656` bytes
- target `.text`: `0x489c0`
- recovery `.text`: `0x47148`
- target `.rdata`: `0xa4fb`
- recovery `.rdata`: `0xa4ef`

The strict callable API set matches `118/118` and broad dotted strings match `129/129`. Exact build id is now restored. Remaining work is implementation/code fidelity, not API discovery or string padding.

API36→API37 stage comparison shows target Spatial introduction increases `.text` by approximately the same magnitude as the remaining recovery `.text` gap. Continue disassembling/recovering real Spatial/wrapper behavior; never pad.

### 3. GitHub Actions source reconstruction remains broken

`recovery/archive.parts` is incomplete/truncated, so the current workflow cannot reproduce the local source base. Local clang-cl/lld-link builds are authoritative for compile validation.

### 4. Runtime verification remains pending

The current candidate has not yet been tested in a live WoW/Turtle client. Cooldown event ordering/coalescing, UnitState selector/lifecycle/event timing and Spatial wrapper return behavior still require real-client regression.

## Resolved / reduced issues

- Exact UnitState selector helper is integrated and locally compile-verified.
- Latest Cooldown + UnitState + Spatial compile simultaneously.
- Strict callable API regression: target `118`, recovery `118`, missing `0`, extra `0`.
- Broad dotted strings: target `129`, recovery `129`, missing `0`, extra `0`.
- Exact final API37 build id restored through the overlay.
- Meaningful `.rdata` delta reduced from `0x50` to `0x0c`.
- Current candidate SHA256 `dccef1c05e0158f9052546bf0a1042a7432102091cffb0ecf5fbada30cab0f34`.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 is supporting server/protocol evidence only; final target DLL disassembly wins for client behavior.
- S5-R2 ~105° was unfinished testing only; it is not a confirmed threshold or a special-boss/global rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not original lost source text.
