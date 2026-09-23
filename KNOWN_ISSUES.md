# KNOWN_ISSUES

## Active blockers

### 1. Strict callable-API regression is still pending

The first all-latest combined build succeeded, and the broad dotted-string set matches the final target exactly (`129/129`, zero missing/extra). The stricter known callable API count is `118`; that contract-level check still needs to be rerun before labeling the DLL a final static candidate.

### 2. Remaining binary-size delta

- target: `380928` bytes
- combined recovery: `371200` bytes
- delta: `9728` bytes

This must be investigated for genuinely missing behavior/metadata. Do not close the delta by padding.

### 3. GitHub Actions source reconstruction remains broken

`recovery/archive.parts` is still incomplete/truncated, so the existing GitHub Actions workflow cannot reconstruct the source base. Local build validation is currently authoritative for compilation status.

### 4. Runtime verification remains pending

The combined DLL has not yet been tested in the live WoW/Turtle client. Cooldown packet coalescing/order, UnitState lifecycle/event timing and exact Spatial Lua return behavior still need real-client regression.

## Resolved / reduced issues

- UnitState latest source: integrated and compile-verified.
- Cooldown exact kind/source/events: integrated and compile-verified.
- Spatial S5-R1F1 no-STL implementation: integrated and compile-verified.
- All three latest modules now compile simultaneously in one API37 DLL.
- Combined build SHA256: `86f2c854770225bb8e3223f30cdde59bf7b1df3411a52b04e954eb5111b55998`.
- Spatial compile issue encountered during combined work was only a local transcription syntax problem: clang-cl rejected single-line x87 `__asm`; restoring the prior multiline MS-style form fixed it.

## Non-blocking cautions

- Turtle/Tortoise 1.18.1 remains supporting server/protocol evidence only; final DLL disassembly wins on client behavior.
- The S5-R2 ~105° observation is unfinished testing, not a confirmed threshold and not a global/special-boss rule.
- API34-37 recovered C++ is behavior-equivalent reconstruction, not the original lost source text.
