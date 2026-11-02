/* wow335modding: a small stand-in for <windows.h> in the browser build, found first on its include path.
 * Only types, constants and functions with an obvious browser equivalent live here. Anything that is real platform
 * work (windows, input, files, sockets, threads) belongs in the Web platform files next to Win32/Win64 instead.
 * Must stay valid C as well as C++: some of the C libraries include it too.
 * ponytail: grows one compile error at a time; add what the next error asks for, nothing speculative. */
#ifndef ENGINE_SOURCE_OS_WEB_COMPAT_WINDOWS_H
#define ENGINE_SOURCE_OS_WEB_COMPAT_WINDOWS_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD; /* 32-bit, like on Windows: long is 32-bit in wasm32 */
typedef unsigned int UINT;
typedef int INT;
typedef long LONG;
typedef unsigned long ULONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef char CHAR;
typedef wchar_t WCHAR;
typedef float FLOAT;
typedef uintptr_t UINT_PTR, ULONG_PTR, DWORD_PTR;
typedef intptr_t INT_PTR, LONG_PTR;
typedef size_t SIZE_T;
typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef wchar_t* LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef BYTE* LPBYTE;
typedef DWORD* LPDWORD;

typedef void* HANDLE;
/* Distinct handle types, the way windows.h declares them (storm.h forward-declares HINSTANCE__). */
#define DECLARE_HANDLE(name) struct name##__; typedef struct name##__* name
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HINSTANCE);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HICON);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HMENU);
typedef HINSTANCE HMODULE;
typedef HICON HCURSOR;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM, LRESULT;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#define WINAPI
#define APIENTRY
#define CALLBACK
#define __stdcall
#define __cdecl
#define __fastcall

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

/* Only the type: BlizzardCore's File.h declares its async reads with it. The browser file code is its own. */
typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct {
            DWORD Offset;
            DWORD OffsetHigh;
        };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct tagRECT {
    LONG left, top, right, bottom;
} RECT, *LPRECT;

typedef struct tagPOINT {
    LONG x, y;
} POINT, *LPPOINT;

static inline UINT GetACP(void) {
    return 1252; /* Western European, what the enUS client reports */
}

/* There is one module, the page; callers only use the handle as an identity. */
static inline HMODULE GetModuleHandleA(LPCSTR name) {
    (void)name;
    return (HMODULE)1;
}

static inline void ExitProcess(UINT code) {
    _exit((int)code);
}

/* Critical sections are recursive mutexes. */
typedef struct _CRITICAL_SECTION {
    pthread_mutex_t mutex;
} CRITICAL_SECTION, *LPCRITICAL_SECTION;

static inline void InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

static inline void EnterCriticalSection(LPCRITICAL_SECTION cs) {
    pthread_mutex_lock(&cs->mutex);
}

static inline BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) {
    return pthread_mutex_trylock(&cs->mutex) == 0;
}

static inline void LeaveCriticalSection(LPCRITICAL_SECTION cs) {
    pthread_mutex_unlock(&cs->mutex);
}

static inline void DeleteCriticalSection(LPCRITICAL_SECTION cs) {
    pthread_mutex_destroy(&cs->mutex);
}

/* Interlocked operations return what Windows returns: the new value for Increment/Decrement, the old one otherwise. */
static inline LONG InterlockedIncrement(LONG volatile* p) {
    return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedDecrement(LONG volatile* p) {
    return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedExchange(LONG volatile* p, LONG value) {
    return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedExchangeAdd(LONG volatile* p, LONG value) {
    return __atomic_fetch_add(p, value, __ATOMIC_SEQ_CST);
}

static inline LONG InterlockedCompareExchange(LONG volatile* p, LONG exchange, LONG comparand) {
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

static inline PVOID InterlockedCompareExchangePointer(PVOID volatile* p, PVOID exchange, PVOID comparand) {
    __atomic_compare_exchange_n(p, &comparand, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

#endif
