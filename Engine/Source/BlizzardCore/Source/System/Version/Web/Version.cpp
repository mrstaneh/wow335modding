// wow335modding: browser build of Version/Win64/Version.cpp. There are no executables with version resources here;
// everything reports the client's own version, 3.3.5.12340.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/BlizzardCore/Source/System/Version/Version.h"

bool Blizzard::Version::GetFileVersion(const char*, FileVersion& version) {
    version.m_major = 3;
    version.m_minor = 3;
    version.m_build = 5;
    version.m_revision = 12340;
    return true;
}

#endif
