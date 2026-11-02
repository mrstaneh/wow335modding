// wow335modding: FMOD's OS layer for the browser build (win32/src/fmod_os_misc.cpp on Windows): pthreads, POSIX
// semaphores and files, and the page's clock.
#include "fmod_settings.h"

#include "fmod.h"
#include "fmod_os_misc.h"
#include "fmod_memory.h"

#include <emscripten/emscripten.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int gSizeofCriticalSection = sizeof(pthread_mutex_t);
int gSizeofSemaphore       = sizeof(sem_t);

void *FMOD_OS_Memory_Alloc(int size, FMOD_MEMORY_TYPE type)
{
    return malloc(size);
}

void *FMOD_OS_Memory_Realloc(void *ptr, int size, FMOD_MEMORY_TYPE type)
{
    return realloc(ptr, size);
}

void FMOD_OS_Memory_Free(void *ptr, FMOD_MEMORY_TYPE type)
{
    free(ptr);
}

FMOD_RESULT FMOD_OS_File_DriveStatus()
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Open(const char *name, char *mode, int unicode, unsigned int *filesize, void **handle)
{
    FILE *file = unicode ? NULL : fopen(name, "rb");

    if (!file)
    {
        return FMOD_ERR_FILE_NOTFOUND;
    }

    fseek(file, 0, SEEK_END);
    *filesize = (unsigned int)ftell(file);
    fseek(file, 0, SEEK_SET);
    *handle = file;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Close(void *handle)
{
    if (!handle)
    {
        return FMOD_ERR_FILE_BAD;
    }

    fclose((FILE *)handle);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Cancel(void *handle)
{
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Read(void *handle, void *buf, unsigned int count, unsigned int *read)
{
    *read = (unsigned int)fread(buf, 1, count, (FILE *)handle);

    if (*read != count)
    {
        return ferror((FILE *)handle) ? FMOD_ERR_FILE_BAD : FMOD_ERR_FILE_EOF;
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_File_Seek(void *handle, unsigned int offset)
{
    return fseek((FILE *)handle, offset, SEEK_SET) == 0 ? FMOD_OK : FMOD_ERR_FILE_BAD;
}

FMOD_RESULT FMOD_OS_Debug_OutputStr(const char *s)
{
    fputs(s, stderr);
    return FMOD_OK;
}

// Milliseconds since FMOD first asked. On worker threads emscripten_get_now() is wall-clock time, far too large for
// the 32-bit counters below, so it is made relative first (and wraps like the Windows timers do).
static double ElapsedMs()
{
    static const double start = emscripten_get_now();
    return emscripten_get_now() - start;
}

/* Microseconds, despite the name: "1000th of a millisecond". */
FMOD_RESULT FMOD_OS_Time_GetNs(unsigned int *ns)
{
    *ns = (unsigned int)(uint64_t)(ElapsedMs() * 1000.0);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Time_GetMs(unsigned int *ms)
{
    *ms = (unsigned int)(uint64_t)ElapsedMs();
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Time_Sleep(unsigned int ms)
{
    usleep(ms * 1000);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Thread_GetCurrentID(FMOD_UINT_NATIVE *id)
{
    *id = (FMOD_UINT_NATIVE)pthread_self();
    return FMOD_OK;
}

struct ThreadStart
{
    THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param);
    void *param;
};

static void *RunThread(void *arg)
{
    ThreadStart start = *(ThreadStart *)arg;
    free(arg);
    start.func(start.param);
    return NULL;
}

/* Priorities are ignored: a browser does not let pages set them. */
FMOD_RESULT FMOD_OS_Thread_Create(const char *name, THREAD_RETURNTYPE (THREAD_CALLCONV *func)(void *param), void *param, FMOD_THREAD_PRIORITY priority, void *stack, int stacksize, void **handle)
{
    if (!handle)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    ThreadStart *start = (ThreadStart *)malloc(sizeof(ThreadStart));
    start->func = func;
    start->param = param;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, stacksize > 0x100000 ? stacksize : 0x100000); /* see BlizzardCore Thread/Web */

    pthread_t thread;
    int error = pthread_create(&thread, &attr, RunThread, start);
    pthread_attr_destroy(&attr);

    if (error)
    {
        free(start);
        return FMOD_ERR_MEMORY;
    }

    *handle = (void *)thread;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Thread_Destroy(void *handle)
{
    return FMOD_OK; /* detached: nothing to release */
}

static pthread_mutex_t gMemoryCrit;

FMOD_RESULT FMOD_OS_CriticalSection_Create(FMOD_OS_CRITICALSECTION **crit, bool memorycrit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    /* Can't use FMOD's allocator for the critical section the memory system itself uses. */
    pthread_mutex_t *mutex = memorycrit ? &gMemoryCrit : (pthread_mutex_t *)FMOD_Memory_Alloc(sizeof(pthread_mutex_t));

    if (!mutex)
    {
        return FMOD_ERR_MEMORY;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    *crit = (FMOD_OS_CRITICALSECTION *)mutex;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Free(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_destroy((pthread_mutex_t *)crit);

    if (crit != (FMOD_OS_CRITICALSECTION *)&gMemoryCrit)
    {
        FMOD_Memory_Free(crit);
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Enter(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock((pthread_mutex_t *)crit);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_CriticalSection_Leave(FMOD_OS_CRITICALSECTION *crit)
{
    if (!crit)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pthread_mutex_unlock((pthread_mutex_t *)crit);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Create(FMOD_OS_SEMAPHORE **sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_t *semaphore = (sem_t *)FMOD_Memory_Alloc(sizeof(sem_t));

    if (!semaphore)
    {
        return FMOD_ERR_MEMORY;
    }

    sem_init(semaphore, 0, 0);
    *sema = (FMOD_OS_SEMAPHORE *)semaphore;
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Free(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_destroy((sem_t *)sema);
    FMOD_Memory_Free(sema);
    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Wait(FMOD_OS_SEMAPHORE *sema)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    while (sem_wait((sem_t *)sema) != 0)
    {
    }

    return FMOD_OK;
}

FMOD_RESULT FMOD_OS_Semaphore_Signal(FMOD_OS_SEMAPHORE *sema, bool interrupt)
{
    if (!sema)
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    sem_post((sem_t *)sema);
    return FMOD_OK;
}

/* No plugin libraries in a page. */
FMOD_RESULT FMOD_OS_Library_Load(const char *dllname, FMOD_OS_LIBRARY **handle)
{
    return FMOD_ERR_FILE_NOTFOUND;
}

FMOD_RESULT FMOD_OS_Library_GetProcAddress(FMOD_OS_LIBRARY *handle, const char *procname, void **address)
{
    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT FMOD_OS_Library_Free(FMOD_OS_LIBRARY *handle)
{
    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT FMOD_OS_CheckDriverList(bool *devicelistchanged)
{
    *devicelistchanged = false;
    return FMOD_OK;
}
