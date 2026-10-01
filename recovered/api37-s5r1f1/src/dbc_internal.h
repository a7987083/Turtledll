#pragma once
#include "lua50.h"

namespace TysDbc {

bool isCommand(const char* cmd);
bool isSearchCommand(const char* cmd);
int dispatch(Lua50::State L,const char* cmd,unsigned long id);
int dispatchSearch(Lua50::State L,const char* cmd,const char* query,unsigned long limit);
int dispatchAutoRangeStatus(Lua50::State L);

bool archiveScanAttempted();
unsigned long archiveCount();
const char* lastError();
void resetLastError();

} // namespace TysDbc
