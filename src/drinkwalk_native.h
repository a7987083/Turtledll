#pragma once
#include "lua50.h"

namespace TysDrinkWalkNative {
int dispatchStatus(Lua50::State L);
int dispatchResolveItem(Lua50::State L);
int dispatchUseItem(Lua50::State L);
}
