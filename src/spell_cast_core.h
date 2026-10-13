#pragma once
#include "lua50.h"

namespace TysSpellCast {

// CAST1-R1 native cast lifecycle. Transport is shared NativeBus; the only
// direct engine hook is CGUnit::ClearCastingSpell, the Turtle/SuperWoW choke
// point needed to observe remote interrupts that are not broadcast as packets.
bool initialize();
void onWorldLeaving();

// Called by the already-owned Aura FrameScript event-table hook. This only
// reserves CAST event names in the expanded table; it installs no hook.
bool installEventNames(char** eventData);

int dispatchStatus(Lua50::State L);
int dispatchGet(Lua50::State L);
int dispatchStateStatus(Lua50::State L);
int dispatchStateGet(Lua50::State L);
int dispatchStateGetByGuid(Lua50::State L);
int dispatchStateList(Lua50::State L);

} // namespace TysSpellCast
