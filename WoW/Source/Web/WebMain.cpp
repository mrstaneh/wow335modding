// wow335modding: entry point of the browser build (Win64/WinMain.cpp on Windows).
// It is linked with -sPROXY_TO_PTHREAD: main() runs on a worker thread, so the client's own blocking main loop
// (CommonMain) runs as it does on Windows instead of being cut into browser frames.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"

int main() {
    // The install folder (Process/Web/Process.cpp): Data comes over HTTP (System/File/Web/WebFile.h), the folders
    // the client writes to live in memory for the session.
    // ponytail: WTF and Cache are lost on reload; mount IDBFS there to keep settings.
    for (const char* directory : { "/wow", "/wow/WTF", "/wow/Cache", "/wow/Logs", "/wow/Screenshots" }) {
        mkdir(directory, 0777);
    }

    chdir("/wow"); // the client's data paths are relative ("Data\\common.MPQ")

    // A first-run Config.wtf: no intro movie (there is no video decoder here), the agreements already seen.
    if (FILE* config = fopen("/wow/WTF/Config.wtf", "wx")) {
        fputs("SET locale \"enUS\"\nSET accounttype \"LK\"\nSET gxResolution \"1280x800\"\nSET gxWindow \"1\"\n"
              "SET movie \"0\"\nSET readTOS \"1\"\nSET readEULA \"1\"\nSET readTerminationWithoutNotice \"1\"\n", config);
        fclose(config);
    }

    StormRtlInitialize();
    return CommonMain();
}

#endif
