cmake_minimum_required(VERSION 3.20)
project(naim_voice_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/whisper.cpp)

add_executable(naim-voice-moduled src/main.cpp)
target_include_directories(naim-voice-moduled PRIVATE
  ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/include
  ${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/ggml/include
  ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib
)
target_link_libraries(naim-voice-moduled PRIVATE whisper Threads::Threads)
