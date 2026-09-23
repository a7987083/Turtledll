#pragma once
namespace TysUnitStateLifecycle {
inline unsigned long nextNonZeroGeneration(unsigned long g){++g;if(!g)g=1u;return g;}
enum ChangeCategoryMask:unsigned long{CHANGE_HEALTH=0x01u,CHANGE_POWER=0x02u,CHANGE_COMBAT=0x04u};
enum class SnapshotOutcome:unsigned long{HELPER_FAILURE=0,OBJECT_UNAVAILABLE=1,SNAPSHOT_OK=2};
} // namespace TysUnitStateLifecycle
