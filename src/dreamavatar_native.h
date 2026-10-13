#pragma once
#include "lua50.h"

namespace TysDreamAvatar {
// Returns -1 when cmd is not DreamAvatar-owned. Handles both
// DreamAvatar.* and MoonMarker.DreamAvatar.* aliases.
int dispatch(Lua50::State L,const char* cmd,bool featureEnabled);
// Shared signed DAS1 appearance transport. These commands are independent of
// DreamAvatar local-edit authorization/feature state and accept both historical
// DreamAvatar.Sync.* and compatibility DreamWeapon.Sync.* prefixes.
bool isSharedSyncCommand(const char* cmd);
int dispatchSharedSync(Lua50::State L,const char* cmd);
void onWorldLeaving(Lua50::State L);
void onWorldEntering(Lua50::State L);
void shutdownRestricted(Lua50::State L);
void shutdown(Lua50::State L);
const char* status();
// Shared backend used by the independent DreamWeapon native module.
// This exposes only the historical public weapon surface plus signed bidirectional sync;
// it deliberately bypasses DreamAvatar guild authorization without duplicating
// the appearance implementation.
int dispatchDreamWeaponShared(Lua50::State L,const char* suffix);
void shutdownDreamWeaponShared(Lua50::State L);
}
