#pragma once
#include "lua50.h"

namespace TysNativeBus {

struct CDataStoreView {
    void* vtable;
    unsigned char* buffer;
    unsigned long base;
    unsigned long alloc;
    unsigned long size;
    unsigned long read;
};

typedef void (*PacketCallback)(unsigned long opcode, CDataStoreView* packet);
typedef void (*TickCallback)();

bool initialize();
bool subscribeIncoming(PacketCallback cb);
bool subscribeOutgoing(PacketCallback cb);
bool subscribeWorldTick(TickCallback cb);

bool incomingHookInstalled();
bool outgoingHookInstalled();
bool worldTickHookInstalled();
const char* status();

int dispatchStatus(Lua50::State L);

// Read from the packet at its current cursor. The NativeBus restores the cursor
// before every subscriber and before returning to the game, so subscribers are
// observational and independent from each other.
bool readBytes(CDataStoreView* packet, void* out, unsigned long size);

template <typename T>
bool read(CDataStoreView* packet, T* out) {
    return readBytes(packet, out, (unsigned long)sizeof(T));
}

bool readPackedGuid(CDataStoreView* packet, unsigned long long* out);

} // namespace TysNativeBus
