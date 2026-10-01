#pragma once
#include "lua50.h"

namespace TysMoonAdvanced {
// Returns -1 if cmd is neither MMAuth nor a migrated public/advanced MoonMarker command.
int dispatch(Lua50::State L,const char* cmd,bool visualEnabled);
void onWorldLeaving();
void observeWorldContext(unsigned long token);
// Exact legacy MoonMarker.Clear cleanup: clear the Advanced local draft only.
// The caller also clears the Advanced preview visual through the shared visual core.
void clearLocalDraft();
void shutdown();
}
