/*
 * TaiYangShenDian AURA6-D4-R4 native-only AuraSource facade.
 *
 * There is no provider arbitration and no external-DLL ingress. Caster evidence
 * comes from one source: TysNativeBus observing SMSG_SPELL_GO at the shared
 * incoming packet funnel. Aura slot lifecycle comes from the existing native Aura callbacks and feeds
 * one AuraSourceCore instance cache keyed by (target, spell, caster).
 */
#include <windows.h>
#include "aura_caster.h"
#include "aura_source_core.h"
#include "aura_cast_timing.h"
#include "aura_state.h"
#include "native_bus.h"
#include "wow112_offsets.h"

namespace TysAuraCaster {
namespace {

constexpr unsigned long MAX_AURA_SLOTS = 48UL;
constexpr unsigned long UNIT_FIELDS_PTR_OFF = 0x110UL;
constexpr unsigned long UNIT_GUID_OFF = 0x30UL;
constexpr unsigned long MAX_SPELL_GO_TARGETS = 100UL;

static volatile LONG g_initOnce = 0;
static volatile LONG g_subscribed = 0;
static volatile LONG g_spellGoCount = 0;
static volatile LONG g_matchHit = 0;
static volatile LONG g_matchMiss = 0;
static volatile LONG g_parseFailure = 0;
static volatile LONG g_snapshotCalls = 0;
static volatile LONG g_snapshotKnown = 0;
static volatile LONG g_snapshotUnknown = 0;
static volatile LONG g_autoBindHit = 0;
static volatile LONG g_autoBindMiss = 0;
static volatile LONG g_stackRecoveryHit = 0;
static volatile LONG g_snapshotBindingClears = 0;
static volatile LONG g_spellGoTargetObservations = 0;
static volatile LONG g_spellGoSelfFallbacks = 0;
static char g_backend[48] = "NOT_INITIALIZED";
static char g_status[128] = "NOT_INITIALIZED";

#pragma pack(push, 1)
struct UnitFieldsAuraView {
    unsigned char preAura[0xA4];
    unsigned long aura[48];
    unsigned long auraFlags[6];
    unsigned char auraLevels[48];
    unsigned char auraApplications[48];
};
#pragma pack(pop)

using GetActivePlayerFn = unsigned long long (__stdcall *)();
using ResolveUnitFn = void* (__fastcall *)(const char* token);

static void copyText(char* d, unsigned cap, const char* s) {
    if (!d || !cap) return;
    unsigned i=0; if (s) for (;s[i] && i+1<cap;++i) d[i]=s[i]; d[i]=0;
}

static bool sameTextNoCase(const char* a,const char* b) {
    if (!a || !b) return false;
    for (;;) {
        char ca=*a,cb=*b;
        if (ca>='A'&&ca<='Z') ca=(char)(ca-'A'+'a');
        if (cb>='A'&&cb<='Z') cb=(char)(cb-'A'+'a');
        if (ca!=cb) return false; if (!ca) return true; ++a;++b;
    }
}

static bool readable(const void* ptr,unsigned long size) {
    if (!ptr || !size) return false;
    MEMORY_BASIC_INFORMATION m={};
    if (VirtualQuery(ptr,&m,sizeof(m))!=sizeof(m) || m.State!=MEM_COMMIT ||
        (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    DWORD p=m.Protect&0xff;
    bool r=p==PAGE_READONLY || p==PAGE_READWRITE || p==PAGE_WRITECOPY ||
           p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
    if (!r) return false;
    unsigned long a=(unsigned long)ptr,end=(unsigned long)m.BaseAddress+(unsigned long)m.RegionSize;
    return a+size>=a && a+size<=end;
}

static bool executable(unsigned long address) {
    MEMORY_BASIC_INFORMATION m={};
    if (!address || VirtualQuery((void*)address,&m,sizeof(m))!=sizeof(m) || m.State!=MEM_COMMIT ||
        (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    DWORD p=m.Protect&0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}

static unsigned long long activePlayerGuid() {
    if (!executable(WoW112::GET_ACTIVE_PLAYER_GUID)) return 0;
    return ((GetActivePlayerFn)WoW112::GET_ACTIVE_PLAYER_GUID)();
}

static char hexDigit(unsigned v){return (char)(v<10?('0'+v):('A'+(v-10)));}
static void guidString(char* out,unsigned cap,unsigned long long guid) {
    if (!out || cap<19) return; out[0]='0';out[1]='x';
    for (int i=0;i<16;++i){unsigned shift=(unsigned)(15-i)*4U;out[2+i]=hexDigit((unsigned)((guid>>shift)&0xFULL));}
    out[18]=0;
}
static int hexValue(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static bool parseGuid(const char* s,unsigned long long* out) {
    if (!s || !out) return false; if (s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s+=2;
    unsigned long long v=0;unsigned n=0;while(*s){int h=hexValue(*s++);if(h<0||n>=16)return false;v=(v<<4)|(unsigned long long)h;++n;}
    if(!n)return false;*out=v;return true;
}

static void setNumber(Lua50::State L,const char* k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char* k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static void setString(Lua50::State L,const char* k,const char* v){Lua50::PushString(L,k);Lua50::PushString(L,v?v:"");Lua50::SetTable(L,-3);}

static const char* sourceName(unsigned char sourceKind) {
    if (sourceKind==TysAuraSourceCore::SOURCE_NATIVE_HIT_TARGET) return "PACKET_SPELL_GO_HIT_TARGET";
    if (sourceKind==TysAuraSourceCore::SOURCE_NATIVE_CASTER_SELF) return "PACKET_SPELL_GO_SELF_FALLBACK";
    return "APPLICATION_NOT_OBSERVED";
}
static const char* qualityName(unsigned char sourceKind) {
    if (sourceKind==TysAuraSourceCore::SOURCE_NATIVE_HIT_TARGET) return "OBSERVED_TARGET_HIT";
    if (sourceKind==TysAuraSourceCore::SOURCE_NATIVE_CASTER_SELF) return "OBSERVED_SELF_CAST";
    return "UNKNOWN";
}

static void pushBindingTable(Lua50::State L,const TysAuraSourceCore::InstanceView& b,unsigned long now) {
    Lua50::NewTable(L);
    char caster[20]={0},target[20]={0};guidString(caster,sizeof(caster),b.casterGuid);guidString(target,sizeof(target),b.targetGuid);
    setBool(L,"known",true);setString(L,"casterGuid",caster);setString(L,"targetGuid",target);
    setNumber(L,"spellId",(double)b.spellId);setNumber(L,"rawSlot",(double)b.rawSlot);
    setBool(L,"isMine",b.casterGuid!=0 && b.casterGuid==activePlayerGuid());
    setString(L,"source",sourceName(b.sourceKind));setString(L,"quality",qualityName(b.sourceKind));
    setNumber(L,"ageMs",(double)(now-b.tick));
    setNumber(L,"correlationWindowMs",(double)TysAuraSourceCore::correlationWindowMs());
    setNumber(L,"instanceCapacity",(double)TysAuraSourceCore::instanceCapacity());
    setNumber(L,"pendingCapacity",(double)TysAuraSourceCore::pendingCapacity());
}

static void onIncomingPacket(unsigned long opcode, TysNativeBus::CDataStoreView* packet) {
    if (opcode != WoW112::SMSG_SPELL_GO_OPCODE || !packet) return;
    ++g_spellGoCount;

    unsigned long long itemGuid=0,caster=0;
    unsigned long spellId=0;
    short castFlags=0;
    unsigned char hitCount=0;
    if (!TysNativeBus::readPackedGuid(packet,&itemGuid) ||
        !TysNativeBus::readPackedGuid(packet,&caster) ||
        !TysNativeBus::read(packet,&spellId) ||
        !TysNativeBus::read(packet,&castFlags) ||
        !TysNativeBus::read(packet,&hitCount) || !caster || !spellId) {
        ++g_parseFailure; return;
    }

    const unsigned long now=GetTickCount();
    const TysAuraCastTiming::Evidence timing =
        TysAuraCastTiming::resolve(caster, spellId, now);
    const unsigned long predictedDuration = timing.valid ? timing.durationMs : 0UL;
    const unsigned char timeSource = timing.valid
        ? (unsigned char)timing.source
        : (unsigned char)TysAuraSourceCore::TIME_SOURCE_NONE;
    unsigned long goodTargets=0;
    const unsigned long count=(hitCount>MAX_SPELL_GO_TARGETS)?MAX_SPELL_GO_TARGETS:hitCount;
    for(unsigned long i=0;i<count;++i) {
        unsigned long long target=0;
        if(!TysNativeBus::read(packet,&target)) { ++g_parseFailure; return; }
        if(!target) continue;
        TysAuraSourceCore::observeCast(target,caster,spellId,now,
            TysAuraSourceCore::SOURCE_NATIVE_HIT_TARGET,predictedDuration,timeSource);
        ++goodTargets; ++g_spellGoTargetObservations;
    }
    // If the server supplied more hit targets than our safety cap, consume them
    // so this subscriber validates the packet body without changing its cursor
    // semantics for itself. NativeBus restores the cursor for every subscriber.
    for(unsigned long i=count;i<(unsigned long)hitCount;++i) {
        unsigned long long ignored=0;
        if(!TysNativeBus::read(packet,&ignored)) { ++g_parseFailure; return; }
    }

    // Self/implicit-target casts can carry an empty hit list. Use the caster as
    // target only in that case; do not create the old unconditional caster-self
    // candidate for ordinary targeted casts.
    if(goodTargets==0 && hitCount==0) {
        TysAuraSourceCore::observeCast(caster,caster,spellId,now,
            TysAuraSourceCore::SOURCE_NATIVE_CASTER_SELF,predictedDuration,timeSource);
        ++g_spellGoSelfFallbacks;
    }
}

} // namespace

bool initialize() {
    if(InterlockedCompareExchange(&g_initOnce,1,0)!=0)
        return InterlockedCompareExchange(&g_subscribed,0,0)!=0;
    TysAuraSourceCore::initialize();
    copyText(g_backend,sizeof(g_backend),"TYS_AURA_SOURCE_CORE");
    if(!TysNativeBus::incomingHookInstalled()) {
        copyText(g_status,sizeof(g_status),"NATIVE_BUS_NOT_READY"); return false;
    }
    if(!TysAuraCastTiming::initialize()) {
        copyText(g_status,sizeof(g_status),"AURA_CAST_TIMING_NOT_READY"); return false;
    }
    if(!TysNativeBus::subscribeIncoming(&onIncomingPacket)) {
        copyText(g_status,sizeof(g_status),"NATIVE_BUS_SUBSCRIBE_SPELL_GO_FAILED"); return false;
    }
    InterlockedExchange(&g_subscribed,1);
    copyText(g_status,sizeof(g_status),"READY_AURA_SOURCE_CORE_FIFO_TARGET_TIME");
    return true;
}

int dispatchStatus(Lua50::State L) {
    if(!L)return 0;
    TysAuraSourceCore::Stats s=TysAuraSourceCore::stats();
    Lua50::NewTable(L);
    setString(L,"backend",g_backend);setString(L,"status",g_status);
    setBool(L,"coreInitialized",InterlockedCompareExchange(&g_initOnce,0,0)!=0);
    setBool(L,"packetSubscriberInstalled",InterlockedCompareExchange(&g_subscribed,0,0)!=0);
    setBool(L,"perOpcodeSpellGoHookInstalled",false);
    setString(L,"architecture","NATIVE_BUS_PACKET_DISPATCH_TO_AURA_SOURCE_CORE");
    setString(L,"source","SMSG_SPELL_GO_PACKET_DISPATCH_PLUS_NATIVE_AURA_LIFECYCLE");
    setString(L,"identityPolicy","TARGET_GUID_PLUS_SPELL_ID_PLUS_CASTER_GUID");
    setString(L,"seatPolicy","FIFO_OLDEST_UNCONSUMED_APPLICATION");
    setString(L,"queryPolicy","READ_SIDE_NEVER_CONSUMES_PENDING");
    setString(L,"stackPolicy","BOUND_SLOT_OWNER_WINS; SAME_CASTER_PENDING_ONLY_REFRESH_CONFIRM");
    setString(L,"descriptorPolicy","UNITFIELDS_PRESENCE_AUTHORITY; EMPTY_DESCRIPTOR_PRESERVES_CACHE");
    setString(L,"auraSlotCasterField","NOT_PRESENT_STANDARD_1_12_UNITFIELDS");
    setString(L,"qualityPolicy","KNOWN_ONLY_WHEN_APPLICATION_WAS_OBSERVED; OTHERWISE_UNKNOWN");
    setString(L,"lifecyclePolicy","R4_REFRESH_FIFO_PLUS_WORLD_RESET_PLUS_STALE_REMOVE_GUARD");
    setNumber(L,"correlationWindowMs",(double)TysAuraSourceCore::correlationWindowMs());
    setNumber(L,"instanceCapacity",(double)TysAuraSourceCore::instanceCapacity());
    setNumber(L,"pendingCapacity",(double)TysAuraSourceCore::pendingCapacity());
    setNumber(L,"activeInstances",(double)TysAuraSourceCore::activeInstanceCount());
    setNumber(L,"activePending",(double)TysAuraSourceCore::activePendingCount(GetTickCount()));
    setNumber(L,"spellGoCount",(double)InterlockedCompareExchange(&g_spellGoCount,0,0));
    setNumber(L,"spellGoTargetObservations",(double)InterlockedCompareExchange(&g_spellGoTargetObservations,0,0));
    setNumber(L,"spellGoSelfFallbacks",(double)InterlockedCompareExchange(&g_spellGoSelfFallbacks,0,0));
    setNumber(L,"matchHit",(double)InterlockedCompareExchange(&g_matchHit,0,0));
    setNumber(L,"matchMiss",(double)InterlockedCompareExchange(&g_matchMiss,0,0));
    setNumber(L,"parseFailure",(double)InterlockedCompareExchange(&g_parseFailure,0,0));
    setNumber(L,"snapshotCalls",(double)InterlockedCompareExchange(&g_snapshotCalls,0,0));
    setNumber(L,"snapshotKnown",(double)InterlockedCompareExchange(&g_snapshotKnown,0,0));
    setNumber(L,"snapshotUnknown",(double)InterlockedCompareExchange(&g_snapshotUnknown,0,0));
    setNumber(L,"autoBindHit",(double)InterlockedCompareExchange(&g_autoBindHit,0,0));
    setNumber(L,"autoBindMiss",(double)InterlockedCompareExchange(&g_autoBindMiss,0,0));
    setNumber(L,"stackRecoveryHit",(double)InterlockedCompareExchange(&g_stackRecoveryHit,0,0));
    setNumber(L,"snapshotInstanceClears",(double)InterlockedCompareExchange(&g_snapshotBindingClears,0,0));
    setNumber(L,"castObservations",(double)s.castObservations);
    setNumber(L,"pendingWrites",(double)s.pendingWrites);
    setNumber(L,"pendingConsumed",(double)s.pendingConsumed);
    setNumber(L,"pendingExpired",(double)s.pendingExpired);
    setNumber(L,"pendingCapacityDrops",(double)s.pendingCapacityDrops);
    setNumber(L,"fifoBindings",(double)s.fifoBindings);
    setNumber(L,"instanceWrites",(double)s.instanceWrites);
    setNumber(L,"instanceRefreshes",(double)s.instanceRefreshes);
    setNumber(L,"instanceRebinds",(double)s.instanceRebinds);
    setNumber(L,"instanceClears",(double)s.instanceClears);
    setNumber(L,"instanceCapacityDrops",(double)s.instanceCapacityDrops);
    setNumber(L,"removeHits",(double)s.removeHits);
    setNumber(L,"removeMisses",(double)s.removeMisses);
    setNumber(L,"addPreclears",(double)s.addPreclears);
    setNumber(L,"existingInstanceReturns",(double)s.existingInstanceReturns);
    setNumber(L,"retainedInstanceReturns",(double)s.retainedInstanceReturns);
    setNumber(L,"queryHits",(double)s.queryHits);
    setNumber(L,"queryMisses",(double)s.queryMisses);
    setNumber(L,"descriptorReconciles",(double)s.descriptorReconciles);
    setNumber(L,"descriptorEmptyPreserves",(double)s.descriptorEmptyPreserves);
    setNumber(L,"descriptorUnbinds",(double)s.descriptorUnbinds);
    setNumber(L,"descriptorClears",(double)s.descriptorClears);
    setNumber(L,"ambiguousRetainedMisses",(double)s.ambiguousRetainedMisses);
    setNumber(L,"timedCastObservations",(double)s.timedCastObservations);
    setNumber(L,"castRefreshes",(double)s.castRefreshes);
    setNumber(L,"refreshPendingWrites",(double)s.refreshPendingWrites);
    setNumber(L,"refreshBindings",(double)s.refreshBindings);
    setNumber(L,"staleRemoveIgnores",(double)s.staleRemoveIgnores);
    setNumber(L,"worldResets",(double)s.worldResets);
    TysAuraCastTiming::Stats ts=TysAuraCastTiming::stats();
    setString(L,"targetTimingStatus",TysAuraCastTiming::status());
    setNumber(L,"comboCaptures",(double)ts.comboCaptures);
    setNumber(L,"comboConsumes",(double)ts.comboConsumes);
    setNumber(L,"localDurations",(double)ts.localDurations);
    setNumber(L,"localComboDurations",(double)ts.localComboDurations);
    setNumber(L,"remoteBaseDurations",(double)ts.remoteBaseDurations);
    setNumber(L,"durationMisses",(double)ts.durationMisses);
    char pg[20]={0};guidString(pg,sizeof(pg),activePlayerGuid());setString(L,"playerGuid",pg);
    return 1;
}

int dispatchMatch(Lua50::State L) {
    if(!L||Lua50::GetTop(L)<4||!Lua50::IsString(L,2)||!Lua50::IsNumber(L,3)||!Lua50::IsNumber(L,4)) {Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    unsigned long long target=0;if(!parseGuid(Lua50::ToString(L,2),&target)||!target){Lua50::PushNil(L);Lua50::PushString(L,"GUID_INVALID");return 2;}
    unsigned long spellId=(unsigned long)Lua50::ToNumber(L,3),rawSlot=(unsigned long)Lua50::ToNumber(L,4);
    if(!spellId||rawSlot>=MAX_AURA_SLOTS){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    unsigned long now=GetTickCount();TysAuraSourceCore::InstanceView b={};TysAuraSourceCore::Resolution resolution=TysAuraSourceCore::RESOLUTION_NONE;
    if(TysAuraSourceCore::match(target,spellId,rawSlot,now,&b,&resolution)){++g_matchHit;pushBindingTable(L,b,now);return 1;}
    ++g_matchMiss;Lua50::PushNil(L);Lua50::PushString(L,"AURA_SOURCE_UNKNOWN_NO_OBSERVED_APPLICATION");return 2;
}

int dispatchSnapshot(Lua50::State L) {
    if(!L||Lua50::GetTop(L)<2||!Lua50::IsString(L,2)){Lua50::PushNil(L);Lua50::PushString(L,"BAD_ARGUMENT");return 2;}
    const char* unit=Lua50::ToString(L,2);
    if(!sameTextNoCase(unit,"player")&&!sameTextNoCase(unit,"target")){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_NOT_SUPPORTED");return 2;}
    ++g_snapshotCalls;
    if(!executable(WoW112::RESOLVE_UNIT_TOKEN)){Lua50::PushNil(L);Lua50::PushString(L,"RESOLVE_UNIT_UNAVAILABLE");return 2;}
    void* obj=((ResolveUnitFn)WoW112::RESOLVE_UNIT_TOKEN)(unit);
    if(!obj||!readable((unsigned char*)obj+UNIT_GUID_OFF,8)||!readable((unsigned char*)obj+UNIT_FIELDS_PTR_OFF,4)){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_UNAVAILABLE");return 2;}
    unsigned long long guid=*(unsigned long long*)((unsigned char*)obj+UNIT_GUID_OFF);
    UnitFieldsAuraView* f=*(UnitFieldsAuraView**)((unsigned char*)obj+UNIT_FIELDS_PTR_OFF);
    if(!guid||!f||!readable(f,sizeof(UnitFieldsAuraView))){Lua50::PushNil(L);Lua50::PushString(L,"UNIT_FIELDS_UNAVAILABLE");return 2;}

    TysAuraSourceCore::Stats before=TysAuraSourceCore::stats();
    TysAuraSourceCore::cleanupVisibleInstances(guid,f->aura);
    TysAuraSourceCore::Stats after=TysAuraSourceCore::stats();
    if(after.instanceClears>before.instanceClears) g_snapshotBindingClears+=(LONG)(after.instanceClears-before.instanceClears);

    Lua50::NewTable(L);char gs[20]={0};guidString(gs,sizeof(gs),guid);
    setString(L,"unit",unit);setString(L,"guid",gs);setString(L,"backend",g_backend);
    setString(L,"source","D4_R2_AURA_SOURCE_CORE_VIA_NATIVE_BUS");
    unsigned long row=0,known=0,unknown=0;unsigned long now=GetTickCount();
    for(unsigned long slot=0;slot<MAX_AURA_SLOTS;++slot) {
        unsigned long spell=f->aura[slot];if(!spell)continue;
        ++row;Lua50::PushNumber(L,(double)row);Lua50::NewTable(L);
        setNumber(L,"rawSlot",(double)slot);setNumber(L,"spellId",(double)spell);
        TysAuraSourceCore::InstanceView b={};TysAuraSourceCore::Resolution resolution=TysAuraSourceCore::RESOLUTION_NONE;
        bool k=TysAuraSourceCore::match(guid,spell,slot,now,&b,&resolution);setBool(L,"known",k);
        if(k){char cs[20]={0};guidString(cs,sizeof(cs),b.casterGuid);setString(L,"casterGuid",cs);setBool(L,"isMine",b.casterGuid==activePlayerGuid());setString(L,"quality",qualityName(b.sourceKind));setString(L,"source",sourceName(b.sourceKind));++known;}
        else{setString(L,"casterGuid","");setBool(L,"isMine",false);setString(L,"quality","UNKNOWN");setString(L,"source","APPLICATION_NOT_OBSERVED");++unknown;}
        Lua50::SetTable(L,-3);
    }
    setNumber(L,"count",(double)row);setNumber(L,"knownCount",(double)known);setNumber(L,"unknownCount",(double)unknown);
    g_snapshotKnown+=(LONG)known;g_snapshotUnknown+=(LONG)unknown;return 1;
}

void onWorldLeaving() {
    TysAuraSourceCore::resetWorldState();
    TysAuraCastTiming::onWorldLeaving();
    TysAuraState::onWorldLeaving();
}

void onAuraAdded(unsigned long long targetGuid,unsigned long spellId,unsigned long rawSlot) {
    if(InterlockedCompareExchange(&g_subscribed,0,0)==0||!targetGuid||!spellId||rawSlot>=MAX_AURA_SLOTS)return;
    unsigned long now=GetTickCount();
    TysAuraSourceCore::ObserveResult r=TysAuraSourceCore::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_ADD,now);
    TysAuraState::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_ADD,now);
    if(r.bound)++g_autoBindHit;else ++g_autoBindMiss;
}

void onAuraRemoved(unsigned long long targetGuid,unsigned long spellId,unsigned long rawSlot) {
    if(InterlockedCompareExchange(&g_subscribed,0,0)==0||!targetGuid||!spellId||rawSlot>=MAX_AURA_SLOTS)return;
    unsigned long now=GetTickCount();
    TysAuraSourceCore::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_REMOVE,now);
    TysAuraState::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_REMOVE,now);
}

void onAuraStackChanged(unsigned long long targetGuid,unsigned long spellId,unsigned long rawSlot) {
    if(InterlockedCompareExchange(&g_subscribed,0,0)==0||!targetGuid||!spellId||rawSlot>=MAX_AURA_SLOTS)return;
    unsigned long now=GetTickCount();
    TysAuraSourceCore::ObserveResult r=TysAuraSourceCore::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_STACK,now);
    TysAuraState::observeAura(targetGuid,spellId,rawSlot,TysAuraSourceCore::AURA_STACK,now);
    if(r.bound && r.resolution!=TysAuraSourceCore::RESOLUTION_EXISTING_INSTANCE){++g_autoBindHit;++g_stackRecoveryHit;}
}

const char* backend(){return g_backend;}
const char* status(){return g_status;}
bool hookInstalled(){return InterlockedCompareExchange(&g_subscribed,0,0)!=0 && TysNativeBus::incomingHookInstalled();}

} // namespace TysAuraCaster
