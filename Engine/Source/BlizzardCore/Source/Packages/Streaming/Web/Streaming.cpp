// wow335modding: browser build of Streaming/Win64/Streaming.cpp. The browser build does not stream game data (that
// was the trial client's on-demand download): every file counts as fully local, nothing is ever queued.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Streaming.h"

bool Blizzard::Streaming::InitializeStreaming(const char*, const char*, const char*, const char*, bool) {
    return false;
}

void StopStreaming() {
}

bool Blizzard::Streaming::IsStreamedFile(const char*) {
    return false;
}

int32_t Blizzard::Streaming::CheckFileRange(const char*, int64_t, int32_t, const char*, int32_t) {
    return 1; // all of it is local
}

int32_t Blizzard::Streaming::GetIsLocalAmount(const char*, int64_t, int32_t size, const char*) {
    return size;
}

bool Blizzard::Streaming::MakeLocalAsync(Blizzard::File::StreamRecord*, int64_t, int32_t, int32_t, const char*) {
    return true;
}

bool Blizzard::Streaming::MakeLocalBlocking(Blizzard::File::StreamRecord*, int64_t, int32_t, int32_t, const char*) {
    return true;
}

bool Blizzard::Streaming::QueueCompletion(Completion*, int32_t) {
    return false;
}

bool Blizzard::Streaming::RemoveCompletion(Completion*, int32_t) {
    return false;
}

bool Blizzard::Streaming::CancelQueue(int32_t) {
    return true;
}

char* GetComponentVersionData(uint32_t* size) {
    if (size) {
        *size = 0;
    }
    return nullptr;
}

#endif
