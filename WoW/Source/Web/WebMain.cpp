// wow335modding: entry point of the browser build (Win64/WinMain.cpp on Windows).
// It is linked with -sPROXY_TO_PTHREAD: main() runs on a worker thread and draws through an OffscreenCanvas, so the
// client's own blocking main loop (CommonMain) runs as it does on Windows instead of being cut into browser frames.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

int main() {
    StormRtlInitialize();
    return CommonMain();
}

#endif
