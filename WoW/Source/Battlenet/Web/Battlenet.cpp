// wow335modding: browser build of Win64/Battlenet.cpp. There is no Battle.net module DLL to load in a browser; the
// 3.3.5 realm login (grunt) does not need it.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "WoW/Source/Battlenet/Battlenet.h"

Battlenet::Library* Battlenet::Library::Load(const char*, Allocator&) {
    return nullptr;
}

#endif
