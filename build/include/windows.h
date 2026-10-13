#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI __stdcall
#define APIENTRY WINAPI
#define CALLBACK __stdcall
#define TRUE 1
#define FALSE 0
#define NULL 0
#define MAX_PATH 260
#define ERROR_NO_MORE_FILES 18


typedef void VOID;
typedef unsigned char BYTE;
typedef unsigned char UINT8;
typedef signed char INT8;
typedef unsigned short WORD;
typedef unsigned short UINT16;
typedef signed short INT16;
typedef unsigned long DWORD;
typedef unsigned long UINT32;
typedef signed long INT32;
typedef unsigned __int64 DWORD64;
typedef unsigned __int64 UINT64;
typedef signed __int64 INT64;
typedef long LONG;
typedef unsigned int UINT;
typedef unsigned long ULONG_PTR;
typedef unsigned long DWORD_PTR;
typedef unsigned long SIZE_T;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef BYTE* LPBYTE;
typedef DWORD* LPDWORD;
typedef UINT32* PUINT32;
typedef const char* LPCSTR;
#ifndef __cplusplus
typedef unsigned short wchar_t;
#endif
typedef const wchar_t* LPCWSTR;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* FARPROC;
typedef int BOOL;

#define INVALID_HANDLE_VALUE ((HANDLE)(LONG)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010UL
typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    char cFileName[MAX_PATH];
    char cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef long LONG_PTR;
#define FIELD_OFFSET(type, field) ((LONG)(LONG_PTR)&(((type *)0)->field))

#define MEM_COMMIT      0x00001000
#define MEM_RESERVE     0x00002000
#define MEM_RELEASE     0x00008000
#define MEM_FREE        0x00010000
#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD             0x100

#define HEAP_NO_SERIALIZE 0x00000001

#define THREAD_SUSPEND_RESUME      0x0002
#define THREAD_GET_CONTEXT         0x0008
#define THREAD_SET_CONTEXT         0x0010
#define THREAD_QUERY_INFORMATION   0x0040

#define CONTEXT_i386    0x00010000
#define CONTEXT_CONTROL (CONTEXT_i386 | 0x00000001L)
#define MAXIMUM_SUPPORTED_EXTENSION 512

typedef struct _FLOATING_SAVE_AREA {
    DWORD ControlWord;
    DWORD StatusWord;
    DWORD TagWord;
    DWORD ErrorOffset;
    DWORD ErrorSelector;
    DWORD DataOffset;
    DWORD DataSelector;
    BYTE RegisterArea[80];
    DWORD Cr0NpxState;
} FLOATING_SAVE_AREA;

typedef struct _CONTEXT {
    DWORD ContextFlags;
    DWORD Dr0; DWORD Dr1; DWORD Dr2; DWORD Dr3; DWORD Dr6; DWORD Dr7;
    FLOATING_SAVE_AREA FloatSave;
    DWORD SegGs; DWORD SegFs; DWORD SegEs; DWORD SegDs;
    DWORD Edi; DWORD Esi; DWORD Ebx; DWORD Edx; DWORD Ecx; DWORD Eax;
    DWORD Ebp; DWORD Eip; DWORD SegCs; DWORD EFlags; DWORD Esp; DWORD SegSs;
    BYTE ExtendedRegisters[MAXIMUM_SUPPORTED_EXTENSION];
} CONTEXT;

typedef struct _MEMORY_BASIC_INFORMATION {
    LPVOID BaseAddress;
    LPVOID AllocationBase;
    DWORD AllocationProtect;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
} MEMORY_BASIC_INFORMATION;

typedef struct _SYSTEMTIME {
    WORD wYear; WORD wMonth; WORD wDayOfWeek; WORD wDay;
    WORD wHour; WORD wMinute; WORD wSecond; WORD wMilliseconds;
} SYSTEMTIME;

typedef struct _SYSTEM_INFO {
    union { DWORD dwOemId; struct { WORD wProcessorArchitecture; WORD wReserved; }; };
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} SYSTEM_INFO;

__declspec(dllimport) HANDLE WINAPI GetCurrentProcess(void);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) DWORD WINAPI GetCurrentThreadId(void);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE);
__declspec(dllimport) HANDLE WINAPI HeapCreate(DWORD,SIZE_T,SIZE_T);
__declspec(dllimport) BOOL WINAPI HeapDestroy(HANDLE);
__declspec(dllimport) LPVOID WINAPI HeapAlloc(HANDLE,DWORD,SIZE_T);
__declspec(dllimport) LPVOID WINAPI HeapReAlloc(HANDLE,DWORD,LPVOID,SIZE_T);
__declspec(dllimport) BOOL WINAPI HeapFree(HANDLE,DWORD,LPVOID);
__declspec(dllimport) LPVOID WINAPI VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);
__declspec(dllimport) BOOL WINAPI VirtualFree(LPVOID,SIZE_T,DWORD);
__declspec(dllimport) BOOL WINAPI VirtualProtect(LPVOID,SIZE_T,DWORD,LPDWORD);
__declspec(dllimport) SIZE_T WINAPI VirtualQuery(LPCVOID,MEMORY_BASIC_INFORMATION*,SIZE_T);
__declspec(dllimport) BOOL WINAPI FlushInstructionCache(HANDLE,LPCVOID,SIZE_T);
__declspec(dllimport) VOID WINAPI GetSystemInfo(SYSTEM_INFO*);
__declspec(dllimport) HANDLE WINAPI OpenThread(DWORD,BOOL,DWORD);
__declspec(dllimport) DWORD WINAPI SuspendThread(HANDLE);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE);
__declspec(dllimport) BOOL WINAPI GetThreadContext(HANDLE,CONTEXT*);
__declspec(dllimport) BOOL WINAPI SetThreadContext(HANDLE,const CONTEXT*);
__declspec(dllimport) LONG WINAPI InterlockedCompareExchange(volatile LONG*,LONG,LONG);
__declspec(dllimport) LONG WINAPI InterlockedExchange(volatile LONG*,LONG);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleW(LPCWSTR);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(LPCSTR);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(HMODULE,char*,DWORD);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR);
__declspec(dllimport) DWORD WINAPI GetLastError(void);
__declspec(dllimport) HANDLE WINAPI FindFirstFileA(LPCSTR,WIN32_FIND_DATAA*);
__declspec(dllimport) BOOL WINAPI FindNextFileA(HANDLE,WIN32_FIND_DATAA*);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE);
__declspec(dllimport) VOID WINAPI Sleep(DWORD);
void* __cdecl memcpy(void*,const void*,unsigned int);
void* __cdecl memset(void*,int,unsigned int);

#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 0x00000001UL
#define FILE_SHARE_WRITE 0x00000002UL
#define FILE_SHARE_DELETE 0x00000004UL
#define CREATE_ALWAYS 2UL
#define OPEN_EXISTING 3UL
#define FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define INVALID_FILE_SIZE 0xFFFFFFFFUL
__declspec(dllimport) HANDLE WINAPI CreateFileA(LPCSTR,DWORD,DWORD,LPVOID,DWORD,DWORD,HANDLE);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE,LPCVOID,DWORD,LPDWORD,LPVOID);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE,LPVOID,DWORD,LPDWORD,LPVOID);
__declspec(dllimport) DWORD WINAPI GetFileSize(HANDLE,LPDWORD);
__declspec(dllimport) DWORD WINAPI SetFilePointer(HANDLE,LONG,LONG*,DWORD);
__declspec(dllimport) VOID WINAPI GetSystemTime(SYSTEMTIME*);
__declspec(dllimport) VOID WINAPI GetLocalTime(SYSTEMTIME*);
__declspec(dllimport) DWORD WINAPI GetTickCount(void);

#ifdef __cplusplus
}
#endif
