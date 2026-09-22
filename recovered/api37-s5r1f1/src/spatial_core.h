#pragma once
#include "lua50.h"
namespace TysSpatialCore {
int dispatchStatus(Lua50::State L);
int dispatchGet(Lua50::State L);
int dispatchDistance(Lua50::State L);
int dispatchBehind(Lua50::State L);
}
