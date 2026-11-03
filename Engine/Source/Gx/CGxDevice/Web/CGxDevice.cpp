// wow335modding: browser build of Win64/CGxDevice.cpp. There is one adapter, the page's WebGL2, and one "monitor", the
// canvas; every API the client asks for (gxApi d3d9, d3d9ex or opengl) gets the WebGL device.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <emscripten/html5.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceWebGl/CGxDeviceWebGl.h"

CGxDevice* CreateCGxDeviceD3d() {
    return CreateCGxDeviceWebGl();
}

CGxDevice* CreateCGxDeviceD3d9Ex() {
    return CreateCGxDeviceWebGl();
}

CGxDevice* CreateCGxDeviceOpenGl() {
    return CreateCGxDeviceWebGl();
}

int32_t CGxDevice::AdapterFormats(EGxApi, TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);

    CGxFormat format;
    format.m_colorFormat = CGxFormat::Fmt_Argb8888;
    format.m_depthFormat = CGxFormat::Fmt_Ds248;
    format.m_multisampleCount = 1;
    *formats.New() = format;

    return 1;
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    int width = 0;
    int height = 0;
    emscripten_get_canvas_element_size("#canvas", &width, &height);
    mode.m_size.x = width > 0 ? width : 1024;
    mode.m_size.y = height > 0 ? height : 768;
    mode.m_bpp = 32;
    mode.m_refreshRate = WebDisplayRefreshRate();
    return 1;
}

// The canvas size at each rate the video options offer; the chosen one caps the frame rate (CGxDeviceWebGl).
int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    CGxMonitorMode desktop;
    CGxDevice::AdapterDesktopMode(desktop);

    uint32_t rates[16];
    uint32_t count = WebRefreshRates(desktop.m_refreshRate, rates);
    modes.SetCount(0);

    for (uint32_t i = 0; i < count; i++) {
        CGxMonitorMode* mode = modes.New();
        *mode = desktop;
        mode->m_refreshRate = rates[i];
    }

    return 1;
}

int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    vendorID = 0xFFFF;
    deviceID = 0xFFFF;
    driverVersionHi = 0;
    driverVersionLo = 0;
    return 0;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    deviceID = 1; // hardware transform and two or more texture stages, no pixel shaders: what the device reports
    return 1;
}

#endif
