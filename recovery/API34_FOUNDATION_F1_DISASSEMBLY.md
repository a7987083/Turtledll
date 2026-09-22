# API34 / Foundation F1 — disassembly confirmation

Target binary: `taiyangshendian_API37_S5_R1F1.dll`

- SHA256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`
- PE32 / i386
- Image base: `0x10000000`

## Confirmed dispatcher path

`Foundation.Status` is compared in the main Lua command dispatcher around `0x10003EC7` and dispatches to the function at:

`0x1000584B`

This is direct code-reference evidence, not string-only inference.

## Confirmed Foundation.Status fields

The handler at `0x1000584B` constructs a Lua table and references the following field names directly:

- `stage` -> `F1`
- `clock` -> `TysClock32`
- `clockWrapSafe`
- `guidFastAddress`
- `guidFastReady`
- `guidFastAttempts`
- `guidFastHits`
- `guidFastMisses`
- `guidFastRejected`
- `guidFallbackAttempts`
- `guidFallbackHits`
- `guidFallbackMisses`
- `losCacheSlots`
- `losCacheTtlMs`
- `losCacheHits`
- `losCacheMisses`
- `losCacheExpired`
- `losCacheCollisionMisses`
- `losCacheStores`
- `losCacheReplacements`
- `losRecomputes`

The binary also carries Foundation-related status strings describing the fast GUID address as `0x464870` and the LOS implementation as `CWorld_Intersect 0x672170`, flags `0x100111`, symmetric pair-cache TTL 50 ms.

## Important correction to the first reconstruction

The first recovered `foundation_f1.cpp` emitted zero for the statistics fields as a placeholder. The original API37 DLL does **not** do that.

Disassembly shows:

- `guidFastReady` is computed at runtime through a helper call.
- GUID statistics are read from live global counters.
- LOS statistics are obtained from a live stats structure before being pushed into the Lua table.
- `losCacheSlots` is 128.
- `losCacheTtlMs` is 50.

Therefore the current source is API-contract compatible but is not yet behaviorally equivalent for the Foundation counters. The next recovery patch must instrument the API33 GUID resolver and LOS pair-cache so these fields are real live counters.

## Recovery status

- Command dispatch: **disassembly-confirmed**
- Handler address: **disassembly-confirmed**
- Field names/order family: **disassembly-confirmed**
- Constant cache size/TTL: **disassembly-confirmed / source-correlated**
- Exact counter update sites: **in progress**
- Real-machine return-value validation: **pending**
