project(SetupHelper)

add_executable(SetupHelper WIN32
    SetupHelper.cpp
)

target_include_directories(SetupHelper PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(SetupHelper
    Advapi32
    Shell32
)
