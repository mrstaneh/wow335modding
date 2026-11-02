// wow335modding: browser build of Storm's Win64 SErr.cpp, SLog.cpp and SReg.cpp. Errors and logs go to the browser
// console (stderr) and to files; fatal errors stop the client. The registry is an in-memory table.
// ponytail: registry values are lost on reload; persist them (IDBFS) once something needs them across sessions.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include "Engine/Source/Storm/H/storm.h"

static thread_local uint32_t s_lastError;
static const char* s_fatalFile;
static int32_t s_fatalLine;

void STORMAPI SErrSetLastError(uint32_t errorcode) {
    s_lastError = errorcode;
}

uint32_t STORMAPI SErrGetLastError() {
    return s_lastError;
}

void SErrInitialize() {
}

int32_t STORMAPI SErrDestroy() {
    return 1;
}

void STORMAPI SErrCatchUnhandledExceptions() {
}

void STORMAPI SErrRegisterHandler(int32_t (STORMAPI*)(uint32_t, const char*, const char*, int32_t, const char*)) {
}

void STORMAPI SErrSetLogCallback(int32_t (STORMAPI*)(char*, uint32_t)) {
}

void STORMAPI SErrSetLogTitleString(const char*) {
}

int32_t STORMAPI SErrGetLogLastPath(char* buf, uint32_t size) {
    if (buf && size) {
        *buf = '\0';
    }
    return 0;
}

int32_t STORMAPI SErrIsDisplayingError() {
    return 0;
}

int32_t STORMAPI SErrGetErrorStr(uint32_t errorcode, char* buffer, uint32_t bufferchars) {
    if (!buffer || !bufferchars) {
        return 0;
    }
    snprintf(buffer, bufferchars, "error 0x%08X", errorcode);
    return 1;
}

void STORMAPI SErrPrepareAppFatal(const char* filename, int32_t linenumber) {
    s_fatalFile = filename;
    s_fatalLine = linenumber;
}

[[noreturn]] static void Fatal(const char* message) {
    fprintf(stderr, "FATAL: %s (%s:%d)\n", message, s_fatalFile ? s_fatalFile : "?", s_fatalLine);
    abort();
}

void SErrDisplayAppFatal(const char* format, ...) {
    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Fatal(message);
}

void SErrDisplayAppFatalCustom(uint32_t errorcode, const char* format, ...) {
    char message[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    fprintf(stderr, "error 0x%08X\n", errorcode);
    Fatal(message);
}

void SErrDisplayAppFatalMessage(const char* message, const char* filename, int32_t linenumber) {
    SErrPrepareAppFatal(filename, linenumber);
    Fatal(message);
}

int32_t STORMAPI SErrDisplayError(uint32_t errorcode, const char* filename, int32_t linenumber, const char* description, int32_t recoverable, uint32_t exitcode, uint32_t) {
    fprintf(stderr, "%s error 0x%08X at %s:%d: %s\n", recoverable ? "Recoverable" : "Fatal", errorcode, filename ? filename : "?", linenumber, description ? description : "");
    if (!recoverable) {
        exit(static_cast<int>(exitcode));
    }
    return 1;
}

int32_t SErrDisplayErrorFmt(uint32_t errorcode, const char* filename, int32_t linenumber, int32_t recoverable, uint32_t exitcode, const char* format, ...) {
    char description[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(description, sizeof(description), format, args);
    va_end(args);
    return SErrDisplayError(errorcode, filename, linenumber, description, recoverable, exitcode, 0);
}

// Logs: an HSLOG is a FILE*. Game paths use backslashes.
void STORMAPI SLogInitialize() {
}

void STORMAPI SLogDestroy() {
}

int32_t STORMAPI SLogCreate(const char* filename, uint32_t, HSLOG* log) {
    std::string path = filename ? filename : "";
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    *log = reinterpret_cast<HSLOG>(fopen(path.c_str(), "a"));
    return *log != nullptr;
}

void STORMAPI SLogClose(HSLOG log) {
    if (log) {
        fclose(reinterpret_cast<FILE*>(log));
    }
}

void STORMAPI SLogFlush(HSLOG log) {
    if (log) {
        fflush(reinterpret_cast<FILE*>(log));
    }
}

void SLogWrite(HSLOG log, const char* format, ...) {
    if (!log) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(reinterpret_cast<FILE*>(log), format, args);
    va_end(args);
    fputc('\n', reinterpret_cast<FILE*>(log));
}

// Registry: "key\value" -> text.
static std::mutex s_registryLock;
static std::map<std::string, std::string> s_registry;

static std::string RegistryName(const char* keyname, const char* valuename) {
    return std::string(keyname ? keyname : "") + "\\" + (valuename ? valuename : "");
}

int32_t STORMAPI SRegLoadString(const char* keyname, const char* valuename, uint32_t, char* buffer, uint32_t buffersize) {
    std::lock_guard<std::mutex> lock(s_registryLock);
    auto it = s_registry.find(RegistryName(keyname, valuename));
    if (it == s_registry.end() || !buffer || !buffersize) {
        return 0;
    }
    snprintf(buffer, buffersize, "%s", it->second.c_str());
    return 1;
}

int32_t STORMAPI SRegLoadValue(const char* keyname, const char* valuename, uint32_t, uint32_t* value) {
    std::lock_guard<std::mutex> lock(s_registryLock);
    auto it = s_registry.find(RegistryName(keyname, valuename));
    if (it == s_registry.end() || !value) {
        return 0;
    }
    *value = static_cast<uint32_t>(strtoul(it->second.c_str(), nullptr, 10));
    return 1;
}

int32_t STORMAPI SRegSaveString(const char* keyname, const char* valuename, uint32_t, const char* string) {
    std::lock_guard<std::mutex> lock(s_registryLock);
    s_registry[RegistryName(keyname, valuename)] = string ? string : "";
    return 1;
}

int32_t STORMAPI SRegSaveValue(const char* keyname, const char* valuename, uint32_t, uint32_t value) {
    std::lock_guard<std::mutex> lock(s_registryLock);
    s_registry[RegistryName(keyname, valuename)] = std::to_string(value);
    return 1;
}

#endif
