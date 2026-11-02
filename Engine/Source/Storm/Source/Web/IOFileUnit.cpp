// wow335modding: browser build of Win64/IOFileUnit.cpp. Only the patch downloader (WowSvcs FileDownload.cpp) writes
// files through Storm's IO units, and a page downloads no patches, so opening a file unit fails.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/Storm/Source/IOUnit/IOFileUnit.h"

namespace Storm {

IOUnit::IOUnit() {
    this->m_refcount = nullptr;
    this->m_count = 1;
}

IOUnit::~IOUnit() {
}

void IOUnit::Release() {
    IORefcount* refcount = this->m_refcount;
    if (--this->m_count == 0) {
        delete this;
    }
    if (refcount) {
        refcount->Release();
    }
}

IOUnit* IOFileUnit::Create(const IOFileParam&, IOUnit*) {
    return nullptr;
}

}

#endif
