// wow335modding: browser build of Process/Win64/Process.cpp. The "executable" lives at /wow/Wow.exe in Emscripten's
// file system, so the client looks for its Data folder, WTF and Logs under /wow/. There are no child processes.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/Path.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"

bool Blizzard::Process::GetProcessPath(char* buffer, int32_t buffersize) {
    if (!buffer || buffersize <= 0) {
        return false;
    }

    Blizzard::String::Copy(buffer, "/wow/Wow.exe", buffersize);
    return true;
}

bool Blizzard::Process::GetProcessDirectory(char* buffer, int32_t buffersize) {
    if (!buffer || buffersize <= 0) {
        return false;
    }

    Blizzard::String::Copy(buffer, "/wow/", buffersize);
    return true;
}

bool Blizzard::Process::LaunchChildProcessWithArguments(const char*, const char*, void (*)(void*), void*) {
    return false;
}

void Blizzard::Process::Sleep(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
}

#endif
