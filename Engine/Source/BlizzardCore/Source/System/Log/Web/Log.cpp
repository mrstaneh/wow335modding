// wow335modding: browser build of Log/Win64/Log.cpp. A browser has no computer name, so the logs say "browser".
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <cstdlib>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"

char* System_Log::DefaultPreamble() {
    Blizzard::Time::TimeRec rec;
    Blizzard::Time::BreakTime(s_startTime, rec);
    char* preamble = static_cast<char*>(malloc(4096));
    Blizzard::String::Format(preamble, 4096, "\n#-----------------------------------------------------------\n# System started at %04d-%02d-%02d %02d:%02d:%02d.%04d\n# system: %s\n#-----------------------------------------------------------\n", rec.year, rec.month, rec.day, rec.hour, rec.minute, rec.second, rec.nanosecond / 100000, "browser");
    return preamble;
}

char* System_Log::DefaultDayHeader() {
    Blizzard::Time::TimeRec rec;
    Blizzard::Time::BreakTime(Blizzard::Time::GetTimestamp(), rec);
    char* header = static_cast<char*>(malloc(4096));
    Blizzard::String::Format(header, 4096, "\n#-----------------------------------------------------------\n# %04d-%02d-%02d\n# system: %s\n#-----------------------------------------------------------\n", rec.year, rec.month, rec.day, "browser");
    return header;
}

uint32_t System_Log::LogThread(void* param) {
    uint32_t lastFlush = static_cast<uint32_t>(Blizzard::Time::Milliseconds());

    while (s_state > 0) {
        Blizzard::Process::Sleep(100);
        uint32_t now = static_cast<uint32_t>(Blizzard::Time::Milliseconds());

        if (now - lastFlush > 5000) {
            lastFlush = now;

            if (Acquire(0)) {
                FlushLogs(false);
                Blizzard::Lock::MutexLeave(s_mutex);
            }
        }
    }

    s_threadDone = true;
    return 0;
}

#endif
