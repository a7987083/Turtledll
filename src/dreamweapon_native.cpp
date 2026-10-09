#include "dreamweapon_native.h"
#include "dreamavatar_native.h"

namespace TysDreamWeapon {
namespace {
static bool startsI(const char*s,const char*p){if(!s||!p)return false;while(*p){if(!*s)return false;char a=*s++,b=*p++;if(a>='A'&&a<='Z')a+=32;if(b>='A'&&b<='Z')b+=32;if(a!=b)return false;}return true;}
static bool eqI(const char*a,const char*b){if(!a||!b)return false;while(*a&&*b){char x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return false;}return *a==*b;}
static const char* nativeSuffix(const char*cmd){if(startsI(cmd,"MoonMarker.DreamWeapon."))return cmd+23;if(startsI(cmd,"DreamWeapon."))return cmd+12;return 0;}
static const char* legacySuffix(const char*cmd){if(startsI(cmd,"MoonMarker.DreamAvatar."))return cmd+23;if(startsI(cmd,"DreamAvatar."))return cmd+12;return 0;}
static bool allowed0348Suffix(const char*s){
    if(!s)return false;
    return eqI(s,"WeaponStatus")||eqI(s,"GetTargetWeapons")||eqI(s,"GetTargetNpcWeapons")
        ||eqI(s,"ApplyWeapon")||eqI(s,"RestoreWeapons")||eqI(s,"MaintainWeapons")
        ||eqI(s,"SetAutoMaintainWeapons")||eqI(s,"Sync.Build")||eqI(s,"Sync.BuildClear")
        ||eqI(s,"Sync.Receive")||eqI(s,"Sync.Reapply")||eqI(s,"Sync.RestoreSender")
        ||eqI(s,"Sync.RestoreAll");
}
static bool weaponSuffix(const char*s){
    if(!s)return false;
    return eqI(s,"WeaponStatus")||eqI(s,"GetTargetWeapons")||eqI(s,"GetTargetNpcWeapons")
        ||eqI(s,"ApplyWeapon")||eqI(s,"RestoreWeapons")||eqI(s,"MaintainWeapons")
        ||eqI(s,"SetAutoMaintainWeapons");
}
static bool syncSuffix(const char*s){return s&&startsI(s,"Sync.")&&allowed0348Suffix(s);}
}

bool isCommand(const char*cmd){return allowed0348Suffix(nativeSuffix(cmd));}

int dispatchAnyPublic(Lua50::State L,const char*cmd){
    const char*s=nativeSuffix(cmd);
    if(allowed0348Suffix(s))return TysDreamAvatar::dispatchDreamWeaponShared(L,s);
    s=legacySuffix(cmd);
    if(allowed0348Suffix(s))return TysDreamAvatar::dispatchDreamWeaponShared(L,s);
    return -1;
}

bool isLegacy0348Command(const char*cmd){return allowed0348Suffix(legacySuffix(cmd));}
bool isLegacyWeaponCommand(const char*cmd){return weaponSuffix(legacySuffix(cmd));}
bool isLegacySyncCommand(const char*cmd){return syncSuffix(legacySuffix(cmd));}

int dispatchLegacy0348(Lua50::State L,const char*cmd){
    const char*s=legacySuffix(cmd);if(!allowed0348Suffix(s))return -1;
    return TysDreamAvatar::dispatchDreamWeaponShared(L,s);
}

void shutdown(Lua50::State L){TysDreamAvatar::shutdownDreamWeaponShared(L);}
const char* status(){return "READY_DW5_SHARED_DAS1";}
}
