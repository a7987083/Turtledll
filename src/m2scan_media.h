#pragma once
#include "lua50.h"

namespace TysScanMedia {

// Public AutoRange R7.5.1b APIs. Returns -1 for an unknown command.
int dispatchAutoRange(Lua50::State L,const char* cmd);

// Old MoonMarker.Advanced.ScanM2.* compatibility. Authorization is enforced
// by the caller. Return shapes stay at the old MoonMarker shape.
int dispatchMoonAdvanced(Lua50::State L,const char* cmd);

void shutdown();

} // namespace TysScanMedia
