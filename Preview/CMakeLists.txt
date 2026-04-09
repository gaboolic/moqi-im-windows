cmake_minimum_required(VERSION 3.0)

project(MoqiCandidatePreview)

add_executable(MoqiCandidatePreview WIN32
    main.cpp
)

target_link_libraries(MoqiCandidatePreview
    gdi32
    user32
)
