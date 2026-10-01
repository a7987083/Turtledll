#include <windows.h>
#include "moonmarker_runtime_guard.h"

namespace TysMoonRuntimeGuard {
namespace {

constexpr unsigned long EXPECTED_IMAGE_BASE = 0x00400000UL;
constexpr unsigned long EXPECTED_TIMESTAMP  = 0x4510B6DBUL;

// Exact MoonMarker-owned executable address set from the TRUE 8x8 handoff.
static const unsigned long CODE_ADDRESSES[] = {
    0x006E60F0UL,
    0x00523D20UL, 0x00523C20UL, 0x00514810UL, 0x00515090UL,
    0x006F34A0UL, 0x006F3720UL,
    0x00648DD0UL, 0x006477C0UL, 0x00648460UL, 0x006487F0UL,
    0x00648730UL, 0x00648EF0UL,
    0x00483EE0UL, 0x0041ADE0UL,
    0x00707350UL, 0x007103A0UL, 0x00710450UL, 0x00710620UL,
    0x00710B90UL, 0x00710C50UL, 0x00710CB0UL
};
static const unsigned long ADDITIONAL_CODE_ADDRESSES[] = { 0x00710CF0UL };
static const unsigned long SEQUENCE_CODE_ADDRESSES[]   = { 0x007121A0UL };
static const unsigned long DATA_ADDRESSES[] = {
    0x00CECAC0UL, 0x00BE1148UL, 0x00CF0BC8UL,
    0x00C7B298UL, 0x00B4B2BCUL
};

enum State {
    STATE_UNKNOWN = 0,
    STATE_READY,
    STATE_UNSUPPORTED_CLIENT,
    STATE_ADDRESS_UNREADABLE,
    STATE_HOOK_INSTALL_FAILED
};

static State g_state = STATE_UNKNOWN;
static char g_detail[96] = "";
static char g_fingerprint[24] = "";

static unsigned long slen(const char* s){ unsigned long n=0; if(s)while(s[n])++n; return n; }
static void cpy(char* out,unsigned long cap,const char* s){ if(!cap)return; unsigned long i=0; if(s){for(;s[i]&&i+1<cap;++i)out[i]=s[i];} out[i]=0; }
static char hexDigit(unsigned v){ return (char)(v<10?('0'+v):('A'+v-10)); }
static void hex8(char* out,unsigned long v){ for(int i=0;i<8;++i){unsigned shift=(unsigned)(28-i*4);out[i]=hexDigit((v>>shift)&15U);}out[8]=0; }
static void hex16(char* out,unsigned long long v){ for(int i=0;i<16;++i){unsigned shift=(unsigned)(60-i*4);out[i]=hexDigit((unsigned)((v>>shift)&15ULL));}out[16]=0; }
static void setAddressDetail(const char* prefix,unsigned long address){
    cpy(g_detail,sizeof(g_detail),prefix); unsigned long n=slen(g_detail); if(n+8>=sizeof(g_detail))return; hex8(g_detail+n,address);
}

static bool readableProtection(DWORD protection){
    const DWORD base=protection&0xFFUL;
    return base==PAGE_READONLY||base==PAGE_READWRITE||base==PAGE_WRITECOPY
        ||base==PAGE_EXECUTE_READ||base==PAGE_EXECUTE_READWRITE||base==PAGE_EXECUTE_WRITECOPY;
}
static bool executableProtection(DWORD protection){
    const DWORD base=protection&0xFFUL;
    return base==PAGE_EXECUTE||base==PAGE_EXECUTE_READ
        ||base==PAGE_EXECUTE_READWRITE||base==PAGE_EXECUTE_WRITECOPY;
}
static bool regionAllows(unsigned long address,unsigned long bytes,bool exec){
    MEMORY_BASIC_INFORMATION info={};
    if(VirtualQuery((const void*)address,&info,sizeof(info))==0)return false;
    if(info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))!=0)return false;
    unsigned long begin=(unsigned long)info.BaseAddress;
    unsigned long end=begin+(unsigned long)info.RegionSize;
    if(address<begin||address>end||bytes>end-address)return false;
    return exec?executableProtection(info.Protect):readableProtection(info.Protect);
}

static unsigned long long fnv1a(const unsigned char* data,unsigned long size,unsigned long long hash){
    const unsigned long long prime=1099511628211ULL;
    for(unsigned long i=0;i<size;++i){hash^=(unsigned long long)data[i];hash*=prime;}
    return hash;
}
static bool validateCodeAddress(unsigned long address,unsigned long long* hash){
    const unsigned long sampleBytes=12;
    if(!hash||!regionAllows(address,sampleBytes,true))return false;
    const unsigned char* bytes=(const unsigned char*)address;
    if(bytes[0]==0x00U||bytes[0]==0xCCU||bytes[0]==0xC3U)return false;
    *hash=fnv1a(bytes,sampleBytes,*hash);return true;
}

static bool validateClientImage(){
    HMODULE module=GetModuleHandleA(0);
    if(!module){cpy(g_detail,sizeof(g_detail),"MAIN_MODULE_UNAVAILABLE");return false;}
    const unsigned long base=(unsigned long)module;
    if(base!=EXPECTED_IMAGE_BASE){cpy(g_detail,sizeof(g_detail),"IMAGE_BASE_MISMATCH");return false;}
    // Use the same PE32 fields as the original guard, parsed explicitly so the
    // project can retain its minimal no-CRT/no-full-WinSDK build environment.
    if(!regionAllows(base,0x40,false)){cpy(g_detail,sizeof(g_detail),"DOS_HEADER_UNREADABLE");return false;}
    const unsigned char* image=(const unsigned char*)base;
    if(*(const unsigned short*)image!=0x5A4D){cpy(g_detail,sizeof(g_detail),"DOS_HEADER_INVALID");return false;}
    const long peOffset=*(const long*)(image+0x3C);
    if(peOffset<=0){cpy(g_detail,sizeof(g_detail),"DOS_HEADER_INVALID");return false;}
    const unsigned long ntAddress=base+(unsigned long)peOffset;
    if(!regionAllows(ntAddress,248,false)){cpy(g_detail,sizeof(g_detail),"NT_HEADER_UNREADABLE");return false;}
    const unsigned char* nt=(const unsigned char*)ntAddress;
    const unsigned long signature=*(const unsigned long*)(nt+0);
    const unsigned short machine=*(const unsigned short*)(nt+4);
    const unsigned long timestamp=*(const unsigned long*)(nt+8);
    const unsigned short magic=*(const unsigned short*)(nt+24);
    const unsigned long imageBase=*(const unsigned long*)(nt+52);
    const unsigned long imageSize=*(const unsigned long*)(nt+80);
    if(signature!=0x00004550UL||machine!=0x014CUL||magic!=0x010BUL
        ||imageBase!=EXPECTED_IMAGE_BASE||timestamp!=EXPECTED_TIMESTAMP){
        cpy(g_detail,sizeof(g_detail),"WOW_1_12_1_5875_SIGNATURE_MISMATCH");return false;
    }
    const unsigned long imageEnd=base+imageSize;
    for(unsigned i=0;i<sizeof(CODE_ADDRESSES)/sizeof(CODE_ADDRESSES[0]);++i){
        unsigned long a=CODE_ADDRESSES[i]; if(a<base||a>=imageEnd){cpy(g_detail,sizeof(g_detail),"CODE_ADDRESS_OUTSIDE_IMAGE");return false;}
    }
    for(unsigned i=0;i<sizeof(DATA_ADDRESSES)/sizeof(DATA_ADDRESSES[0]);++i){
        unsigned long a=DATA_ADDRESSES[i]; if(a<base||a>=imageEnd){cpy(g_detail,sizeof(g_detail),"DATA_ADDRESS_OUTSIDE_IMAGE");return false;}
    }
    return true;
}

static bool validateMoonMarkerAbi(){
    unsigned long long hash=1469598103934665603ULL;
    for(unsigned i=0;i<sizeof(CODE_ADDRESSES)/sizeof(CODE_ADDRESSES[0]);++i){
        unsigned long a=CODE_ADDRESSES[i];
        if(!validateCodeAddress(a,&hash)){setAddressDetail("CODE_ADDRESS_INVALID_",a);return false;}
    }
    for(unsigned i=0;i<sizeof(ADDITIONAL_CODE_ADDRESSES)/sizeof(ADDITIONAL_CODE_ADDRESSES[0]);++i){
        if(!validateCodeAddress(ADDITIONAL_CODE_ADDRESSES[i],&hash)){cpy(g_detail,sizeof(g_detail),"SET_COLOR_ADDRESS_INVALID");return false;}
    }
    for(unsigned i=0;i<sizeof(SEQUENCE_CODE_ADDRESSES)/sizeof(SEQUENCE_CODE_ADDRESSES[0]);++i){
        if(!validateCodeAddress(SEQUENCE_CODE_ADDRESSES[i],&hash)){cpy(g_detail,sizeof(g_detail),"SET_SEQUENCE_ADDRESS_INVALID");return false;}
    }
    for(unsigned i=0;i<sizeof(DATA_ADDRESSES)/sizeof(DATA_ADDRESSES[0]);++i){
        unsigned long a=DATA_ADDRESSES[i];
        if(!regionAllows(a,sizeof(unsigned long),false)){setAddressDetail("DATA_ADDRESS_INVALID_",a);return false;}
    }
    hex16(g_fingerprint,hash);return true;
}

} // namespace

bool initialize(){
    if(g_state!=STATE_UNKNOWN)return g_state==STATE_READY;
#if defined(MOONMARKER_FORCE_UNSUPPORTED)
    g_state=STATE_UNSUPPORTED_CLIENT;cpy(g_detail,sizeof(g_detail),"FORCED_UNSUPPORTED_TEST_MODE");return false;
#endif
    if(!validateClientImage()){g_state=STATE_UNSUPPORTED_CLIENT;return false;}
    if(!validateMoonMarkerAbi()){g_state=STATE_ADDRESS_UNREADABLE;return false;}
    g_state=STATE_READY;cpy(g_detail,sizeof(g_detail),"SUPPORTED_WOW_1_12_1_5875");return true;
}

bool enabled(){return g_state==STATE_READY;}

void markHookInstallFailed(const char* detailText){
    g_state=STATE_HOOK_INSTALL_FAILED;
    cpy(g_detail,sizeof(g_detail),detailText?detailText:"MOONMARKER_HOOK_INSTALL_FAILED");
}

const char* statusCode(){
    switch(g_state){
    case STATE_READY:return "READY";
    case STATE_UNSUPPORTED_CLIENT:return "UNSUPPORTED_CLIENT";
    case STATE_ADDRESS_UNREADABLE:return "ADDRESS_VALIDATION_FAILED";
    case STATE_HOOK_INSTALL_FAILED:return "HOOK_INSTALL_FAILED";
    default:return "NOT_CHECKED";
    }
}

const char* userMessage(){return enabled()?"MoonMarker 已启用":"当前 WoW 客户端版本不受支持";}
const char* detail(){return g_detail;}
const char* fingerprint(){return g_fingerprint;}

} // namespace TysMoonRuntimeGuard
