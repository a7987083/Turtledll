#pragma once
#include "lua50.h"

namespace TysMoonMarker {

// Returns -1 when cmd is not a standard MoonMarker compatibility command.
// Otherwise returns the exact Lua return count for the handled command.
int dispatch(Lua50::State L,const char* cmd,bool visualEnabled);
// One-shot ordinary MoonMarker ABI preflight. This does not install the lazy
// ground-cursor hook; it only validates the addresses/data required by the
// currently migrated MoonMarker core.
bool preflight(const char** code);
void shutdown();
const char* status();

} // namespace TysMoonMarker
