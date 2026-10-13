#pragma once
#include "lua50.h"

namespace TysAuraCaster {

// AURA6-D4-R2 native-only facade backed by AuraSourceCore. SMSG_SPELL_GO is observed through the shared
// NativeBus packet-dispatch funnel; there is no external provider or DLL
// arbitration path.
bool initialize();

int dispatchStatus(Lua50::State L);
int dispatchMatch(Lua50::State L);
int dispatchSnapshot(Lua50::State L);

const char* backend();
const char* status();
bool hookInstalled();

// World lifecycle: clear AuraSourceCore/cast-timing/state metadata before the next map.
void onWorldLeaving();

// Lifecycle input from the existing native Aura callbacks.
void onAuraAdded(unsigned long long targetGuid, unsigned long spellId, unsigned long rawSlot);
void onAuraRemoved(unsigned long long targetGuid, unsigned long spellId, unsigned long rawSlot);
void onAuraStackChanged(unsigned long long targetGuid, unsigned long spellId, unsigned long rawSlot);

} // namespace TysAuraCaster
