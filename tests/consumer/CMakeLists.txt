cmake_minimum_required(VERSION 3.25)
project(ComposiaConsumer LANGUAGES CXX)
# Composia is built with the static CRT; the consumer must use the same runtime library.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Composia 0.3 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE Composia::UI)
