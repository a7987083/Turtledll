#pragma once
#include "lua50.h"
namespace TysUnitStateCore {
bool initialize();
const char* status();
int dispatchStatus(Lua50::State L);
int dispatchGet(Lua50::State L);
int dispatchTrack(Lua50::State L);
int dispatchUntrack(Lua50::State L);
int dispatchList(Lua50::State L);
int dispatchClear(Lua50::State L);
}
