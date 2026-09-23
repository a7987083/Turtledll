#pragma once
namespace TysUnitStateLayout {
constexpr unsigned long OBJECT_DESCRIPTOR_PTR=0x08u,OBJECT_TYPE=0x14u,OBJECT_GUID_LOW=0x30u,OBJECT_GUID_HIGH=0x34u,OBJECT_VALIDATE_BYTES=0x38u;
constexpr unsigned long DESC_HEALTH=0x58u,DESC_POWER1=0x5Cu,DESC_MAX_HEALTH=0x70u,DESC_MAX_POWER1=0x74u,DESC_POWER_TYPE_PACKED=0x90u,DESC_UNIT_FLAGS=0xB8u,DESC_DYNAMIC_FLAGS=0x23Cu;
constexpr unsigned long DESC_RANGE_START=0x58u,DESC_RANGE_BYTES=0x1E8u,POWER_LANES=5u,UNIT_FLAG_IN_COMBAT=0x00080000u,UNIT_DYNFLAG_DEAD=0x00000020u;
inline unsigned long powerOffset(unsigned long t){return DESC_POWER1+t*4u;}
inline unsigned long maxPowerOffset(unsigned long t){return DESC_MAX_POWER1+t*4u;}
inline unsigned long powerTypeFromPacked(unsigned long p){return p>>24;}
inline bool inCombat(unsigned long f){return (f&UNIT_FLAG_IN_COMBAT)!=0;}
inline bool dead(unsigned long h,unsigned long d){return h==0u||(d&UNIT_DYNFLAG_DEAD)!=0u;}
enum PowerChangeMask:unsigned long{POWER_TYPE_CHANGED=0x01u,POWER_VALUE_CHANGED=0x02u,POWER_MAX_CHANGED=0x04u};
} // namespace TysUnitStateLayout
