#pragma once
#include "lua50.h"

namespace TysDreamWeapon {
// Returns true for DreamWeapon-owned public commands.
// Native prefixes: DreamWeapon.* and MoonMarker.DreamWeapon.*.
bool isCommand(const char* cmd);

// Dispatches a public DreamWeapon command. Public weapon operations do not
// depend on DreamAvatar feature or guild authorization state.
int dispatchAnyPublic(Lua50::State L,const char* cmd);

// Compatibility surface for the standalone DreamWeapon v0.3.4.11 frontend.
// It deliberately accepts only the weapon + sync subset of the historical
// MoonMarker.DreamAvatar.* namespace, never character/glow/mount commands.
bool isLegacy0348Command(const char* cmd);
bool isLegacyWeaponCommand(const char* cmd);
bool isLegacySyncCommand(const char* cmd);
int dispatchLegacy0348(Lua50::State L,const char* cmd);

void shutdown(Lua50::State L);
const char* status();
}
