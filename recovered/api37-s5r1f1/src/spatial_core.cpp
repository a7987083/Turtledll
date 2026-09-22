#include <windows.h>
#include "spatial_core.h"
#include "wow112_offsets.h"

namespace TysSpatialCore {
namespace {
static volatile LONG g_queryCount=0;
static volatile LONG g_distanceCount=0;
static volatile LONG g_behindCount=0;
static volatile LONG g_unavailableCount=0;
static void setStr(Lua50::State L,const char*k,const char*v){Lua50::PushString(L,k);Lua50::PushString(L,v);Lua50::SetTable(L,-3);}
static void setNum(Lua50::State L,const char*k,double v){Lua50::PushString(L,k);Lua50::PushNumber(L,v);Lua50::SetTable(L,-3);}
static void setBool(Lua50::State L,const char*k,bool v){Lua50::PushString(L,k);Lua50::PushBool(L,v);Lua50::SetTable(L,-3);}
static int unavailable(Lua50::State L){++g_unavailableCount;Lua50::PushNil(L);Lua50::PushString(L,"SPATIAL_DATA_UNAVAILABLE");return 2;}
}

int dispatchStatus(Lua50::State L){Lua50::NewTable(L);setStr(L,"stage","S5-R1F1");setStr(L,"status","READY_EXPLICIT_QUERY_NO_BACKGROUND_WORK");setStr(L,"rangePolicy","CLIENT_GEOMETRY");setStr(L,"losPolicy","REUSE_UNIT_INSIGHT_LOS1_EXPLICIT");setStr(L,"behindPolicy","CLIENT_GEOMETRY_REAR_AXIS_CALIBRATED_PI");setStr(L,"behindDotSemantics","POSITIVE_REAR_NEGATIVE_FRONT");setStr(L,"behindSemantics","RAW_MOVEMENT_AXIS_TREATED_AS_REAR_FROM_S5R2_LIVE_SAMPLES");setStr(L,"serverBackstabPolicy","S5R2_OBSERVER_CALIBRATES_CAST_RESULTS");setNum(L,"queryCount",g_queryCount);setNum(L,"distanceCount",g_distanceCount);setNum(L,"behindCount",g_behindCount);setNum(L,"unavailableCount",g_unavailableCount);setBool(L,"backgroundThread",false);setBool(L,"objectManagerPolling",false);setBool(L,"directHook",false);return 1;}

// Command surface and calibration semantics are binary-confirmed. Exact object
// resolution/geometry return layout is still under disassembly recovery; fail
// closed rather than fabricating values in this compile-stage scaffold.
int dispatchGet(Lua50::State L){++g_queryCount;return unavailable(L);}
int dispatchDistance(Lua50::State L){++g_distanceCount;return unavailable(L);}
int dispatchBehind(Lua50::State L){++g_behindCount;return unavailable(L);}
}
