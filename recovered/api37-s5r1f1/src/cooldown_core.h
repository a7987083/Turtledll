#pragma once
#include "lua50.h"
namespace TysCooldownCore {
bool initialize();
const char* status();
int dispatchStatus(Lua50::State L);
int dispatchGet(Lua50::State L);
int dispatchList(Lua50::State L);
}
