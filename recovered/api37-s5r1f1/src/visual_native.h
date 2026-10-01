#pragma once

namespace TysVisual {

constexpr unsigned long WORLD_M2_CONTEXT_PTR = 0x00C7B298UL;
constexpr unsigned long CREATE_MODEL = 0x00707350UL;
constexpr unsigned long RELEASE_MODEL = 0x007103A0UL;
constexpr unsigned long ENSURE_RENDER_READY = 0x00710450UL;
constexpr unsigned long SET_WORLD_MATRIX = 0x00710620UL;
constexpr unsigned long ATTACH_RENDER_LIST = 0x00710B90UL;
constexpr unsigned long SET_ACTIVE_TIMESTAMP = 0x00710C50UL;
constexpr unsigned long SET_ALPHA = 0x00710CB0UL;
constexpr unsigned long SET_COLOR = 0x00710CF0UL;
constexpr unsigned long SET_SEQUENCE = 0x007121A0UL;
constexpr unsigned long SCENE_END = 0x005A17A0UL;

// Shared runtime layout. AutoRange and MoonMarker never allocate from each
// other's pool, but both pools use the same native model/lifetime engine.
constexpr unsigned long SLOT_COUNT = 16UL;             // AutoRange public pool
constexpr unsigned long MOON_SLOT_COUNT = 64UL;        // 8 colors x 8 markers
constexpr unsigned long TOTAL_SLOT_COUNT = SLOT_COUNT + MOON_SLOT_COUNT + 2UL;
constexpr unsigned long KEY_CAP = 160UL;
constexpr unsigned long PATH_CAP = 241UL;
constexpr float MIN_SCALE = 0.05f;
constexpr float MAX_SCALE = 20.0f;

void resetAllStateNoRelease();

// AutoRange namespace ---------------------------------------------------------
void clearAll();
void clear(const char* key);
bool set(const char* key,const char* modelPath,float x,float y,float z,float scale,float yawDegrees,char* normalized,unsigned long normalizedCap);
bool move(const char* key,float x,float y,float z,float scale,float yawDegrees);
bool hide(const char* key);
bool show(const char* key);
bool restart(const char* key);

// MoonMarker namespace --------------------------------------------------------
// Ground coordinates are accepted. The working MoonMarker renderer offsets the
// beam body by +0.05 Z internally, exactly as the old nativeM2Test path did.
bool moonSet(const char* color,const char* icon,float x,float y,float z,char* normalizedColor,unsigned long colorCap,char* normalizedIcon,unsigned long iconCap);
void moonClearAll();
unsigned long moonActiveCount();

// Old MoonMarker Advanced/Main custom model path. This replaces the M2 body in
// the same color x icon slot used by normal MoonMarker, so the projected raid
// icon remains owned by the same 8x8 namespace.
bool moonSetCustom(const char* color,const char* icon,float x,float y,float z,
                   const char* modelPath,float scale,float yawDegrees,
                   char* normalizedPath,unsigned long pathCap,
                   char* normalizedColor,unsigned long colorCap,
                   char* normalizedIcon,unsigned long iconCap);

// Dedicated Advanced model-library preview. Independent from the green ground
// targeting preview and from the 64 live MoonMarker slots.
bool moonAdvancedPreviewSet(const char* modelPath,float x,float y,float z,float scale,float yawDegrees,char* normalizedPath,unsigned long pathCap);
bool moonAdvancedPreviewTransform(float scale,float yawDegrees);
void moonAdvancedPreviewClear();
bool moonAdvancedPreviewStatus(bool* active,bool* renderReady,char* path,unsigned long pathCap,float* scale,float* yawDegrees);
unsigned long worldContextToken();

// Dedicated green ground-targeting preview used by the old MoonMarker UI.
bool moonPreviewSet(float x,float y,float z);
bool moonPreviewMove(float x,float y,float z);
void moonPreviewClear();

// Native frame maintenance for both namespaces. gxDevice is the WoW CGxDevice
// argument received by sceneEnd; it is used only for MoonMarker projected icon
// support. No Lua timer / OnUpdate / VisualTick is involved.
void updateNativeFrame(unsigned long gxDevice);

// Original MoonMarker world-lifecycle bridge. PLAYER_LEAVING_WORLD clears
// live models while the old world is still valid; if teardown has already
// started the implementation drops pointers without calling client release.
void onWorldLeaving();
void onWorldEntering();
void shutdownMoonPresentation();

unsigned long activeCount();       // AutoRange only
unsigned long hiddenCount();       // AutoRange only
unsigned long modelCount();        // all shared M2 models
const char* lastError();
void stats(char* out,unsigned long cap);

} // namespace TysVisual
