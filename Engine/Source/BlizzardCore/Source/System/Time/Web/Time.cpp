// wow335modding: browser build of Time/Win64/Time.cpp. A Timestamp counts nanoseconds since 2000-01-01 00:00 UTC.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <ctime>
#include <emscripten.h>
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"

static const int64_t UNIX_SECONDS_AT_2000 = 946684800;
static const int64_t NANOSECONDS = 1000000000;

const int32_t System_Time::s_daysBeforeMonth[13] = { 0, 31, 59, 90, 120, 151, 182, 212, 243, 273, 304, 334, 365 };
uint64_t System_Time::timeScaleCycles = 0xFFFFFFFFFFFFFFFF;
bool System_Time::s_usePerformanceCounter;
Blizzard::Lock::Atomic System_Time::s_tickLock;
int64_t System_Time::s_absBegin;
uint32_t System_Time::s_lastTickCount;
double System_Time::timeScaleMilliseconds;
Blizzard::Time::Timestamp System_Time::s_gmBegin;
uint64_t System_Time::s_tickCountHigh;
double System_Time::timeScaleNanoseconds;
double System_Time::timeScaleSeconds;
double System_Time::timeScaleMicroseconds;

Blizzard::Time::Timestamp Blizzard::Time::FromFileTime(uint64_t fileTime) {
    if (fileTime <= 0x0077A5D6AA8BF851) {
        return static_cast<Timestamp>(0x8000000000000000);
    }

    if (fileTime <= 0x030701FFA04E87AE) {
        return static_cast<Timestamp>(fileTime * 100 + 0x5143382561530000);
    }

    return 0x7FFFFFFFFFFFFFFF;
}

uint64_t System_Time::GetTickCount64() {
    return static_cast<uint64_t>(emscripten_get_now());
}

bool System_Time::TimeInit() {
    return true;
}

void Blizzard::Time::BreakTime(Timestamp time, TimeRec& rec) {
    int64_t seconds = time / NANOSECONDS;
    int32_t nanosecond = static_cast<int32_t>(time % NANOSECONDS);
    if (nanosecond < 0) {
        nanosecond += NANOSECONDS;
        seconds--;
    }

    time_t unixTime = static_cast<time_t>(seconds + UNIX_SECONDS_AT_2000);
    tm utc;
    gmtime_r(&unixTime, &utc);
    rec.year = utc.tm_year + 1900;
    rec.month = utc.tm_mon + 1;
    rec.day = utc.tm_mday;
    rec.hour = utc.tm_hour;
    rec.minute = utc.tm_min;
    rec.second = utc.tm_sec;
    rec.nanosecond = nanosecond;
    rec.weekday = utc.tm_wday;
    rec.yearday = utc.tm_yday;
}

Blizzard::Time::Timestamp Blizzard::Time::MakeTime(const TimeRec& rec) {
    tm utc = {};
    utc.tm_year = rec.year - 1900;
    utc.tm_mon = rec.month - 1;
    utc.tm_mday = rec.day;
    utc.tm_hour = rec.hour;
    utc.tm_min = rec.minute;
    utc.tm_sec = rec.second;
    int64_t seconds = static_cast<int64_t>(timegm(&utc)) - UNIX_SECONDS_AT_2000;
    return static_cast<Timestamp>(seconds * NANOSECONDS + rec.nanosecond);
}

Blizzard::Time::Timestamp Blizzard::Time::GetTimestamp() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return static_cast<Timestamp>((static_cast<int64_t>(now.tv_sec) - UNIX_SECONDS_AT_2000) * NANOSECONDS + now.tv_nsec);
}

int64_t Blizzard::Time::Milliseconds() {
    return static_cast<int64_t>(emscripten_get_now());
}

#endif
