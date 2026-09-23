# API36 UnitState US1-R2 exact Status surface

Target: `taiyangshendian_API37_S5_R1F1.dll`
SHA-256: `1d17050789310d077dbfaf1d6f00a67f822b6166828d44b7fe08a69977706eae`

## Binary-confirmed public Status keys

The final target `UnitState.Status` handler (`0x10045A02`) exposes the following recovery-relevant keys:

- `status`
- `architecture`
- `authority`
- `capacity`
- `tracked`
- `customEventsReady`
- `dynamicEventSlots`
- `eventHealthSlot`
- `eventPowerSlot`
- `eventCombatSlot`
- `updateObjectOpcode`
- `compressedUpdateObjectOpcode`
- `fastGuidLookupAddress`
- `updatePackets`
- `compressedUpdatePackets`
- `dirtySignals`
- `coalescedSignals`
- `reconcilePasses`
- `recordsChecked`
- `objectUnavailable`
- `descriptorFailures`
- `healthEvents`
- `powerEvents`
- `combatEvents`
- `trackCalls`
- `untrackCalls`
- `capacityFailures`
- `lastChangedGuid`
- `lastChangedMask`
- `worldGeneration`
- `reconcilePending`
- `directHook`
- `objectManagerPolling`
- `packetBodyParsing`
- `zlibDecompression`
- `timer`
- `idleTickPath`
- `powerSemantics`
- `powerMaskSemantics`
- `descriptorReadPolicy`
- `combatFlag`
- `compressedPolicy`

## Important correction

The final target Status handler does **not** expose these keys:

- `descriptorClears`
- `descriptorEmptyPreserves`
- `descriptorReconciles`
- `descriptorUnbinds`

Those strings exist elsewhere in the DLL and/or were consumed opportunistically by diagnostic add-ons, but they are not part of the final `UnitState.Status` output path. Recovery code must not invent them as public fields.

## Exact counter mapping in the final target

Within the US1-R2 globals used by `UnitState.Status`:

- `0x1021B74C` -> `reconcilePasses`
- `0x1021B750` -> `recordsChecked`
- `0x1021B754` -> `objectUnavailable`
- `0x1021B758` -> `descriptorFailures`
- `0x1021B75C` -> `healthEvents`
- `0x1021B760` -> `powerEvents`
- `0x1021B764` -> `combatEvents`
- `0x1021B768` -> `trackCalls`
- `0x1021B76C` -> `untrackCalls`
- `0x1021B770` -> `capacityFailures`
- `0x100595F4` -> `worldGeneration`

## Reconcile failure branches

World-tick reconcile (`0x100450AF`) calls snapshot helper `0x100469FE` for each tracked dirty record.

- helper returns false -> `descriptorFailures++`
- helper returns true and `objectPresent == false` -> `objectUnavailable++` and cached live/valid state is cleared
- helper returns true, object present, but descriptor-present flag is false -> `descriptorFailures++`
- complete snapshot -> compare old/new state and emit health/power/combat events as needed

This means `objectUnavailable` is an authoritative no-live-object condition and is distinct from descriptor read failure.

## Change masks

`lastChangedMask` is a category mask:

- `0x01` HEALTH
- `0x02` POWER
- `0x04` COMBAT

The power event payload has its own independent mask:

- `0x01` active power type changed
- `0x02` active power value changed
- `0x04` active max power changed

## Confidence

All field names and mappings above are taken from final-target string references and disassembly of the final API37 DLL. Runtime validation is still pending.
