// wow335modding: Windows-style wait handles for the browser build (events, mutexes, semaphores, finished threads),
// so platform code can keep its WaitForSingleObject/WaitForMultipleObjects logic.
#ifndef ENGINE_SOURCE_STORM_SOURCE_WEB_WEBWAIT_H
#define ENGINE_SOURCE_STORM_SOURCE_WEB_WEBWAIT_H

#include <cstdint>

namespace WebWait {

static const uint32_t OBJECT_0 = 0;          // WAIT_OBJECT_0: + index of the handle that ended the wait
static const uint32_t TIMEOUT = 258;         // WAIT_TIMEOUT
static const uint32_t FAILED = 0xFFFFFFFF;   // WAIT_FAILED
static const uint32_t FOREVER = 0xFFFFFFFF;  // INFINITE

void* CreateEvent(bool manualReset, bool signaled);
void* CreateMutex(bool owned);
void* CreateSemaphore(int32_t count, int32_t maximum);
void* CreateThreadHandle(); // signaled by ThreadFinished, for waiting on a thread

bool SetEvent(void* handle);
bool ResetEvent(void* handle);
bool ReleaseMutex(void* handle);
bool ReleaseSemaphore(void* handle, int32_t count);
void ThreadFinished(void* handle);

uint32_t Wait(void* handle, uint32_t timeout);
uint32_t WaitMultiple(uint32_t count, void* const* handles, bool waitAll, uint32_t timeout);
void Close(void* handle);

}

#endif
