// wow335modding: force-included into every file of the browser build (see the root CMakeLists.txt).
// It papers over MSVC-only spellings so the decompiled code compiles unchanged with Emscripten's clang.
#ifndef ENGINE_SOURCE_OS_WEB_COMPAT_WEBCOMPAT_H
#define ENGINE_SOURCE_OS_WEB_COMPAT_WEBCOMPAT_H

#include <alloca.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

// In Microsoft mode __unaligned is a keyword, but Emscripten's SSE headers use it as a struct name.
#define __unaligned __wowdecomp_unaligned

#ifdef __cplusplus
// MSVC's type_info::raw_name() is its decorated name; the allocators only use it as a label for leak reports.
#define raw_name name
#endif

// MSVC C runtime names
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _strtoi64 strtoll
#define _strtoui64 strtoull
#define _isnan isnan
#define _finite isfinite
#define _atoi64 atoll
#define _msize malloc_usable_size
#define _ismbcspace isspace

// MSVC's CRT teardown before _exit; here that only means flushing open streams.
static inline void _cexit(void) {
    fflush(NULL);
}

static inline int fopen_s(FILE** file, const char* name, const char* mode) {
    *file = fopen(name, mode);
    return *file ? 0 : errno;
}

static inline char* _strupr(char* s) {
    for (char* p = s; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    return s;
}

static inline char* _strlwr(char* s) {
    for (char* p = s; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    return s;
}

#endif
