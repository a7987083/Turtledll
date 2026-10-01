#pragma once
namespace WoW112 {
constexpr unsigned long IMAGE_BASE = 0x00400000UL;
constexpr unsigned long PE_TIMESTAMP = 0x4510B6DBUL;
constexpr unsigned short MACHINE_I386 = 0x014c;
constexpr unsigned short PE32_MAGIC = 0x010b;

constexpr unsigned long PLAYER_LOAD_SCRIPT_FUNCTIONS = 0x00490250UL;
constexpr unsigned long GLUE_LOAD_SCRIPT_FUNCTIONS   = 0x0046ABB0UL;
constexpr unsigned long FRAME_SCRIPT_REGISTER_FUNCTION = 0x00704120UL;

// DrinkWalk native actuator. Restored from the V0.4.x validated baseline.
// Explicit-call only: PackBagSlot -> GetItemBySlot -> CGItem::UseItem.
constexpr unsigned long PACK_BAG_SLOT = 0x004F9820UL;
constexpr unsigned long ITEMMGR_GET_ITEM_BY_SLOT = 0x006228A0UL;
constexpr unsigned long ITEM_USE_NATIVE = 0x005D8D00UL;

// AURA6-D4-R1 shared NativeBus funnels. Feature modules subscribe here instead
// of detouring per-opcode spell handlers.
constexpr unsigned long NET_SEND = 0x005379A0UL;
constexpr unsigned long NET_MESSAGE_DISPATCH = 0x00537AA0UL;
constexpr unsigned long WORLD_TICK = 0x0066FD50UL;
constexpr unsigned long CMSG_CAST_SPELL_OPCODE = 0x012EUL;
constexpr unsigned long SMSG_CAST_RESULT_OPCODE = 0x0130UL;
constexpr unsigned long SMSG_SPELL_START_OPCODE = 0x0131UL;
constexpr unsigned long SMSG_SPELL_GO_OPCODE = 0x0132UL;
constexpr unsigned long SMSG_SPELL_FAILURE_OPCODE = 0x0133UL;
constexpr unsigned long SMSG_SPELL_COOLDOWN_OPCODE = 0x0134UL;
constexpr unsigned long SMSG_COOLDOWN_EVENT_OPCODE = 0x0135UL;
constexpr unsigned long MSG_CHANNEL_START_OPCODE = 0x0139UL;
constexpr unsigned long MSG_CHANNEL_UPDATE_OPCODE = 0x013AUL;
constexpr unsigned long SMSG_UPDATE_OBJECT_OPCODE = 0x00A9UL;
constexpr unsigned long SMSG_CLEAR_COOLDOWN_OPCODE = 0x01DEUL;
constexpr unsigned long SMSG_COOLDOWN_CHEAT_OPCODE = 0x01E1UL;
constexpr unsigned long SMSG_COMPRESSED_UPDATE_OBJECT_OPCODE = 0x01F6UL;
constexpr unsigned long SMSG_SPELL_DELAYED_OPCODE = 0x01E2UL;
constexpr unsigned long SMSG_SPELL_FAILED_OTHER_OPCODE = 0x02A6UL;
// Turtle/SuperWoW cast-stop choke point. SuperWoW calls this engine method
// directly for remote interrupts; observing it avoids relying on failure
// packets Turtle does not consistently broadcast.
constexpr unsigned long UNIT_CLEAR_CASTING_SPELL = 0x0060D040UL;
constexpr unsigned long OFF_UNIT_CAST_SPELL = 0x0C8CUL;

// Aura native custom FrameScript event extension.
constexpr unsigned long FRAMESCRIPT_CREATE_EVENTS = 0x00703D90UL;
constexpr unsigned long FRAMESCRIPT_SET_EVENT_COUNT = 0x007053B0UL;
constexpr unsigned long FRAMESCRIPT_EVENT_OBJECT_DATA = 0x00CEEF68UL;
constexpr unsigned long SIGNAL_EVENT_PARAM = 0x00703F50UL;
constexpr unsigned long SSTR_DUP_A = 0x0064A620UL;
constexpr unsigned long CGUNIT_ON_AURA_REMOVED = 0x00612320UL;
constexpr unsigned long CGUNIT_ON_AURA_ADDED = 0x006123F0UL;
constexpr unsigned long CGUNIT_ON_AURA_STACKS_CHANGED = 0x00612450UL;
constexpr unsigned long GET_ACTIVE_PLAYER_GUID = 0x00468550UL;
constexpr unsigned long SPELL_DB = 0x00C0D780UL;
constexpr unsigned long RESOLVE_UNIT_TOKEN = 0x00515940UL;
constexpr unsigned long RESOLVE_UNIT_GUID = 0x00515970UL;
constexpr unsigned long FAST_GUID_LOOKUP = 0x00464870UL;
constexpr unsigned long COOLDOWN_QUERY_HELPER = 0x006E2EA0UL;

// AURA6-D4-R4 target-duration/lifecycle prediction. Values are read from the stock
// 1.12 client at cast time; no per-frame scanning is introduced.
constexpr unsigned long GET_SPELL_DURATION = 0x006EA000UL;
constexpr unsigned long SPELL_DURATION_RECORDS = 0x00C0D828UL;
constexpr unsigned long SPELL_DURATION_COUNT = 0x00C0D82CUL;
constexpr unsigned long OFF_CGPLAYER_INFO = 0xE68UL;
constexpr unsigned long OFF_CGPLAYER_COMBO_POINTS = 0x1029UL;
constexpr unsigned long OFF_CGPLAYER_COMBO_TARGET = 0x838UL;
constexpr unsigned long OFF_CGUNIT_OBJECT_FIELDS = 0x110UL;
constexpr unsigned long OFF_UNIT_DESCRIPTOR_CLASS_BYTE = 0x79UL;
constexpr unsigned long CHRCLASSES_RECORDS = 0x00C0DEF4UL;
constexpr unsigned long CHRCLASSES_COUNT = 0x00C0DEF8UL;
constexpr unsigned long OFF_CHRCLASSES_SPELL_FAMILY = 0x3CUL;
constexpr unsigned long OFF_SPELL_RECORD_DURATION_INDEX = 0x78UL;
constexpr unsigned long OFF_SPELL_RECORD_ATTRIBUTES_EX = 0x1CUL;
constexpr unsigned long OFF_SPELL_RECORD_ATTRIBUTES_EX3 = 0x24UL;
constexpr unsigned long OFF_SPELL_RECORD_FAMILY_NAME = 0x280UL;
constexpr unsigned long OFF_SPELL_RECORD_FAMILY_FLAGS = 0x284UL;
constexpr unsigned long OFF_SPELL_NAMES = 0x1E0UL;
constexpr unsigned long LOCALE_INDEX = 0x00C0E080UL;
constexpr unsigned long SPELL_ATTR_EX3_IGNORE_CASTER_MODIFIERS = 0x20000000UL;
constexpr unsigned long SPELLMOD_FLAT_TABLE = 0x00CEAD60UL;
constexpr unsigned long SPELLMOD_PCT_TABLE = 0x00CECB30UL;
constexpr unsigned long SPELLMOD_SLOT_STRIDE = 0x74UL;
constexpr unsigned long SPELLMOD_SLOT_COUNT = 64UL;
constexpr unsigned long SPELLMOD_OP_DURATION = 1UL;

// AURA4-D2 self exact duration: D1 read-only discovery + event-driven CGBuffBar duration hook.
constexpr unsigned long OS_GET_ASYNC_TIME_MS = 0x0042B790UL;
constexpr unsigned long CGBUFFBAR_UPDATE_DURATION = 0x004E4390UL;
constexpr unsigned long BUFFBAR_EXPIRATION_ARRAY = 0x00BC5F68UL;
constexpr unsigned long SMSG_UPDATE_AURA_DURATION_HANDLER = 0x005E38C0UL;
constexpr unsigned long SMSG_UPDATE_AURA_DURATION_OPCODE = 0x0137UL;
constexpr unsigned long MOVEMENT_GET_EFFECTIVE_SPEED = 0x007C4C90UL;
constexpr unsigned long UNIT_REACTION = 0x006061E0UL;
constexpr unsigned long CAN_ATTACK = 0x00606980UL;
constexpr unsigned long UNIT_GET_CREATURE_TYPE = 0x00605570UL;
constexpr unsigned long OBJECT_MANAGER_PTR = 0x00B41414UL;

constexpr unsigned long LUA_STATE_PTR = 0x007040D0UL;
constexpr unsigned long LUA_GETTOP = 0x006F3070UL;
constexpr unsigned long LUA_SETTOP = 0x006F3080UL;
constexpr unsigned long LUA_TYPE = 0x006F3400UL;
constexpr unsigned long LUA_ISNUMBER = 0x006F34D0UL;
constexpr unsigned long LUA_ISSTRING = 0x006F3510UL;
constexpr unsigned long LUA_TONUMBER = 0x006F3620UL;
constexpr unsigned long LUA_TOBOOLEAN = 0x006F3660UL;
constexpr unsigned long LUA_TOSTRING = 0x006F3690UL;
constexpr unsigned long LUA_PUSHNIL = 0x006F37F0UL;
constexpr unsigned long LUA_PUSHNUMBER = 0x006F3810UL;
constexpr unsigned long LUA_PUSHSTRING = 0x006F3890UL;
constexpr unsigned long LUA_PUSHBOOLEAN = 0x006F39F0UL;
constexpr unsigned long LUA_GETTABLE = 0x006F3A40UL;
constexpr unsigned long LUA_NEWTABLE = 0x006F3C90UL;
constexpr unsigned long LUA_SETTABLE = 0x006F3E20UL;
constexpr unsigned long LUA_ISCFUNCTION = 0x006F34A0UL;
constexpr unsigned long LUA_TOCFUNCTION = 0x006F3720UL;
constexpr unsigned long LUA_PCALL = 0x006F41A0UL;
constexpr unsigned long LUA_PUSHVALUE = 0x006F3350UL;
constexpr unsigned long LUA_INSERT = 0x006F31A0UL;
constexpr unsigned long LUA_PUSHCLOSURE = 0x006F3920UL;
constexpr unsigned long LUA_CALL = 0x006F4180UL;
constexpr unsigned long LUA_SEThOOK = 0x006FBA40UL;
constexpr unsigned long LUA_GETINFO = 0x006FBAA0UL;
constexpr unsigned long LUA_GETSTACK = 0x006FBB20UL;

// Lua 5.0 lua_State hook fields used by the original B1R5R4 Targeted Deep implementation.
constexpr unsigned long OFF_LUA_HOOK_MASK = 0x30UL;
constexpr unsigned long OFF_LUA_HOOK_PTR = 0x3CUL;

constexpr int LUA_GLOBALSINDEX = -10001;
constexpr int LUA_UPVALUE1 = -10002;
constexpr int LUA_UPVALUE2 = -10003;
constexpr int LUA_TFUNCTION = 6;

constexpr unsigned long OFF_UNIT_MOVEMENT_INFO_PTR = 0x118UL;
constexpr unsigned long OFF_MOVEMENT_FACING = 0x1CUL;
constexpr unsigned long OFF_MOVEMENT_RUN_SPEED = 0x8CUL;
constexpr unsigned long OFF_MOVEMENT_SWIM_SPEED = 0x94UL;
constexpr unsigned long OFF_MOVEMENT_COLLISION_HEIGHT = 0xB4UL;

// ARX1/API33: explicit-call-only LOS query. This is the stock 1.12.1 build 5875
// CWorld_Intersect entry used by UnitXP_SP3. No hook is installed at this address.
constexpr unsigned long CWORLD_INTERSECT = 0x00672170UL;
constexpr unsigned long CWORLD_INTERSECT_LOS_FLAGS = 0x00100111UL;

constexpr unsigned long OFF_CGOBJECT_DESCRIPTOR = 0x08UL;
constexpr unsigned long OFF_CGOBJECT_TYPE = 0x14UL;
constexpr unsigned long OFF_CGOBJECT_GUID = 0x30UL;
constexpr unsigned long OFF_CGOBJECT_VTBL_GET_POSITION = 0x14UL; // vtable slot 5
constexpr unsigned long OFF_OBJECT_FIELD_ENTRY = 0x0CUL;

// WoW 1.12.1 build 5875 global UpdateFields indices (vmangos cross-check).
constexpr unsigned long UNIT_FIELD_HEALTH_INDEX = 0x16UL;
constexpr unsigned long UNIT_FIELD_MAXHEALTH_INDEX = 0x1CUL;
constexpr unsigned long UNIT_DYNAMIC_FLAGS_INDEX = 0x8FUL;
constexpr unsigned long UNIT_DYNFLAG_DEAD = 0x20UL;

constexpr unsigned long OFF_OBJECT_MANAGER_NEXT_BASE = 0xA4UL;
constexpr unsigned long OFF_OBJECT_MANAGER_HEAD = 0xACUL;

// DynamicObject UpdateFields relative to the generic descriptor pointer.
constexpr unsigned long OFF_DYNAMIC_CASTER_GUID = 0x18UL;
constexpr unsigned long OFF_DYNAMIC_SPELL_ID = 0x24UL;
constexpr unsigned long OFF_DYNAMIC_RADIUS = 0x28UL;
constexpr unsigned long OFF_DYNAMIC_X = 0x2CUL;
constexpr unsigned long OFF_DYNAMIC_Y = 0x30UL;
constexpr unsigned long OFF_DYNAMIC_Z = 0x34UL;

constexpr unsigned long TYPE_UNIT = 3UL;
constexpr unsigned long TYPE_PLAYER = 4UL;
constexpr unsigned long TYPE_GAMEOBJECT = 5UL;
constexpr unsigned long TYPE_DYNAMICOBJECT = 6UL;
}
