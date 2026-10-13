#pragma once
#include "lua50.h"

// API-compatible B1R5R8 handoff re-migration.
namespace TysProfiler {
// Returns >=0 when command belongs to profiler/profilerdeep, otherwise -1.
int dispatch(Lua50::State L,const char* command);

// True only while Light or Targeted Deep actually needs native frame boundaries.
bool needsFrameBoundary();
void onFrameBoundary();

// World/session cleanup. Uses only the current Lua state passed by the caller to
// remove a hook; it never dereferences a cached state from a previous UI VM.
void onWorldLeaving(Lua50::State currentState);
void onWorldEntering(Lua50::State currentState);

// Full process cleanup (DLL detach is normally process teardown, so this is only
// defensive and must not call into a stale Lua VM).
void shutdown();
}
