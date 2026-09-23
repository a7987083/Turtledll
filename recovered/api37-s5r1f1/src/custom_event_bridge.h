#pragma once
#include <windows.h>
namespace TysCustomEvents {
constexpr unsigned EVENT_COUNT_EXPANDED = 700u;
bool available();
int claim(const char* name);
int find(const char* name);
bool ensureUnitStateEvents();
bool emitUnitHealth(unsigned long long guid,unsigned long oldHealth,unsigned long newHealth,unsigned long maxHealth,bool dead);
bool emitUnitPower(unsigned long long guid,unsigned long powerType,unsigned long oldPower,unsigned long newPower,unsigned long maxPower,unsigned long changeMask);
bool emitUnitCombat(unsigned long long guid,bool oldCombat,bool newCombat);
bool ensureCooldownEvents();
bool emitCooldownStarted(unsigned long spellId,unsigned long startMs,unsigned long durationMs,unsigned long endMs,unsigned long remainingMs,unsigned long enable,unsigned long kind,unsigned long source);
bool emitCooldownChanged(unsigned long spellId,unsigned long startMs,unsigned long durationMs,unsigned long endMs,unsigned long remainingMs,unsigned long enable,unsigned long kind,unsigned long source);
bool emitCooldownReady(unsigned long spellId,unsigned long startMs,unsigned long durationMs,unsigned long endMs,unsigned long remainingMs,unsigned long enable,unsigned long kind,unsigned long source);
int unitHealthSlot();int unitPowerSlot();int unitCombatSlot();
int cooldownStartedSlot();int cooldownChangedSlot();int cooldownReadySlot();
} // namespace TysCustomEvents
