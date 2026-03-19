cmake_minimum_required(VERSION 3.13)
project(Clipper2)

add_library(Clipper2 STATIC
    src/clipper.engine.cpp
    src/clipper.offset.cpp
    src/clipper.rectclip.cpp
)

target_include_directories(Clipper2 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(Clipper2 PUBLIC cxx_std_17)
