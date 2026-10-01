#pragma once
#include "lua50.h"

namespace TysAuraState {

// AURA6-D3 Unified Aura State Service.
// This service owns no Aura hook and no periodic scanner. UnitFields remains
// authoritative for current Aura existence; CasterCore and the active-player
// BuffBar expiration array are merged only on explicit query.
bool initialize();
void onWorldLeaving();

// Native observational lifecycle metadata. The Aura callback path calls this
// after receiving an Aura delta. It never decides
// whether an Aura exists and never changes caster attribution.
void observeAura(unsigned long long targetGuid,
                 unsigned long spellId,
                 unsigned long rawSlot,
                 unsigned long state,
                 unsigned long nowMs);

// API23 unified read surface:
//   TaiYangShenDian("Aura.State.Status")
//   TaiYangShenDian("Aura.Get", "target", rawSlot)
//   TaiYangShenDian("Aura.List", "target")
int dispatchStatus(Lua50::State L);
int dispatchGet(Lua50::State L);
int dispatchList(Lua50::State L);

const char* status();

} // namespace TysAuraState
