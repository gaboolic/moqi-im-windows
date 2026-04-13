cmake_minimum_required(VERSION 3.21)
# Satisfy older FetchContent deps (e.g. jsoncpp) under CMake 4.x; avoids fragile -D on command line.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Explicitly control the MSVC runtime before project() so generated VS projects
# keep the legacy PIME behavior: /MDd for Debug, /MD for RelWithDebInfo,
# and /MT for Release/MinSizeRel.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:DLL>")

# Override default c/c++ flags to keep MSVC release builds self-contained.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/c_flag_overrides.cmake")
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cxx_flag_overrides.cmake")

project(MoqiIMWindows LANGUAGES C CXX)

include(FetchContent)
set(MOQI_PROTOBUF_SOURCE_DIR "" CACHE PATH "Optional local protobuf source tree")
set(MOQI_PROTOBUF_VENDORED_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/protobuf")

# Load semantic version from version.txt.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" MOQI_VERSION)
string(REGEX MATCHALL "[0-9]+" MOQI_VERSION_PARTS "${MOQI_VERSION}")
list(GET MOQI_VERSION_PARTS 0 MOQI_VERSION_MAJOR)
list(GET MOQI_VERSION_PARTS 1 MOQI_VERSION_MINOR)
list(GET MOQI_VERSION_PARTS 2 MOQI_VERSION_PATCH)

add_definitions(
    /D_UNICODE=1
    /DUNICODE=1
    /D_CRT_SECURE_NO_WARNINGS
    /GR-
)

add_definitions(
    -DWIN32_LEAN_AND_MEAN
    -D_WIN32_WINNT=0x0602
)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)

set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_STRICT_ISO OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_WARNINGS OFF CACHE BOOL "" FORCE)

set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOBUF_BINARIES ON CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.2.1
)

FetchContent_MakeAvailable(spdlog)

if(NOT MOQI_PROTOBUF_SOURCE_DIR AND EXISTS "${MOQI_PROTOBUF_VENDORED_SOURCE_DIR}/CMakeLists.txt")
    set(MOQI_PROTOBUF_SOURCE_DIR "${MOQI_PROTOBUF_VENDORED_SOURCE_DIR}")
endif()

if(MOQI_PROTOBUF_SOURCE_DIR)
    add_subdirectory("${MOQI_PROTOBUF_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/protobuf")
    set(MOQI_PROTOBUF_INCLUDE_DIR "${MOQI_PROTOBUF_SOURCE_DIR}/src")
    set(MOQI_PROTOBUF_ABSL_INCLUDE_DIR "${MOQI_PROTOBUF_SOURCE_DIR}/third_party/abseil-cpp")
else()
    FetchContent_Declare(
        protobuf
        URL https://github.com/protocolbuffers/protobuf/releases/download/v29.5/protobuf-29.5.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(protobuf)
    set(MOQI_PROTOBUF_INCLUDE_DIR "${protobuf_SOURCE_DIR}/src")
    set(MOQI_PROTOBUF_ABSL_INCLUDE_DIR "${protobuf_SOURCE_DIR}/third_party/abseil-cpp")
endif()

if(TARGET protobuf::libprotobuf)
    set(MOQI_PROTOBUF_TARGET protobuf::libprotobuf)
else()
    set(MOQI_PROTOBUF_TARGET libprotobuf)
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/jsoncpp")

add_subdirectory("${PROJECT_SOURCE_DIR}/libIME2")
add_subdirectory("${PROJECT_SOURCE_DIR}/libuv")
add_subdirectory("${PROJECT_SOURCE_DIR}/MoqiTextService")
add_subdirectory("${PROJECT_SOURCE_DIR}/Preview")

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory("${PROJECT_SOURCE_DIR}/MoqLauncher")
endif()
