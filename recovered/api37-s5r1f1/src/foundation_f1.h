#pragma once
#include "lua50.h"

namespace TysFoundationF1 {
bool initialize();
const char* status();
int dispatchStatus(Lua50::State L);
}
