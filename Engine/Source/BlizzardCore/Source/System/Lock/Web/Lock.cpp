// wow335modding: browser build of Lock/Win64 (Atomic.cpp, Event.cpp, Mutex.cpp). Threads are pthreads (Web Workers);
// a Windows auto-reset event becomes a mutex, a condition variable and a flag.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

namespace {
struct WebEvent {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
};

WebEvent* ToWeb(Blizzard::Lock::Event& event) {
    return static_cast<WebEvent*>(event);
}
}

int32_t Blizzard::Lock::Atomic::Increment() {
    return __atomic_add_fetch(&this->m_value, 1, __ATOMIC_SEQ_CST);
}

int32_t Blizzard::Lock::Atomic::Decrement() {
    return __atomic_sub_fetch(&this->m_value, 1, __ATOMIC_SEQ_CST);
}

void Blizzard::Lock::EventCreate(Event& event) {
    WebEvent* web = new WebEvent;
    pthread_mutex_init(&web->mutex, nullptr);
    pthread_cond_init(&web->cond, nullptr);
    web->signaled = false;
    event = web;
}

void Blizzard::Lock::EventDestroy(Event& event) {
    if (WebEvent* web = ToWeb(event)) {
        pthread_cond_destroy(&web->cond);
        pthread_mutex_destroy(&web->mutex);
        delete web;
    }
    event = nullptr;
}

void Blizzard::Lock::EventSet(Event& event) {
    WebEvent* web = ToWeb(event);
    pthread_mutex_lock(&web->mutex);
    web->signaled = true;
    pthread_cond_signal(&web->cond);
    pthread_mutex_unlock(&web->mutex);
}

void Blizzard::Lock::EventWaitForever(Event& event) {
    WebEvent* web = ToWeb(event);
    pthread_mutex_lock(&web->mutex);
    while (!web->signaled) {
        pthread_cond_wait(&web->cond, &web->mutex);
    }
    web->signaled = false; // auto-reset, like CreateEventA(nullptr, FALSE, FALSE, nullptr)
    pthread_mutex_unlock(&web->mutex);
}

Blizzard::Lock::Event* Blizzard::Lock::EventWait(uint32_t timeout, Event& event) {
    WebEvent* web = ToWeb(event);
    timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += static_cast<long>(timeout % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&web->mutex);
    int result = 0;
    while (!web->signaled && result != ETIMEDOUT) {
        result = pthread_cond_timedwait(&web->cond, &web->mutex, &deadline);
    }
    bool signaled = web->signaled;
    web->signaled = false;
    pthread_mutex_unlock(&web->mutex);
    return signaled ? &event : nullptr;
}

Blizzard::Lock::EventObject::EventObject() {
    EventCreate(this->m_event);
}

Blizzard::Lock::EventObject::~EventObject() {
    EventDestroy(this->m_event);
}

void Blizzard::Lock::MutexCreate(Mutex& mutex) {
    InitializeCriticalSection(&mutex);
}

void Blizzard::Lock::MutexEnter(Mutex& mutex) {
    EnterCriticalSection(&mutex);
}

void Blizzard::Lock::MutexLeave(Mutex& mutex) {
    LeaveCriticalSection(&mutex);
}

void Blizzard::Lock::MutexDestroy(Mutex& mutex) {
    DeleteCriticalSection(&mutex);
}

Blizzard::Lock::ScopedMutex::ScopedMutex(Mutex& mutex) {
    this->m_mutex = &mutex;
    MutexEnter(mutex);
}

Blizzard::Lock::ScopedMutex::~ScopedMutex() {
    MutexLeave(*this->m_mutex);
}

Blizzard::Lock::RMutex::RMutex() {
    MutexCreate(this->m_mutex);
}

Blizzard::Lock::RMutex::~RMutex() {
    MutexDestroy(this->m_mutex);
}

#endif
