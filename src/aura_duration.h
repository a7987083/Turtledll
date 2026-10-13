#pragma once
#include "lua50.h"

namespace TysAuraDuration {

// Exact self-duration layer. D4-R1 always owns one native
// CGBuffBar_UpdateDuration detour and emits TYS duration events 580/581.
// No external provider, timer, packet poller, Aura poller, or background scan.
bool initialize();

// Called by the existing Aura FrameScript event-table initializer.
bool installEventNames(char** eventData);

int dispatchStatus(Lua50::State L);
int dispatchSnapshot(Lua50::State L);

} // namespace TysAuraDuration
