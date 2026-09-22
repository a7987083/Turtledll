#pragma once

#include <windows.h>
#include <cstdint>

namespace TysCustomEvents {

constexpr unsigned EVENT_COUNT_EXPANDED = 700u;

bool available();
int claim(const char* name);
int find(const char* name);

bool ensureUnitStateEvents();
bool emitUnitHealth(unsigned long long guid,
                    std::uint32_t oldHealth,
                    std::uint32_t newHealth,
                    std::uint32_t maxHealth,
                    bool dead);
bool emitUnitPower(unsigned long long guid,
                   std::uint32_t powerType,
                   std::uint32_t oldPower,
                   std::uint32_t newPower,
                   std::uint32_t maxPower,
                   std::uint32_t changeMask);
bool emitUnitCombat(unsigned long long guid,
                    bool oldCombat,
                    bool newCombat);

bool ensureCooldownEvents();
bool emitCooldownStarted(std::uint32_t spellId, std::uint32_t startMs,
                         std::uint32_t durationMs, std::uint32_t endMs,
                         std::uint32_t remainingMs, std::uint32_t enable,
                         std::uint32_t kind, std::uint32_t source);
bool emitCooldownChanged(std::uint32_t spellId, std::uint32_t startMs,
                         std::uint32_t durationMs, std::uint32_t endMs,
                         std::uint32_t remainingMs, std::uint32_t enable,
                         std::uint32_t kind, std::uint32_t source);
bool emitCooldownReady(std::uint32_t spellId, std::uint32_t startMs,
                       std::uint32_t durationMs, std::uint32_t endMs,
                       std::uint32_t remainingMs, std::uint32_t enable,
                       std::uint32_t kind, std::uint32_t source);

int unitHealthSlot();
int unitPowerSlot();
int unitCombatSlot();
int cooldownStartedSlot();
int cooldownChangedSlot();
int cooldownReadySlot();

} // namespace TysCustomEvents
