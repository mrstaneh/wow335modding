// wow335modding: browser build of Win64/PatchDownloadGlue.cpp. A web page cannot run a downloaded patcher.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "WoW/Source/Glue/CGlueMgr.h"

bool CGlueMgr::PatchDownloadExecuteCommand(const char*, const char*) {
    return false;
}

#endif
