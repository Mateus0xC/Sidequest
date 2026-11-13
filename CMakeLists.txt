cmake_minimum_required(VERSION 3.24)
project(Sidequest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING)
add_compile_options(/utf-8 /W3 /permissive- /EHsc)

file(GLOB CORE_SRC CONFIGURE_DEPENDS src/core/*.cpp)
add_library(sq_core STATIC ${CORE_SRC})
target_include_directories(sq_core PUBLIC src third_party)

file(GLOB APP_SRC CONFIGURE_DEPENDS src/app/*.cpp src/platform/*.cpp src/ui/*.cpp)
add_executable(sidequest WIN32 src/main.cpp ${APP_SRC})
target_link_libraries(sidequest PRIVATE sq_core d3d11 dxgi d2d1 dwrite dcomp dwmapi user32 shell32 gdi32 ole32 uuid shcore)
target_link_options(sidequest PRIVATE /MANIFEST:EMBED)

add_executable(sq_tests tests/test_core.cpp)
target_link_libraries(sq_tests PRIVATE sq_core)
