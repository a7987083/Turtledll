# CAST1-R2 Unified CastState + Corpse LootFX Stable

Base commit: `c5855262c5ed768f2b981e9da6c0bd6d9e38b1f7`
Base DLL SHA256: `2c449ead21770f15c8067311b2c4bfb6ad934f60ac82ef2ffb7fdfb2c65aa1ed`

Frozen compatibility identity:
- DLL: `1.4.0-AURA6D4-R4-CAST1R2`
- API: `31`

Only new runtime behavior:
- patch the real-client A/B-verified corpse-only direct CreateModel callsite `0x61FA6A`;
- stock `Particles\\LootFX.mdl` on that callsite is routed to `Particles\\TaiYangCorpse\\LootFX.mdl`;
- gathering/interact callsite `0x61FC9F` is untouched;
- failed flag creation falls back to stock LootFX;
- no corpse slot pool, no GUID table, no ObjectManager scan, no reconcile/timer, no global CreateModel hook.

Required MPQ resource:
- `Particles\\TaiYangCorpse\\LootFX.m2`
- must NOT contain a global `Particles\\LootFX.m2` override.

No API31 command/catalog additions were made.
