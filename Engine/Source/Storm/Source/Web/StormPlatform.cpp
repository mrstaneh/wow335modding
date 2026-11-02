// wow335modding: browser build of Storm's Win64 platform files SCritSect.cpp, SLock.cpp (sync objects), SThread.cpp,
// SServer.cpp, SMsg.cpp and SCmd.cpp. Handles are WebWait objects; threads are pthreads.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <new>
#include <pthread.h>
#include <unistd.h>
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Storm/H/SCritSect.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/SLock.h"
#include "Engine/Source/Storm/Source/SThread.h"
#include "Engine/Source/Storm/Source/Web/WebWait.h"

CCritSect::CCritSect() {
    InitializeCriticalSection(&this->m_critsect);
}

CCritSect::~CCritSect() {
    DeleteCriticalSection(&this->m_critsect);
}

void CCritSect::Enter() {
    EnterCriticalSection(&this->m_critsect);
}

void CCritSect::Enter(int32_t) {
    EnterCriticalSection(&this->m_critsect);
}

void CCritSect::Leave() {
    LeaveCriticalSection(&this->m_critsect);
}

void CCritSect::Leave(int32_t) {
    LeaveCriticalSection(&this->m_critsect);
}

SCritSect::SCritSect() {
    InitializeCriticalSection(&this->m_critsect);
}

SCritSect::~SCritSect() {
    DeleteCriticalSection(&this->m_critsect);
}

void SCritSect::Enter() {
    EnterCriticalSection(&this->m_critsect);
}

void SCritSect::Leave() {
    LeaveCriticalSection(&this->m_critsect);
}

// ponytail: readers and writers both take one recursive lock (correct, less parallel); use pthread_rwlock if it matters.
static CRITICAL_SECTION*& RWLock(SRWLock::SURWLOCK& data) {
    return *reinterpret_cast<CRITICAL_SECTION**>(&data);
}

CSRWLock::CSRWLock() {
    RWLock(this->m_opaqueData) = new CRITICAL_SECTION;
    InitializeCriticalSection(RWLock(this->m_opaqueData));
}

CSRWLock::~CSRWLock() {
    DeleteCriticalSection(RWLock(this->m_opaqueData));
    delete RWLock(this->m_opaqueData);
}

void CSRWLock::Enter(int32_t) {
    EnterCriticalSection(RWLock(this->m_opaqueData));
}

void CSRWLock::Leave(int32_t) {
    LeaveCriticalSection(RWLock(this->m_opaqueData));
}

SSyncObject::SSyncObject() {
    this->m_opaqueData = nullptr;
}

SSyncObject::~SSyncObject() {
    this->Close();
}

void SSyncObject::Close() {
    if (this->m_opaqueData) {
        WebWait::Close(this->m_opaqueData);
        this->m_opaqueData = nullptr;
    }
}

int32_t SSyncObject::Valid() {
    return this->m_opaqueData != nullptr;
}

uint32_t SSyncObject::Wait(uint32_t timeout) {
    return WebWait::Wait(this->m_opaqueData, timeout);
}

uint32_t STORMAPI SSyncObject::WaitMultiplePtr(uint32_t count, SSyncObject** objects, int32_t waitAll, uint32_t timeout) {
    void* handles[64];
    if (count > 64) {
        return WebWait::FAILED;
    }

    uint32_t handleCount = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (objects[i] && objects[i]->m_opaqueData) {
            handles[handleCount++] = objects[i]->m_opaqueData;
        }
    }

    return WebWait::WaitMultiple(handleCount, handles, waitAll != 0, timeout);
}

SEvent::SEvent(int32_t manualReset, int32_t initialState) {
    this->m_opaqueData = WebWait::CreateEvent(manualReset != 0, initialState != 0);
}

int32_t SEvent::Set() {
    return WebWait::SetEvent(this->m_opaqueData);
}

int32_t SEvent::Reset() {
    return WebWait::ResetEvent(this->m_opaqueData);
}

SMutex::SMutex() {
    this->Create(0, nullptr);
}

void SMutex::Create(int32_t initialOwner, const char*) {
    this->Close();
    this->m_opaqueData = WebWait::CreateMutex(initialOwner != 0);
}

int32_t SMutex::Release() {
    return WebWait::ReleaseMutex(this->m_opaqueData);
}

SSemaphore::SSemaphore(int32_t initialCount, int32_t maximumCount) {
    this->m_opaqueData = WebWait::CreateSemaphore(initialCount, maximumCount);
}

int32_t SSemaphore::Signal(uint32_t count) {
    return WebWait::ReleaseSemaphore(this->m_opaqueData, static_cast<int32_t>(count));
}

namespace {
struct ThreadLaunch {
    uint32_t (STORMAPI* proc)(void*);
    void* param;
    void* handle;
};

void* LaunchThread(void* param) {
    ThreadLaunch* launch = static_cast<ThreadLaunch*>(param);
    launch->proc(launch->param);
    WebWait::ThreadFinished(launch->handle); // the handle outlives the thread; its owner closes it
    delete launch;
    return nullptr;
}
}

uint32_t STORMAPI SGetCurrentThreadId() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
}

uint32_t STORMAPI SGetCurrentProcessId() {
    return 1;
}

int32_t STORMAPI SGetCurrentThreadPriority() {
    return 0; // THREAD_PRIORITY_NORMAL
}

int32_t STORMAPI SSetCurrentThreadPriority(int32_t) {
    return 1;
}

int32_t STORMAPI SSetCurrentProcessAffinityMask(size_t) {
    return 1;
}

void* SCreateThread(uint32_t dwStackSize, uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t, uint32_t* lpThreadId, char*) {
    ThreadLaunch* launch = new ThreadLaunch { lpStartAddress, lpParameter, WebWait::CreateThreadHandle() };
    void* handle = launch->handle;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (dwStackSize) {
        pthread_attr_setstacksize(&attr, dwStackSize);
    }

    pthread_t thread;
    bool created = pthread_create(&thread, &attr, LaunchThread, launch) == 0;
    pthread_attr_destroy(&attr);

    if (!created) {
        WebWait::Close(handle);
        delete launch;
        return nullptr;
    }

    if (lpThreadId) {
        *lpThreadId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread));
    }

    return handle;
}

void* STORMAPI SCreateThread(uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t* lpThreadId, void*, char* threadName, uint32_t stackSize) {
    return SCreateThread(stackSize, lpStartAddress, lpParameter, 0, lpThreadId, threadName);
}

int32_t STORMAPI SGetThreadName(uint32_t, char* buffer, int32_t bufferchars) {
    if (buffer && bufferchars > 0) {
        *buffer = '\0';
    }
    return 0;
}

int32_t STORMAPI SCreateProcess(const char*, char*, void (*)(void*), void*) {
    return 0;
}

int32_t SThread::Create(uint32_t (STORMAPI* threadProc)(void*), void* param, SThread& thread, char* threadName, uint32_t stackSize) {
    uint32_t threadId;
    thread.m_opaqueData = SCreateThread(threadProc, param, &threadId, nullptr, threadName, stackSize);
    return thread.m_opaqueData != nullptr;
}

int32_t SThread::SetPriority(int32_t) {
    return 1;
}

int32_t CInitCritSect::Enter() {
    int32_t result = 0;

    if (!this->m_critsect) {
        while (__atomic_exchange_n(&this->m_spinLock, 1, __ATOMIC_SEQ_CST)) {
        }

        if (!this->m_critsect) {
            this->m_critsect = reinterpret_cast<CCritSect*>(this->m_critsectData);
            new (this->m_critsectData) CCritSect();
            result = 1;
        }

        __atomic_store_n(&this->m_spinLock, 0, __ATOMIC_SEQ_CST);
    }

    this->m_critsect->Enter();
    return result;
}

void CInitCritSect::Leave() {
    this->m_critsect->Leave();
}

void* CreateNamedEventA(const char*, int32_t manualReset) {
    return WebWait::CreateEvent(manualReset != 0, false);
}

void SServerInitialize() {
}

void SServerDestroy() {
}

HWND__* STORMAPI SMsgGetDefaultWindow() {
    return nullptr;
}

int32_t STORMAPI SMsgDestroy() {
    return 1;
}

void SDebugPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

// A web page has no command line.
int32_t STORMAPI SCmdProcessCommandLine(int32_t (STORMAPI*)(const char*), void (STORMAPI*)(_CMDERROR*)) {
    return 1;
}

#endif
