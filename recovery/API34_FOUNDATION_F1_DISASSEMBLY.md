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

## GUID resolver — confirmed live counters

The fast/fallback resolver is visible at approximately `0x1000BB5D`.

The fast path calls the stock client function at `0x00464870`, validates the returned object pointer and compares the object's GUID at offsets `+0x30/+0x34` against the requested 64-bit GUID. If the fast path is unavailable, misses, or is rejected by validation, the routine enters the validated O(n) object-manager fallback.

Counter globals are directly observable in the final binary:

- `0x10059ADC` = `guidFastAttempts`
- `0x10059AE0` = `guidFastHits`
- `0x10059AE4` = `guidFastMisses`
- `0x10059AE8` = `guidFastRejected`
- `0x10059AEC` = `guidFallbackAttempts`
- `0x10059AF0` = `guidFallbackHits`
- `0x10059AF4` = `guidFallbackMisses`

This confirms that API34 changed API33's GUID lookup from pure O(n) scanning to a fast stock-client lookup with validation and a safe fallback scan.

## LOS pair-cache — confirmed live counters

`Foundation.Status` obtains the LOS cache counters through a stats-copy helper at `0x10033216`. That helper copies seven 32-bit counters from the LOS cache state. The field order is confirmed by the subsequent Lua table writes:

- `0x101F3E38` = `losCacheHits`
- `0x101F3E3C` = `losCacheMisses`
- `0x101F3E40` = `losCacheExpired`
- `0x101F3E44` = `losCacheCollisionMisses`
- `0x101F3E48` = `losCacheStores`
- `0x101F3E4C` = `losCacheReplacements`
- `0x101F3E50` = `losRecomputes`

The final binary still uses the API33 LOS design constants:

- fixed cache slots: `128`
- TTL: `50 ms`
- symmetric GUID pair keying
- no worker/timer/background scan

## Important correction to the first reconstruction

The first recovered `foundation_f1.cpp` emitted zero for the statistics fields as a placeholder. The original API37 DLL does **not** do that.

Disassembly shows:

- `guidFastReady` is computed at runtime.
- GUID statistics are real live counters.
- LOS statistics are read from a real stats block.
- `losCacheSlots` is 128.
- `losCacheTtlMs` is 50.

Therefore the current source is API-contract compatible but is not yet behaviorally equivalent for the Foundation counters. The next source patch must instrument the API33 GUID resolver and LOS pair-cache with the confirmed semantics above.

## Recovery status

- Command dispatch: **disassembly-confirmed**
- Handler address: **disassembly-confirmed**
- Field names: **disassembly-confirmed**
- Fast GUID function `0x00464870`: **disassembly-confirmed**
- GUID counter meanings/update paths: **disassembly-confirmed**
- LOS stats block/order: **disassembly-confirmed**
- Cache size/TTL: **disassembly-confirmed / source-correlated**
- Source patch to replace placeholder counters: **next**
- Real-machine return-value validation: **pending**
