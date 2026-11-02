// wow335modding: browser build of URL/Win64/URL.cpp (WinINet downloads). Only the streaming tracker and patch
// manifests use it, and the browser build streams nothing, so downloads report failure.
// ponytail: implement with emscripten_fetch if a feature needs HTTP.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"

bool Blizzard::URL::DownloadURL(const char*, DownloadCallback*) {
    return false;
}

#endif
