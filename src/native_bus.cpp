/*
 * TaiYangShenDian AURA6-D4-R1 NativeBus.
 *
 * Single-owner transport/tick funnels for WoW 1.12.1 build 5875:
 *   - incoming SMSG dispatch funnel
 *   - outgoing CMSG send funnel
 *   - world tick
 *
 * Feature modules subscribe read-only callbacks instead of owning per-opcode
 * network hooks. Packet cursors are restored around every callback and before
 * the original engine function resumes.
 */
#include <windows.h>
#include "../third_party/minhook/include/MinHook.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysNativeBus {
namespace {

constexpr unsigned MAX_PACKET_SUBSCRIBERS = 24;
constexpr unsigned MAX_TICK_SUBSCRIBERS = 24;

static PacketCallback g_incoming[MAX_PACKET_SUBSCRIBERS] = {};
static PacketCallback g_outgoing[MAX_PACKET_SUBSCRIBERS] = {};
static TickCallback g_ticks[MAX_TICK_SUBSCRIBERS] = {};
static unsigned g_incomingCount = 0;
static unsigned g_outgoingCount = 0;
static unsigned g_tickCount = 0;

static volatile LONG g_initOnce = 0;
static volatile LONG g_inHook = 0;
static volatile LONG g_outHook = 0;
static volatile LONG g_tickHook = 0;
static volatile LONG g_inPackets = 0;
static volatile LONG g_outPackets = 0;
static volatile LONG g_worldTicks = 0;
static volatile LONG g_cursorOverruns = 0;
static volatile LONG g_callbackCalls = 0;
static unsigned long g_lastIncomingOpcode = 0;
static unsigned long g_lastOutgoingOpcode = 0;
static char g_status[128] = "NOT_INITIALIZED";

using IncomingDispatchFn = void (__fastcall *)(void* self, void* edx,
                                                unsigned long param1,
                                                CDataStoreView* packet);
using OutgoingSendFn = void (__fastcall *)(void* conn, void* edx,
                                           CDataStoreView* packet);
using WorldTickFn = void (__fastcall *)(int ecxArg, int edxArg, int stackArg);

static IncomingDispatchFn g_nextIncoming = 0;
static OutgoingSendFn g_nextOutgoing = 0;
static WorldTickFn g_nextWorldTick = 0;
static bool g_createdIncoming = false;
static bool g_createdOutgoing = false;
static bool g_createdTick = false;

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i = 0;
    if (s) for (; s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
}

static bool readable(const void* ptr, unsigned long size) {
    if (!ptr || !size) return false;
    MEMORY_BASIC_INFORMATION m = {};
    if (VirtualQuery(ptr, &m, sizeof(m)) != sizeof(m) || m.State != MEM_COMMIT ||
        (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    bool ok = p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
              p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE ||
              p == PAGE_EXECUTE_WRITECOPY;
    if (!ok) return false;
    unsigned long a = (unsigned long)ptr;
    unsigned long end = (unsigned long)m.BaseAddress + (unsigned long)m.RegionSize;
    return a + size >= a && a + size <= end;
}

static bool executable(unsigned long address) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!address || VirtualQuery((void*)address, &m, sizeof(m)) != sizeof(m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static bool packetShapeOk(CDataStoreView* p) {
    if (!p || !readable(p, sizeof(CDataStoreView)) || !p->buffer) return false;
    if (p->base > p->size || p->read < p->base || p->read > p->size) return false;
    unsigned long bytes = p->size - p->base;
    return bytes == 0 || readable(p->buffer, bytes);
}

static bool readAt(CDataStoreView* p, unsigned long* cursor, void* out, unsigned long n) {
    if (!p || !cursor || !out || !n || !packetShapeOk(p)) return false;
    unsigned long pos = *cursor;
    if (pos < p->base || pos > p->size || n > p->size - pos) return false;
    const unsigned char* src = p->buffer + (pos - p->base);
    if (!readable(src, n)) return false;
    unsigned char* d = (unsigned char*)out;
    for (unsigned long i = 0; i < n; ++i) d[i] = src[i];
    *cursor = pos + n;
    return true;
}

static void fanOut(PacketCallback* list, unsigned count, unsigned long opcode,
                   CDataStoreView* packet, unsigned long bodyRead) {
    if (!packet) return;
    for (unsigned i = 0; i < count; ++i) {
        PacketCallback cb = list[i];
        if (!cb) continue;
        packet->read = bodyRead;
        cb(opcode, packet);
        ++g_callbackCalls;
        if (packet->read > packet->size) ++g_cursorOverruns;
    }
}

static void __fastcall incomingHook(void* self, void* edx, unsigned long param1,
                                    CDataStoreView* packet) {
    ++g_inPackets;
    if (packet && packetShapeOk(packet)) {
        const unsigned long saved = packet->read;
        unsigned long cursor = saved;
        unsigned short opcode16 = 0;
        if (readAt(packet, &cursor, &opcode16, sizeof(opcode16))) {
            g_lastIncomingOpcode = opcode16;
            fanOut(g_incoming, g_incomingCount, (unsigned long)opcode16, packet, cursor);
        }
        packet->read = saved;
    }
    if (g_nextIncoming) g_nextIncoming(self, edx, param1, packet);
}

static void __fastcall outgoingHook(void* conn, void* edx, CDataStoreView* packet) {
    ++g_outPackets;
    if (packet && packetShapeOk(packet)) {
        const unsigned long saved = packet->read;
        unsigned long cursor = saved;
        unsigned long opcode = 0;
        if (readAt(packet, &cursor, &opcode, sizeof(opcode))) {
            g_lastOutgoingOpcode = opcode;
            fanOut(g_outgoing, g_outgoingCount, opcode, packet, cursor);
        }
        packet->read = saved;
    }
    if (g_nextOutgoing) g_nextOutgoing(conn, edx, packet);
}

static void __fastcall worldTickHook(int a, int b, int c) {
    if (g_nextWorldTick) g_nextWorldTick(a, b, c);
    ++g_worldTicks;
    for (unsigned i = 0; i < g_tickCount; ++i) {
        TickCallback cb = g_ticks[i];
        if (cb) { cb(); ++g_callbackCalls; }
    }
}

static void rollback() {
    if (g_createdTick) {
        MH_DisableHook((void*)WoW112::WORLD_TICK);
        MH_RemoveHook((void*)WoW112::WORLD_TICK);
        g_createdTick = false; g_nextWorldTick = 0;
    }
    if (g_createdOutgoing) {
        MH_DisableHook((void*)WoW112::NET_SEND);
        MH_RemoveHook((void*)WoW112::NET_SEND);
        g_createdOutgoing = false; g_nextOutgoing = 0;
    }
    if (g_createdIncoming) {
        MH_DisableHook((void*)WoW112::NET_MESSAGE_DISPATCH);
        MH_RemoveHook((void*)WoW112::NET_MESSAGE_DISPATCH);
        g_createdIncoming = false; g_nextIncoming = 0;
    }
    InterlockedExchange(&g_inHook, 0);
    InterlockedExchange(&g_outHook, 0);
    InterlockedExchange(&g_tickHook, 0);
}

static bool createEnable(unsigned long target, void* hook, void** original, bool* created) {
    MH_STATUS cr = MH_CreateHook((void*)target, hook, original);
    if (cr != MH_OK) return false;
    *created = true;
    MH_STATUS en = MH_EnableHook((void*)target);
    return en == MH_OK || en == MH_ERROR_ENABLED;
}

static void setNumber(Lua50::State L,const char* k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char* k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static void setString(Lua50::State L,const char* k,const char* v){Lua50::PushString(L,k);Lua50::PushString(L,v?v:"");Lua50::SetTable(L,-3);}

} // namespace

bool initialize() {
    if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0)
        return incomingHookInstalled() && outgoingHookInstalled() && worldTickHookInstalled();

    if (!executable(WoW112::NET_MESSAGE_DISPATCH) ||
        !executable(WoW112::NET_SEND) ||
        !executable(WoW112::WORLD_TICK)) {
        copyText(g_status, sizeof(g_status), "NATIVE_BUS_PREFLIGHT_FAILED");
        return false;
    }

    MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        copyText(g_status, sizeof(g_status), "NATIVE_BUS_MH_INIT_FAILED");
        return false;
    }

    if (!createEnable(WoW112::NET_MESSAGE_DISPATCH, (void*)&incomingHook,
                      (void**)&g_nextIncoming, &g_createdIncoming)) {
        copyText(g_status, sizeof(g_status), "HOOK_NET_MESSAGE_DISPATCH_FAILED");
        rollback(); return false;
    }
    InterlockedExchange(&g_inHook, 1);

    if (!createEnable(WoW112::NET_SEND, (void*)&outgoingHook,
                      (void**)&g_nextOutgoing, &g_createdOutgoing)) {
        copyText(g_status, sizeof(g_status), "HOOK_NET_SEND_FAILED");
        rollback(); return false;
    }
    InterlockedExchange(&g_outHook, 1);

    if (!createEnable(WoW112::WORLD_TICK, (void*)&worldTickHook,
                      (void**)&g_nextWorldTick, &g_createdTick)) {
        copyText(g_status, sizeof(g_status), "HOOK_WORLD_TICK_FAILED");
        rollback(); return false;
    }
    InterlockedExchange(&g_tickHook, 1);

    copyText(g_status, sizeof(g_status), "READY_NATIVE_BUS_3_FUNNELS");
    return true;
}

bool subscribeIncoming(PacketCallback cb) {
    if (!cb) return false;
    for (unsigned i=0;i<g_incomingCount;++i) if (g_incoming[i]==cb) return true;
    if (g_incomingCount >= MAX_PACKET_SUBSCRIBERS) return false;
    g_incoming[g_incomingCount++] = cb; return true;
}

bool subscribeOutgoing(PacketCallback cb) {
    if (!cb) return false;
    for (unsigned i=0;i<g_outgoingCount;++i) if (g_outgoing[i]==cb) return true;
    if (g_outgoingCount >= MAX_PACKET_SUBSCRIBERS) return false;
    g_outgoing[g_outgoingCount++] = cb; return true;
}

bool subscribeWorldTick(TickCallback cb) {
    if (!cb) return false;
    for (unsigned i=0;i<g_tickCount;++i) if (g_ticks[i]==cb) return true;
    if (g_tickCount >= MAX_TICK_SUBSCRIBERS) return false;
    g_ticks[g_tickCount++] = cb; return true;
}

bool incomingHookInstalled(){return InterlockedCompareExchange(&g_inHook,0,0)!=0;}
bool outgoingHookInstalled(){return InterlockedCompareExchange(&g_outHook,0,0)!=0;}
bool worldTickHookInstalled(){return InterlockedCompareExchange(&g_tickHook,0,0)!=0;}
const char* status(){return g_status;}

bool readBytes(CDataStoreView* packet, void* out, unsigned long n) {
    if (!packet || !out || !n || !packetShapeOk(packet)) return false;
    unsigned long cursor = packet->read;
    if (!readAt(packet, &cursor, out, n)) {
        packet->read = packet->size + 1;
        return false;
    }
    packet->read = cursor;
    return true;
}

bool readPackedGuid(CDataStoreView* packet, unsigned long long* out) {
    if (!packet || !out) return false;
    unsigned char mask = 0;
    if (!read(packet, &mask)) return false;
    unsigned long long guid = 0;
    for (unsigned i=0;i<8;++i) {
        if ((mask & (1U<<i)) == 0) continue;
        unsigned char b = 0;
        if (!read(packet, &b)) return false;
        guid |= ((unsigned long long)b) << (i*8U);
    }
    *out = guid;
    return true;
}

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    Lua50::NewTable(L);
    setString(L,"status",g_status);
    setString(L,"architecture","SINGLE_OWNER_NATIVE_FUNNELS_READ_ONLY_SUBSCRIBERS");
    setBool(L,"incomingHookInstalled",incomingHookInstalled());
    setBool(L,"outgoingHookInstalled",outgoingHookInstalled());
    setBool(L,"worldTickHookInstalled",worldTickHookInstalled());
    setNumber(L,"incomingSubscribers",(double)g_incomingCount);
    setNumber(L,"outgoingSubscribers",(double)g_outgoingCount);
    setNumber(L,"worldTickSubscribers",(double)g_tickCount);
    setNumber(L,"incomingPackets",(double)InterlockedCompareExchange(&g_inPackets,0,0));
    setNumber(L,"outgoingPackets",(double)InterlockedCompareExchange(&g_outPackets,0,0));
    setNumber(L,"worldTicks",(double)InterlockedCompareExchange(&g_worldTicks,0,0));
    setNumber(L,"callbackCalls",(double)InterlockedCompareExchange(&g_callbackCalls,0,0));
    setNumber(L,"cursorOverruns",(double)InterlockedCompareExchange(&g_cursorOverruns,0,0));
    setNumber(L,"lastIncomingOpcode",(double)g_lastIncomingOpcode);
    setNumber(L,"lastOutgoingOpcode",(double)g_lastOutgoingOpcode);
    setNumber(L,"incomingHookAddress",(double)WoW112::NET_MESSAGE_DISPATCH);
    setNumber(L,"outgoingHookAddress",(double)WoW112::NET_SEND);
    setNumber(L,"worldTickHookAddress",(double)WoW112::WORLD_TICK);
    return 1;
}

} // namespace TysNativeBus
