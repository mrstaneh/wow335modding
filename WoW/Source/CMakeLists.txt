file(GLOB PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (WOWDECOMP_PLATFORM_WEB)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Web/*.cpp")
elseif (WOWDECOMP_ARCH_64)
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win64/*.cpp")
else ()
    file(GLOB PLATFORM_SOURCES CONFIGURE_DEPENDS "Win32/*.cpp")
endif ()

add_executable(Wow WIN32 ${PRIVATE_SOURCES} ${PLATFORM_SOURCES})

if (WOWDECOMP_PLATFORM_WEB)
    set(WOW_OUTPUT_NAME WowWeb)
elseif (WOWDECOMP_ARCH_64)
    set(WOW_OUTPUT_NAME Wow64)
else ()
    set(WOW_OUTPUT_NAME Wow32)
endif ()

set_target_properties(Wow PROPERTIES
    OUTPUT_NAME ${WOW_OUTPUT_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(Wow PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
endforeach ()

target_include_directories(Wow
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

if (WOWDECOMP_PLATFORM_WEB)
    # main() on a worker thread, so the client's blocking main loop works (Web/WebMain.cpp); it renders into its own
    # OffscreenCanvas and posts frames to the page (Engine/Source/Gx/CGxDeviceWebGl/WebGl.js)
    target_link_options(Wow PRIVATE "-pthread" "-sPROXY_TO_PTHREAD" "-sPTHREAD_POOL_SIZE=24"
        "-sALLOW_MEMORY_GROWTH=1" "-sMAXIMUM_MEMORY=4GB" "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2" "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$stringToNewUTF8"
        "--js-library=${CMAKE_SOURCE_DIR}/Engine/Source/Gx/CGxDeviceWebGl/WebGl.js" "-Wl,--error-limit=0" "-sSTACK_SIZE=4MB" "-sDEFAULT_PTHREAD_STACK_SIZE=1MB" "--profiling-funcs" "-sASSERTIONS=1" "-sSTACK_OVERFLOW_CHECK=2")
else ()
    target_link_options(Wow PRIVATE "/INCREMENTAL:NO" "/ENTRY:start")
endif ()

target_link_libraries(Wow
    PRIVATE
        BLPFile
        Base
        BlizzardCore
        ComSat
        ConsoleBase
        Event
        FFX
        Frame
        FrameScript
        lua
        FrameXML
        Gx
        Gxu
        Images
        Model2
        Os
        Scrn
        Services
        ShaderEffect
        Tempest
        SoundEngine
        SteelseriesMouse
        Storm
        WoWCommon
        Battlenet
        Console
        ConsoleDevice
        DB
        Game
        Glue
        LiquidSystem
        Magic
        Minigame
        Net
        Object
        ObjectAlloc
        ObjectMgrClient
        SoundInterface
        UIUtil
        Ui
        WorldClient
        WowServices
        WowSvcs
)
