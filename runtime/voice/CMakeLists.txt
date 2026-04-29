cmake_minimum_required(VERSION 3.22)

project(
  naim-voice-module-runtime
  LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
include(FetchContent)

set(NAIM_WHISPER_CPP_GIT_TAG "v1.8.1" CACHE STRING
    "Pinned whisper.cpp release used for the NAIM voice-module runtime")
set(NAIM_CPP_HTTPLIB_GIT_TAG "v0.26.0" CACHE STRING
    "Pinned cpp-httplib release used for the NAIM voice-module runtime")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static whisper.cpp for the NAIM voice-module runtime" FORCE)
set(GGML_CUDA OFF CACHE BOOL "Build whisper.cpp without CUDA for the NAIM voice-module runtime" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  whisper_cpp
  GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
  GIT_TAG ${NAIM_WHISPER_CPP_GIT_TAG}
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  cpp_httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG ${NAIM_CPP_HTTPLIB_GIT_TAG}
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(whisper_cpp cpp_httplib)

add_executable(
  naim-voice-moduled
  src/main.cpp
)
target_include_directories(
  naim-voice-moduled
  PRIVATE
    "${whisper_cpp_SOURCE_DIR}/include"
    "${whisper_cpp_SOURCE_DIR}/ggml/include"
    "${cpp_httplib_SOURCE_DIR}"
)
target_link_libraries(
  naim-voice-moduled
  PRIVATE
    whisper
    Threads::Threads
)
if(MSVC)
  target_compile_options(naim-voice-moduled PRIVATE /W4)
else()
  target_compile_options(naim-voice-moduled PRIVATE -Wall -Wextra -Wpedantic)
endif()
