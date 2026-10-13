#pragma once

namespace TysMoonRuntimeGuard {

// Faithful migration of the MoonMarker-owned runtime ABI guard. It performs
// one-shot validation only; it installs no hook, timer, thread or background work.
bool initialize();
bool enabled();
void markHookInstallFailed(const char* detailText);
const char* statusCode();
const char* userMessage();
const char* detail();
const char* fingerprint();

} // namespace TysMoonRuntimeGuard
