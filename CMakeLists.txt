# Midnight Club 3: DUB Edition Remix (original Xbox), statically recompiled, with online co-op

cmake_minimum_required(VERSION 3.25)
project(mc3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here, two folders up inside the SDK's templates, a checkout beside this repository, else downloaded
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../XboxXBE-Recomp)
else()
    include(FetchContent)
    FetchContent_Declare(xbe_sdk GIT_REPOSITORY https://github.com/TGP482/XboxXBE-Recomp.git GIT_TAG main)
    FetchContent_MakeAvailable(xbe_sdk)
    set(SDK ${xbe_sdk_SOURCE_DIR})
endif()
include(${SDK}/cmake/XbeRecomp.cmake)
# src/audio/xaudio2.cpp replaces the SDK one (adds --mute)
list(FILTER XBE_RUNTIME_SOURCES EXCLUDE REGEX "/audio/xaudio2\.cpp$")

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE
add_xbe_image(MC3 default.xbe game.toml ${GAME_PARTITIONS} game)

# build_info.h (MC3_COMMIT) for the crash log, rewritten only when the commit changes
find_package(Git QUIET)
set(BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/build_info.h)
add_custom_target(build_info
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DOUT=${BUILD_INFO} -DGIT=${GIT_EXECUTABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_info.cmake
    BYPRODUCTS ${BUILD_INFO}
    VERBATIM)
set_source_files_properties(src/game/diagnostics.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}"
    OBJECT_DEPENDS "${BUILD_INFO}")
add_dependencies(MC3 build_info)

# EOS (online rooms) is opt-in: the official C SDK from an Epic developer account, kept outside version
# control like the product credentials (coop-online.toml beside the exe). C:/dev/eos-sdk is used when present.
set(MC3_EOS_SDK "" CACHE PATH "Extracted Epic Online Services C SDK directory")
if(NOT MC3_EOS_SDK AND EXISTS "C:/dev/eos-sdk/SDK/Include/eos_sdk.h")
    set(MC3_EOS_SDK "C:/dev/eos-sdk")
endif()
if(MC3_EOS_SDK)
    find_path(MC3_EOS_INCLUDE eos_sdk.h PATHS "${MC3_EOS_SDK}/SDK/Include" "${MC3_EOS_SDK}/Include" NO_DEFAULT_PATH REQUIRED)
    find_library(MC3_EOS_LIBRARY NAMES EOSSDK-Win64-Shipping PATHS "${MC3_EOS_SDK}/SDK/Lib" "${MC3_EOS_SDK}/Lib" NO_DEFAULT_PATH REQUIRED)
    find_file(MC3_EOS_DLL EOSSDK-Win64-Shipping.dll PATHS "${MC3_EOS_SDK}/SDK/Bin" "${MC3_EOS_SDK}/Bin" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(MC3 PRIVATE "${MC3_EOS_INCLUDE}")
    target_link_libraries(MC3 PRIVATE "${MC3_EOS_LIBRARY}")
    target_compile_definitions(MC3 PRIVATE MC3_WITH_EOS=1)
    add_custom_command(TARGET MC3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${MC3_EOS_DLL}" "$<TARGET_FILE_DIR:MC3>/EOSSDK-Win64-Shipping.dll")
endif()
