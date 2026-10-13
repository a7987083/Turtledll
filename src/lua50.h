#pragma once
#include "wow112_offsets.h"
namespace Lua50 {
using State = void*;
using CFunction = int (__fastcall *)(State);
using GetStateFn = State (__fastcall *)();
using RegisterFn = void (__fastcall *)(const char*, CFunction);
using GetTopFn = int (__fastcall *)(State);
using SetTopFn = void (__fastcall *)(State,int);
using TypeFn = int (__fastcall *)(State,int);
using IsNumberFn = int (__fastcall *)(State,int);
using IsStringFn = int (__fastcall *)(State,int);
using ToNumberFn = double (__fastcall *)(State,int);
using ToBooleanFn = int (__fastcall *)(State,int);
using ToStringFn = const char* (__fastcall *)(State,int);
using PushNilFn = void (__fastcall *)(State);
using PushNumberFn = void (__fastcall *)(State,double);
using PushStringFn = void (__fastcall *)(State,const char*);
using PushBooleanFn = void (__fastcall *)(State,int);
using GetTableFn = void (__fastcall *)(State,int);
using NewTableFn = void (__fastcall *)(State);
using SetTableFn = void (__fastcall *)(State,int);
using IsCFunctionFn = int (__fastcall *)(State,int);
using ToCFunctionFn = CFunction (__fastcall *)(State,int);
using PCallFn = int (__fastcall *)(State,int,int,int);
using PushValueFn = void (__fastcall *)(State,int);
using InsertFn = void (__fastcall *)(State,int);
using PushCClosureFn = void (__fastcall *)(State,CFunction,int);
using CallFn = void (__fastcall *)(State,int,int);

inline State GetState(){ return ((GetStateFn)WoW112::LUA_STATE_PTR)(); }
inline void Register(const char* n,CFunction f){ ((RegisterFn)WoW112::FRAME_SCRIPT_REGISTER_FUNCTION)(n,f); }
inline int GetTop(State L){ return ((GetTopFn)WoW112::LUA_GETTOP)(L); }
inline void SetTop(State L,int n){ ((SetTopFn)WoW112::LUA_SETTOP)(L,n); }
inline int Type(State L,int i){ return ((TypeFn)WoW112::LUA_TYPE)(L,i); }
inline bool IsNumber(State L,int i){ return ((IsNumberFn)WoW112::LUA_ISNUMBER)(L,i)!=0; }
inline bool IsString(State L,int i){ return ((IsStringFn)WoW112::LUA_ISSTRING)(L,i)!=0; }
inline double ToNumber(State L,int i){ return ((ToNumberFn)WoW112::LUA_TONUMBER)(L,i); }
inline bool ToBoolean(State L,int i){ return ((ToBooleanFn)WoW112::LUA_TOBOOLEAN)(L,i)!=0; }
inline const char* ToString(State L,int i){ return ((ToStringFn)WoW112::LUA_TOSTRING)(L,i); }
inline void PushNil(State L){ ((PushNilFn)WoW112::LUA_PUSHNIL)(L); }
inline void PushNumber(State L,double n){ ((PushNumberFn)WoW112::LUA_PUSHNUMBER)(L,n); }
inline void PushString(State L,const char* s){ ((PushStringFn)WoW112::LUA_PUSHSTRING)(L,s?s:""); }
inline void PushBool(State L,bool b){ ((PushBooleanFn)WoW112::LUA_PUSHBOOLEAN)(L,b?1:0); }
inline void GetTable(State L,int i){ ((GetTableFn)WoW112::LUA_GETTABLE)(L,i); }
inline void NewTable(State L){ ((NewTableFn)WoW112::LUA_NEWTABLE)(L); }
inline void SetTable(State L,int i){ ((SetTableFn)WoW112::LUA_SETTABLE)(L,i); }
inline bool IsCFunction(State L,int i){ return ((IsCFunctionFn)WoW112::LUA_ISCFUNCTION)(L,i)!=0; }
inline CFunction ToCFunction(State L,int i){ return ((ToCFunctionFn)WoW112::LUA_TOCFUNCTION)(L,i); }
inline int PCall(State L,int nargs,int nresults,int err){ return ((PCallFn)WoW112::LUA_PCALL)(L,nargs,nresults,err); }
inline void PushValue(State L,int i){ ((PushValueFn)WoW112::LUA_PUSHVALUE)(L,i); }
inline void Insert(State L,int i){ ((InsertFn)WoW112::LUA_INSERT)(L,i); }
inline void PushCClosure(State L,CFunction f,int n){ ((PushCClosureFn)WoW112::LUA_PUSHCLOSURE)(L,f,n); }
inline void Call(State L,int nargs,int nresults){ ((CallFn)WoW112::LUA_CALL)(L,nargs,nresults); }
inline void GetGlobal(State L,const char* name){ PushString(L,name); GetTable(L,WoW112::LUA_GLOBALSINDEX); }
}
