// wow335modding: the browser build has no movie decoder yet (Windows uses DivxDecoder.dll or Tools/XvidDecoder), so
// cinematics fail to open and are skipped. ponytail: compile Tools/XvidDecoder for the web to get movies back.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include "ThirdParty/DivxDecoder/DivxDecoder.h"

int32_t InitializeDivxDecoder(int32_t, int32_t, int32_t) {
    return 0;
}

int32_t SetOutputFormat(int32_t, int32_t, int32_t, int32_t) {
    return 0;
}

int32_t DivxDecode(int32_t, DivxDecodeFrame*, int32_t) {
    return 0;
}

int32_t UnInitializeDivxDecoder(int32_t) {
    return 0;
}

#endif
