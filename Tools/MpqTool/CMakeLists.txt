# Standalone helper, not part of the client build:
#   cmake -S Tools/MpqTool -B build/MpqTool -A x64
#   cmake --build build/MpqTool --config Release
cmake_minimum_required(VERSION 3.14)
project(MpqTool C CXX)

include(FetchContent)
FetchContent_Declare(StormLib
    GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
    GIT_TAG 2aa6216c65ce68b88c46c0e4f12a38dc94c36b7a) # v9.31
FetchContent_MakeAvailable(StormLib)

add_executable(MpqTool main.cpp)
target_compile_features(MpqTool PRIVATE cxx_std_17)
target_link_libraries(MpqTool PRIVATE storm)
# CMake links storm itself; stop StormLib.h from #pragma-linking a prebuilt StormLibRAD.lib
target_compile_definitions(MpqTool PRIVATE __STORMLIB_NO_STATIC_LINK__)

add_executable(MapGen mapgen.cpp)
target_compile_features(MapGen PRIVATE cxx_std_17)
