#pragma once
#include <windows.h>
#define TH32CS_SNAPTHREAD 0x00000004

typedef struct tagTHREADENTRY32 {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ThreadID;
    DWORD th32OwnerProcessID;
    LONG tpBasePri;
    LONG tpDeltaPri;
    DWORD dwFlags;
} THREADENTRY32;

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllimport) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD,DWORD);
__declspec(dllimport) BOOL WINAPI Thread32First(HANDLE,THREADENTRY32*);
__declspec(dllimport) BOOL WINAPI Thread32Next(HANDLE,THREADENTRY32*);
#ifdef __cplusplus
}
#endif
