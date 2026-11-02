// wow335modding: browser build of Hardware/Win64/Hardware.cpp. No CPUID in WebAssembly: report SSE and SSE2, which
// the build provides through WebAssembly SIMD, and the browser's logical core count.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <cstdint>
#include <emscripten/threading.h>
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

Blizzard::Hardware::ProcessorFeatures System_Hardware::s_processorFeatures;
bool System_Hardware::s_initialized;
Blizzard::Lock::Atomic System_Hardware::s_initCount;

void System_Hardware::InitProcessorFeatures() {
    int cores = emscripten_num_logical_cores();
    s_processorFeatures.m_vendor = 0;
    s_processorFeatures.m_features = 0x4 | 0x10 | (cores > 1 ? 0x40 : 0); // SSE, SSE2, multi-core
    s_processorFeatures.m_coreCount = cores > 0 ? cores : 1;
    s_processorFeatures.m_processorCount = s_processorFeatures.m_coreCount;
}

uint32_t Blizzard::Hardware::GetProcessorFeatures() {
    if (!System_Hardware::s_initialized) {
        System_Hardware::InitProcessorFeatures();
        System_Hardware::s_initialized = true;
    }

    return System_Hardware::s_processorFeatures.m_features;
}

#endif
