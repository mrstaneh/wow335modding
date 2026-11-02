// wow335modding: browser build of Win64/BattlenetLogin.cpp. Battle.net login is not available in the browser build
// (see Battlenet/Web/Battlenet.cpp), so there is nothing to resolve.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "WoW/Source/WowServices/BattlenetLogin.h"

uint32_t BattlenetApplication::Resolve(uint32_t, const char*) {
    return 0;
}

#endif
