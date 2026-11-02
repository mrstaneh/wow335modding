// wow335modding: browser build of the rest of Os/Win64: System.cpp, Gui.cpp, TimeManager.cpp, Time.cpp, Tls.cpp,
// OsLibrary.cpp, OsClipboard.cpp, OsSecureRandom.cpp, OsIME.cpp, Joystick.cpp, OsURLDownload.cpp, OsVersionHash.cpp,
// Path.cpp and the address helpers of OsTcp.cpp. What a page cannot do (load libraries, read the clipboard, input
// methods, joysticks, version resources) reports "not available", and the client carries on without it.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Os/OsIME.h"
#include "Engine/Source/Os/OsLibrary.h"
#include "Engine/Source/Os/OsSecureRandom.h"
#include "Engine/Source/Os/OsTcp.h"
#include "Engine/Source/Os/OsURLDownload.h"
#include "Engine/Source/Os/OsVersionHash.h"
#include "Engine/Source/Os/Path.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Os/Time.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Os/Tls.h"
#include "Engine/Source/Storm/H/storm.h"

// The Win32 last-error value (declared in Os/Web/Compat/windows.h).
static thread_local DWORD s_lastError;

extern "C" void SetLastError(DWORD error) {
    s_lastError = error;
}

extern "C" DWORD GetLastError() {
    return s_lastError;
}

// Time: one high-resolution clock, in milliseconds since the program started. On worker threads emscripten_get_now()
// is wall-clock time (performance.timeOrigin + now, the same on every thread), far past 2^32 ms, so it is made
// relative first; like GetTickCount, the 32-bit value then wraps only after 49 days.
static double ElapsedMs() {
    static const double s_start = emscripten_get_now();
    return emscripten_get_now() - s_start;
}

uint32_t OsGetAsyncTimeMs() {
    return static_cast<uint32_t>(ElapsedMs());
}

uint64_t OsGetAsyncTimeClocks() {
    return static_cast<uint64_t>(ElapsedMs() * 1000.0);
}

int64_t OsGetAsyncClocksPerSecond() {
    return 1000000;
}

void OsTimeManagerCreate(OsTimeManager::TimingMethod) {
}

void OsTimeManagerDestroy() {
}

OsTimeManager::TimingMethod OsTimeGetTimingMethod() {
    return OsTimeManager::SystemMethod2;
}

uint32_t OsTimeGetTestError() {
    return 0;
}

const char* GetTimingMethodName(OsTimeManager::TimingMethod method) {
    return method == OsTimeManager::NotSet ? "[Not Set]" : "performance.now";
}

uint64_t OsGetProcessorTicksPerSecond() {
    return 1000000;
}

int32_t OsGetTimeZone() {
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    return static_cast<int32_t>(local.tm_gmtoff / 60);
}

void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    tm local;
    localtime_r(&now, &local);
    strftime(buffer, size, "%m%d%y_%H%M%S", &local);
}

void OsSleep(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
}

static int32_t s_sleepInBackground;
static uint32_t s_backgroundSleepMs;

void OsSetSleepInBackground(int32_t sleep) {
    s_sleepInBackground = sleep;
}

int32_t OsSleepInBackground() {
    return s_sleepInBackground;
}

void OsSetBackgroundSleepMs(uint32_t milliseconds) {
    s_backgroundSleepMs = milliseconds;
}

uint32_t OsGetBackgroundSleepMs() {
    return s_backgroundSleepMs;
}

// Thread-local storage: pthread keys.
uint32_t OsTlsAlloc() {
    pthread_key_t key;
    return pthread_key_create(&key, nullptr) == 0 ? static_cast<uint32_t>(key) : 0xFFFFFFFF;
}

void OsTlsFree(uint32_t index) {
    pthread_key_delete(static_cast<pthread_key_t>(index));
}

void* OsTlsGetValue(uint32_t index) {
    return pthread_getspecific(static_cast<pthread_key_t>(index));
}

int32_t OsTlsSetValue(uint32_t index, void* value) {
    return pthread_setspecific(static_cast<pthread_key_t>(index), value) == 0;
}

// The machine, as far as a page can tell.
int32_t OsGetComputerName(char* buffer, uint32_t* size) {
    SStrCopy(buffer, "browser", *size);
    *size = static_cast<uint32_t>(strlen(buffer));
    return 1;
}

int32_t OsGetUserName(char* buffer, uint32_t* size) {
    SStrCopy(buffer, "player", *size);
    *size = static_cast<uint32_t>(strlen(buffer));
    return 1;
}

uint32_t OsGetNumberOfProcessors() {
    int cores = emscripten_num_logical_cores();
    return cores > 0 ? cores : 1;
}

uint32_t OsGetOsId() {
    return 0;
}

uint64_t OsGetPhysicalMemory() {
    return 4ull << 30; // the WebAssembly heap can grow to 4 GB
}

uint32_t OsGetProcessorFeaturesEx(int32_t& vendor) {
    vendor = 0;
    return Blizzard::Hardware::GetProcessorFeatures();
}

uint32_t OsGetProcessorId() {
    return 0;
}

void OsGetVersionString(char* buffer, int32_t size) {
    SStrCopy(buffer, "Web Browser", size);
}

int32_t OsIsRemoteSession() {
    return 0;
}

void OsSystemEnableCpuLog() {
}

void OsValidateFunctionPointer(const void*) {
}

uint32_t GetModuleFileNameUTF8(void*, char* path, uint32_t size) {
    SStrCopy(path, "/wow/Wow.exe", size);
    return static_cast<uint32_t>(strlen(path));
}

void OsSecureRandom(uint8_t* buffer, uint32_t size) {
    while (size) {
        uint32_t chunk = size > 256 ? 256 : size; // getentropy's limit per call
        getentropy(buffer, chunk);
        buffer += chunk;
        size -= chunk;
    }
}

// The page is the only window. The canvas has no native handle, so the "window" is a fixed non-null token.
static int32_t s_window;

void* OsGuiGetWindow(int32_t) {
    return &s_window;
}

void OsGuiSetGxWindow(void*) {
}

void OsGuiSetWindowTitle(void*, const char* title) {
    MAIN_THREAD_EM_ASM({ document.title = UTF8ToString($0); }, title);
}

int32_t OsGuiMessageBox(void*, int32_t, const char* message, const char* title) {
    fprintf(stderr, "%s: %s\n", title ? title : "", message ? message : "");
    MAIN_THREAD_EM_ASM({ alert(UTF8ToString($0) + "\n\n" + UTF8ToString($1)); }, title ? title : "", message ? message : "");
    return 1; // IDOK
}

int32_t OsLaunchURL(const char* url) {
    MAIN_THREAD_EM_ASM({ window.open(UTF8ToString($0), "_blank"); }, url);
    return 1;
}

// No native libraries in a browser.
void* OsLoadLibrary(const char*) {
    return nullptr;
}

void OsFreeLibrary(void*) {
}

void* OsGetLibraryEntryPoint(void*, const char*) {
    return nullptr;
}

// Clipboard: a page may write it, but reading needs a permission prompt and is asynchronous.
// ponytail: copy only; paste needs an async clipboard read fed through the input queue.
char* IOsClipboardGetString(void*) {
    return nullptr;
}

int32_t IOsClipboardPutString(const char* string, void*) {
    MAIN_THREAD_EM_ASM({ if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString($0)); }, string);
    return 1;
}

void OsClipboardFreeString(char* string) {
    free(string);
}

// No input method editors: text arrives as finished characters.
void OsIMEInitialize() {
}

void OsIMEDestroy() {
}

void OsIMEEnable(int32_t) {
}

void OsIMESync() {
}

int32_t OsIMEGetLanguageMode() {
    return 0;
}

void OsIMESetLanguageMode(int32_t) {
}

int32_t OsIMEGetCompositionString(char* buffer, uint32_t bufferSize) {
    if (buffer && bufferSize) {
        *buffer = '\0';
    }
    return 0;
}

int32_t OsIMEGetCompositionResult(char* buffer, uint32_t bufferSize) {
    if (buffer && bufferSize) {
        *buffer = '\0';
    }
    return 0;
}

int32_t OsIMEGetClauseInfo(uint32_t&, uint32_t&, uint32_t&) {
    return 0;
}

int32_t OsIMEGetCandidates(uint32_t, uint32_t&, uint32_t&, uint32_t&, uint32_t&, TSGrowableArray<OsIMECandidate>&) {
    return 0;
}

// No joysticks. ponytail: the Gamepad API could drive these.
uint32_t OsJoystickOpen(uint32_t) {
    return 0;
}

void OsJoystickClose(uint32_t) {
}

char* OsJoystickGetName(uint32_t, char* buffer, uint32_t size) {
    if (buffer && size) {
        *buffer = '\0';
    }
    return buffer;
}

void OsJoystickMapHat(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {
}

void OsJoystickMapSlider(uint32_t, int32_t) {
}

void OsJoystickMapStick(uint32_t, int32_t, int32_t, int32_t) {
}

void OsJoystickConfigureMouseMode(uint32_t, int32_t, int32_t, int32_t, float) {
}

void OsJoystickSetMouseMode(uint32_t, int32_t) {
}

bool OsURLDownload(const char*, OsURLDownloadCallback, void*) {
    return false;
}

// Executable version data: there are no executables. The checksum feeds an HMAC the realm server does not check by
// default (AzerothCore StrictVersionCheck = 0).
int32_t IOsVersionChecksum(IOsVersionHashCallback&, const char*, int32_t) {
    return 1;
}

OsFileVersionInfo::OsFileVersionInfo(const char*) {
    const uint16_t version[4] = { 3, 3, 5, 12340 };
    memcpy(this->m_fileVersion, version, sizeof(version));
    memcpy(this->m_productVersion, version, sizeof(version));
}

// Network address helpers (OsTcp.cpp): a NETADDR holds a sockaddr_in.
uint32_t OsNetAddrGetAddress(const NETADDR* addr, uint16_t* port) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);
    if (port) {
        *port = ntohs(in->sin_port);
    }
    return in->sin_addr.s_addr;
}

const char* OsNetAddrToStr(const NETADDR* addr, char* buffer, uint32_t size) {
    const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(addr);
    SStrPrintf(buffer, size, "%s:%u", inet_ntoa(in->sin_addr), ntohs(in->sin_port));
    return buffer;
}

// ponytail: the old OsNet layer (OsTcp.cpp) is not ported; the game's connections use WowConnection.
void OsNetPump(uint32_t timeout) {
    OsSleep(timeout);
}

#endif
