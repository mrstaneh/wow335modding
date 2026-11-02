// wow335modding: see WebWait.h. Every wait object shares one mutex and one condition variable: any change wakes all
// waiters, and each re-checks its own handles. That makes waiting on several handles at once simple and correct.
// ponytail: one global lock; give objects their own conditions if contention ever shows up.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/Storm/Source/Web/WebWait.h"
#include <ctime>
#include <pthread.h>

namespace {

enum Kind { EVENT, MUTEX, SEMAPHORE, THREAD };

struct Object {
    Kind kind;
    bool manualReset;
    bool signaled;
    int32_t count;      // semaphore count, or mutex recursion depth
    int32_t maximum;
    pthread_t owner;    // mutex owner while count > 0
};

pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;

Object* New(Kind kind) {
    Object* object = new Object();
    object->kind = kind;
    return object;
}

// Called with s_lock held.
bool Ready(Object* object) {
    switch (object->kind) {
        case EVENT:
        case THREAD:
            return object->signaled;
        case MUTEX:
            return object->count == 0 || pthread_equal(object->owner, pthread_self());
        case SEMAPHORE:
            return object->count > 0;
    }
    return false;
}

// Called with s_lock held, for an object that is Ready: what a successful wait does to it.
void Take(Object* object) {
    switch (object->kind) {
        case EVENT:
            if (!object->manualReset) {
                object->signaled = false;
            }
            break;
        case MUTEX:
            object->owner = pthread_self();
            object->count++;
            break;
        case SEMAPHORE:
            object->count--;
            break;
        case THREAD:
            break;
    }
}

void Changed() {
    pthread_cond_broadcast(&s_changed);
    pthread_mutex_unlock(&s_lock);
}

}

void* WebWait::CreateEvent(bool manualReset, bool signaled) {
    Object* object = New(EVENT);
    object->manualReset = manualReset;
    object->signaled = signaled;
    return object;
}

void* WebWait::CreateMutex(bool owned) {
    Object* object = New(MUTEX);
    if (owned) {
        object->owner = pthread_self();
        object->count = 1;
    }
    return object;
}

void* WebWait::CreateSemaphore(int32_t count, int32_t maximum) {
    Object* object = New(SEMAPHORE);
    object->count = count;
    object->maximum = maximum;
    return object;
}

void* WebWait::CreateThreadHandle() {
    return New(THREAD);
}

bool WebWait::SetEvent(void* handle) {
    pthread_mutex_lock(&s_lock);
    static_cast<Object*>(handle)->signaled = true;
    Changed();
    return true;
}

bool WebWait::ResetEvent(void* handle) {
    pthread_mutex_lock(&s_lock);
    static_cast<Object*>(handle)->signaled = false;
    pthread_mutex_unlock(&s_lock);
    return true;
}

bool WebWait::ReleaseMutex(void* handle) {
    Object* object = static_cast<Object*>(handle);
    pthread_mutex_lock(&s_lock);
    if (object->count == 0 || !pthread_equal(object->owner, pthread_self())) {
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    object->count--;
    Changed();
    return true;
}

bool WebWait::ReleaseSemaphore(void* handle, int32_t count) {
    Object* object = static_cast<Object*>(handle);
    pthread_mutex_lock(&s_lock);
    if (object->count + count > object->maximum) {
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    object->count += count;
    Changed();
    return true;
}

void WebWait::ThreadFinished(void* handle) {
    pthread_mutex_lock(&s_lock);
    static_cast<Object*>(handle)->signaled = true;
    Changed();
}

uint32_t WebWait::Wait(void* handle, uint32_t timeout) {
    return WaitMultiple(1, &handle, true, timeout);
}

uint32_t WebWait::WaitMultiple(uint32_t count, void* const* handles, bool waitAll, uint32_t timeout) {
    if (!count) {
        return FAILED;
    }

    timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += static_cast<long>(timeout % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&s_lock);
    for (;;) {
        if (waitAll) {
            bool all = true;
            for (uint32_t i = 0; i < count && all; i++) {
                all = Ready(static_cast<Object*>(handles[i]));
            }
            if (all) {
                for (uint32_t i = 0; i < count; i++) {
                    Take(static_cast<Object*>(handles[i]));
                }
                pthread_mutex_unlock(&s_lock);
                return OBJECT_0;
            }
        } else {
            for (uint32_t i = 0; i < count; i++) {
                Object* object = static_cast<Object*>(handles[i]);
                if (Ready(object)) {
                    Take(object);
                    pthread_mutex_unlock(&s_lock);
                    return OBJECT_0 + i;
                }
            }
        }

        if (timeout == 0) {
            break;
        }
        if (timeout == FOREVER) {
            pthread_cond_wait(&s_changed, &s_lock);
        } else if (pthread_cond_timedwait(&s_changed, &s_lock, &deadline) != 0) {
            break;
        }
    }

    pthread_mutex_unlock(&s_lock);
    return TIMEOUT;
}

void WebWait::Close(void* handle) {
    delete static_cast<Object*>(handle);
}

#endif
