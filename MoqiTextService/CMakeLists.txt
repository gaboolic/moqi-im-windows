cmake_minimum_required(VERSION 3.0)

project(MoqiTextService)

# http://www.utf8everywhere.org/
add_definitions(/D_UNICODE=1 /DUNICODE=1)
set(CMAKE_CXX_STANDARD 14)

# generate the resource file containing version info
configure_file("MoqiTextService.rc.in" "MoqiTextService.rc" @ONLY)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${MOQI_PROTOBUF_INCLUDE_DIR}
    ${MOQI_PROTOBUF_ABSL_INCLUDE_DIR}
    ${jsoncpp_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(MoqiTextService SHARED
    ${CMAKE_SOURCE_DIR}/proto/moqi.pb.cc
    # core IME implementation
    MoqiImeModule.cpp
    MoqiImeModule.h
    MoqiTextService.cpp
    MoqiTextService.def
    MoqiTextService.h
    MoqiCandidateWindow.cpp
    MoqiCandidateWindow.h
    MoqiClient.cpp
    MoqiClient.h
    MoqiLangBarButton.cpp
    MoqiLangBarButton.h
    DllEntry.cpp
    # resources
    ${CMAKE_CURRENT_BINARY_DIR}/MoqiTextService.rc
)

target_link_libraries(MoqiTextService
    libIME2_static
    ${MOQI_PROTOBUF_TARGET}
    jsoncpp_lib_static
)
