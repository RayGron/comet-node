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
option(NAIM_ENABLE_CUDA "Build whisper.cpp with CUDA backend for the NAIM voice-module runtime" OFF)
option(NAIM_CUDA_NATIVE "Prefer native CUDA architectures for ggml-cuda builds" OFF)
set(NAIM_CUDA_ARCHITECTURES "" CACHE STRING
    "Explicit CUDA architectures for ggml-cuda builds; empty lets whisper.cpp choose defaults")
set(NAIM_CUDA_NVCC_JOBS "1" CACHE STRING
    "Maximum number of concurrent nvcc compile jobs for ggml-cuda when using Ninja")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static whisper.cpp for the NAIM voice-module runtime" FORCE)
if(NAIM_ENABLE_CUDA)
  if(NAIM_CUDA_ARCHITECTURES)
    set(NAIM_CUDA_NATIVE OFF CACHE BOOL "" FORCE)
    set(CMAKE_CUDA_ARCHITECTURES
        "${NAIM_CUDA_ARCHITECTURES}"
        CACHE STRING "Explicit CUDA architectures for voice-module ggml-cuda builds" FORCE)
  endif()
  set(GGML_NATIVE ${NAIM_CUDA_NATIVE} CACHE BOOL "" FORCE)
  set(GGML_CUDA ON CACHE BOOL "Build whisper.cpp with CUDA for the NAIM voice-module runtime" FORCE)
  message(STATUS "Naim voice-module CUDA runtime: enabled")
  message(STATUS "Naim voice-module CUDA native architectures: ${NAIM_CUDA_NATIVE}")
  message(STATUS "Naim voice-module CUDA explicit architectures: ${NAIM_CUDA_ARCHITECTURES}")
else()
  set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  set(GGML_CUDA OFF CACHE BOOL "Build whisper.cpp without CUDA for the NAIM voice-module runtime" FORCE)
  message(STATUS "Naim voice-module CUDA runtime: disabled")
endif()
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

if(NAIM_ENABLE_CUDA AND TARGET ggml-cuda AND CMAKE_GENERATOR STREQUAL "Ninja")
  if(NAIM_CUDA_NVCC_JOBS MATCHES "^[1-9][0-9]*$")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS "naim_voice_cuda_pool=${NAIM_CUDA_NVCC_JOBS}")
    set_property(TARGET ggml-cuda PROPERTY JOB_POOL_COMPILE naim_voice_cuda_pool)
    message(STATUS "Using voice-module ggml-cuda compile pool size: ${NAIM_CUDA_NVCC_JOBS}")
  else()
    message(WARNING
      "NAIM_CUDA_NVCC_JOBS='${NAIM_CUDA_NVCC_JOBS}' is not a positive integer; "
      "voice-module ggml-cuda will use the default global compile pool")
  endif()
endif()

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
