cmake_minimum_required(VERSION 3.0)

project(MoqiTextService)

# http://www.utf8everywhere.org/
add_definitions(/D_UNICODE=1 /DUNICODE=1)
set(CMAKE_CXX_STANDARD 20)

# generate the resource file containing version info
configure_file("MoqiTextService.rc.in" "MoqiTextService.rc" @ONLY)

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${MOQI_GENERATED_PROTO_ROOT}
    ${MOQI_PROTOBUF_INCLUDE_DIR}
    ${MOQI_PROTOBUF_ABSL_INCLUDE_DIR}
    ${jsoncpp_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(MoqiTextService SHARED
    ${MOQI_PROTO_CPP}
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

set_source_files_properties(${MOQI_PROTO_CPP} ${MOQI_PROTO_HDR} PROPERTIES GENERATED TRUE)
add_dependencies(MoqiTextService moqi_proto_cpp_gen)

target_link_libraries(MoqiTextService
    libIME2_static
    ${MOQI_PROTOBUF_TARGET}
    jsoncpp_lib_static
)
