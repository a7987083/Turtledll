#pragma once
#include "lua50.h"

namespace TysAuraNative {

// Early process-lifetime initialization. Must run before FrameScript creates the
// global event table. D4-R1 always owns the single TYS native Aura path.
bool initialize();

// Backward-compatible status surface from AURA1.
int dispatchStatus(Lua50::State L);

// AURA3 diagnostics. Returns one Lua table with backend integrity, module state,
// live hook ownership probes, native event counters and the last native event.
// This is read-only and installs no hook / timer / polling path.
int dispatchDiagnostics(Lua50::State L);

// AURA3 formal one-shot state snapshot. Usage:
//   TaiYangShenDian("Aura.Snapshot", "target")
//   TaiYangShenDian("Aura.Snapshot", "player")
// Returns one Lua table containing numeric Aura rows plus stable metadata fields.
// AURA3 adds generation/capture time and Buff/Debuff counts without removing any
// AURA2 fields. No background scan is created; 48 raw slots are read only on call.
int dispatchSnapshot(Lua50::State L);

const char* backend();
const char* status();
bool nativeHooksInstalled();
bool eventsReady();

} // namespace TysAuraNative
