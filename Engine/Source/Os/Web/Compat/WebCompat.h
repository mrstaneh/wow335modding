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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define _snprintf WebSnprintf
#define _vsnprintf WebVsnprintf
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

// MSVC's printf spells the 64-bit length modifier I64 (%I64d, %016I64X); musl rejects the whole format and writes
// nothing. Storm's SStrPrintf even turns ll into I64 before it calls _snprintf, so achievement links in chat stopped
// at their guid. The format goes through with I64 turned into ll.
static inline int WebVsnprintf(char* dest, size_t size, const char* format, va_list args) {
    char fixed[1024];
    size_t out = 0;

    for (const char* p = format; *p && out < sizeof(fixed) - 1; p++) {
        fixed[out++] = *p;

        if (*p != '%') {
            continue;
        }

        while (p[1] && strchr("-+ #0123456789.*", p[1]) && out < sizeof(fixed) - 1) {
            fixed[out++] = *++p;
        }

        if (p[1] == 'I' && p[2] == '6' && p[3] == '4' && out < sizeof(fixed) - 2) {
            fixed[out++] = 'l';
            fixed[out++] = 'l';
            p += 3;
        }
    }

    fixed[out] = 0;
    return vsnprintf(dest, size, strlen(format) < sizeof(fixed) - 1 ? fixed : format, args);
}

static inline int WebSnprintf(char* dest, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = WebVsnprintf(dest, size, format, args);
    va_end(args);
    return result;
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
