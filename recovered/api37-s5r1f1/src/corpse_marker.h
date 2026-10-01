#pragma once
#include "lua50.h"

namespace TysCorpseMarker {

bool initialize();
void shutdown();
bool setEnabled(bool enabled);
bool enabled();
const char* status();
const char* modelPath();
void diagnostics(char* out, unsigned long cap);

// API32 LootFX selector. Returns -1 for unknown commands.
int dispatchSelector(Lua50::State L, const char* cmd);

} // namespace TysCorpseMarker
