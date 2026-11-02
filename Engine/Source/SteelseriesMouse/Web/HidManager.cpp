// wow335modding: browser build of Win64/HidManagerImpl.cpp. Browsers give no raw HID access to the mouse, so the
// Steelseries WoW mouse support finds no devices.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include "Engine/Source/SteelseriesMouse/HidManager.h"

namespace {
class NoHidManager : public HidManager {
    public:
    bool EnumerateDevices() override { return false; }
    void SetWindow(void*) override {}
    uint32_t GetDeviceCount() override { return 0; }
    bool WriteWait(const void*, uint32_t) override { return false; }
    bool Write(const void*, uint32_t) override { return false; }
    bool SetFeature(void*, uint32_t) override { return false; }
};
}

HidManager::~HidManager() {
}

HidManager* HidManager::Create() {
    return new NoHidManager;
}

#endif
