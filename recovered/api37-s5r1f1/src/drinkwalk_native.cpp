/*
 * TS DrinkWalk native actuator discovery D1.
 *
 * Scope is intentionally narrow:
 *   - explicit-call diagnostics only;
 *   - no timer, thread, hook, packet forging, or background scheduler;
 *   - resolve a bag/slot through WoW's own PackBagSlot/GetItemBySlot path;
 *   - dispatch the resolved CGItem through WoW's native item-use primitive.
 *
 * A successful dispatch means only that the native primitive was called.
 * It does NOT prove the server accepted/consumed the item. The companion
 * diagnostic addon confirms execution using strong evidence (stack decrease
 * and/or Drink Aura appearance).
 */

#include <windows.h>
#include "drinkwalk_native.h"
#include "wow112_offsets.h"

namespace TysDrinkWalkNative {
namespace {

using PackBagSlotFn = int (__fastcall *)(Lua50::State L, void** outInvMgr,
                                         int* outLinearSlot, int* outUnused);
using GetItemBySlotFn = void* (__thiscall *)(void* invMgr, int linearSlot);
using UseItemFn = unsigned (__thiscall *)(const void* item,
                                          const unsigned long long* targetGuid,
                                          int flag);

static volatile LONG g_resolveCalls = 0;
static volatile LONG g_resolveOk = 0;
static volatile LONG g_resolveFail = 0;
static volatile LONG g_useCalls = 0;
static volatile LONG g_useDispatch = 0;
static volatile LONG g_preflightFail = 0;
static int g_lastBag = -1;
static int g_lastSlot = -1;
static int g_lastLinearSlot = -1;
static unsigned long g_lastItemPtr = 0;
static unsigned long g_lastEngineReturn = 0;
static char g_lastCode[96] = "NOT_CALLED";

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i = 0;
    if (s) for (; s[i] && i + 1 < cap; ++i) d[i] = s[i];
    d[i] = 0;
}

static bool executable(unsigned long address) {
    MEMORY_BASIC_INFORMATION m = {};
    if (!address || VirtualQuery((void*)address, &m, sizeof(m)) != sizeof(m) ||
        m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    DWORD p = m.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ ||
           p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}

static bool preflight() {
    const bool ok = executable(WoW112::PACK_BAG_SLOT) &&
                    executable(WoW112::ITEMMGR_GET_ITEM_BY_SLOT) &&
                    executable(WoW112::ITEM_USE_NATIVE);
    if (!ok) ++g_preflightFail;
    return ok;
}

static void setString(Lua50::State L, const char* key, const char* value) {
    Lua50::PushString(L, key); Lua50::PushString(L, value ? value : ""); Lua50::SetTable(L, -3);
}
static void setNumber(Lua50::State L, const char* key, double value) {
    Lua50::PushString(L, key); Lua50::PushNumber(L, value); Lua50::SetTable(L, -3);
}
static void setBool(Lua50::State L, const char* key, bool value) {
    Lua50::PushString(L, key); Lua50::PushBool(L, value); Lua50::SetTable(L, -3);
}

static char hexDigit(unsigned v) { return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10))); }
static void ptrString(char* out, unsigned cap, unsigned long value) {
    if (!out || cap < 11) return;
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        unsigned shift = (unsigned)(7 - i) * 4U;
        out[2 + i] = hexDigit((value >> shift) & 0xFU);
    }
    out[10] = 0;
}

struct ResolveResult {
    bool ok;
    void* item;
    int linearSlot;
    const char* code;
};

static ResolveResult resolveItem(Lua50::State L, int bag, int slot) {
    ResolveResult r = {false, 0, -1, "UNRESOLVED"};
    ++g_resolveCalls;
    g_lastBag = bag;
    g_lastSlot = slot;
    g_lastLinearSlot = -1;
    g_lastItemPtr = 0;

    if (!L) {
        r.code = "LUA_STATE_MISSING";
        ++g_resolveFail;
        return r;
    }
    if (bag < 0 || bag > 4 || slot < 1 || slot > 128) {
        r.code = "BAD_BAG_SLOT";
        ++g_resolveFail;
        return r;
    }
    if (!preflight()) {
        r.code = "NATIVE_PREFLIGHT_FAILED";
        ++g_resolveFail;
        return r;
    }

    // PackBagSlot expects bag/slot in Lua stack positions 1/2. The caller's
    // TaiYangShenDian command and original arguments are already copied into
    // locals before we replace the stack; this dispatch is terminal.
    Lua50::SetTop(L, 0);
    Lua50::PushNumber(L, (double)bag);
    Lua50::PushNumber(L, (double)slot);

    void* invMgr = 0;
    int linearSlot = -1;
    int unused = 0;
    PackBagSlotFn pack = (PackBagSlotFn)WoW112::PACK_BAG_SLOT;
    const int packed = pack(L, &invMgr, &linearSlot, &unused);
    Lua50::SetTop(L, 0);

    if (!packed || !invMgr || linearSlot < 0) {
        r.code = "PACK_BAG_SLOT_FAILED";
        ++g_resolveFail;
        return r;
    }

    GetItemBySlotFn getItem = (GetItemBySlotFn)WoW112::ITEMMGR_GET_ITEM_BY_SLOT;
    void* item = getItem(invMgr, linearSlot);
    if (!item) {
        r.code = "EMPTY_OR_UNRESOLVED_SLOT";
        ++g_resolveFail;
        return r;
    }

    r.ok = true;
    r.item = item;
    r.linearSlot = linearSlot;
    r.code = "RESOLVED_CGITEM";
    g_lastLinearSlot = linearSlot;
    g_lastItemPtr = (unsigned long)item;
    ++g_resolveOk;
    return r;
}

static bool readBagSlotArgs(Lua50::State L, int* bag, int* slot) {
    if (!L || !bag || !slot || Lua50::GetTop(L) < 3 ||
        !Lua50::IsNumber(L, 2) || !Lua50::IsNumber(L, 3)) return false;
    *bag = (int)Lua50::ToNumber(L, 2);
    *slot = (int)Lua50::ToNumber(L, 3);
    return true;
}

} // namespace

int dispatchStatus(Lua50::State L) {
    if (!L) return 0;
    const bool ready = preflight();
    char pPack[16] = {}, pGet[16] = {}, pUse[16] = {}, pItem[16] = {};
    ptrString(pPack, sizeof(pPack), WoW112::PACK_BAG_SLOT);
    ptrString(pGet, sizeof(pGet), WoW112::ITEMMGR_GET_ITEM_BY_SLOT);
    ptrString(pUse, sizeof(pUse), WoW112::ITEM_USE_NATIVE);
    ptrString(pItem, sizeof(pItem), g_lastItemPtr);

    Lua50::NewTable(L);
    setBool(L, "available", ready);
    setString(L, "mode", "EXPLICIT_NATIVE_DIAGNOSTIC_ONLY");
    setBool(L, "autoSchedulerInstalled", false);
    setBool(L, "hooksInstalled", false);
    setBool(L, "packetForgeUsed", false);
    setString(L, "resolver", "PackBagSlot->GetItemBySlot");
    setString(L, "actuator", "CGItem::UseItem");
    setString(L, "packBagSlot", pPack);
    setString(L, "getItemBySlot", pGet);
    setString(L, "itemUsePrimitive", pUse);
    setNumber(L, "resolveCalls", (double)g_resolveCalls);
    setNumber(L, "resolveOk", (double)g_resolveOk);
    setNumber(L, "resolveFail", (double)g_resolveFail);
    setNumber(L, "useCalls", (double)g_useCalls);
    setNumber(L, "useDispatch", (double)g_useDispatch);
    setNumber(L, "preflightFail", (double)g_preflightFail);
    setNumber(L, "lastBag", (double)g_lastBag);
    setNumber(L, "lastSlot", (double)g_lastSlot);
    setNumber(L, "lastLinearSlot", (double)g_lastLinearSlot);
    setString(L, "lastItemPtr", pItem);
    setNumber(L, "lastEngineReturn", (double)g_lastEngineReturn);
    setString(L, "lastCode", g_lastCode);
    setString(L, "confirmation", "EXTERNAL_STRONG_EVIDENCE_REQUIRED");
    return 1;
}

int dispatchResolveItem(Lua50::State L) {
    int bag = -1, slot = -1;
    if (!readBagSlotArgs(L, &bag, &slot)) {
        Lua50::SetTop(L, 0);
        Lua50::PushBool(L, false);
        Lua50::PushString(L, "BAD_ARGUMENT");
        return 2;
    }

    ResolveResult r = resolveItem(L, bag, slot);
    char pItem[16] = {};
    ptrString(pItem, sizeof(pItem), (unsigned long)r.item);
    copyText(g_lastCode, sizeof(g_lastCode), r.code);
    Lua50::SetTop(L, 0);
    Lua50::PushBool(L, r.ok);
    Lua50::PushString(L, r.code);
    Lua50::PushString(L, pItem);
    Lua50::PushNumber(L, (double)r.linearSlot);
    return 4;
}

int dispatchUseItem(Lua50::State L) {
    int bag = -1, slot = -1;
    if (!readBagSlotArgs(L, &bag, &slot)) {
        Lua50::SetTop(L, 0);
        Lua50::PushBool(L, false);
        Lua50::PushString(L, "BAD_ARGUMENT");
        Lua50::PushNumber(L, 0.0);
        return 3;
    }

    ++g_useCalls;
    ResolveResult r = resolveItem(L, bag, slot);
    if (!r.ok) {
        copyText(g_lastCode, sizeof(g_lastCode), r.code);
        g_lastEngineReturn = 0;
        Lua50::SetTop(L, 0);
        Lua50::PushBool(L, false);
        Lua50::PushString(L, r.code);
        Lua50::PushNumber(L, 0.0);
        return 3;
    }

    const unsigned long long zeroTarget = 0;
    UseItemFn useItem = (UseItemFn)WoW112::ITEM_USE_NATIVE;
    const unsigned ret = useItem(r.item, &zeroTarget, 0);
    g_lastEngineReturn = ret;
    ++g_useDispatch;
    copyText(g_lastCode, sizeof(g_lastCode), "DISPATCHED_NATIVE_PRIMITIVE_UNCONFIRMED");

    Lua50::SetTop(L, 0);
    Lua50::PushBool(L, true);
    Lua50::PushString(L, "DISPATCHED_NATIVE_PRIMITIVE_UNCONFIRMED");
    Lua50::PushNumber(L, (double)ret);
    return 3;
}

} // namespace TysDrinkWalkNative
