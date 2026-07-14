cmake_minimum_required(VERSION 3.16)
project(whisper-echo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2 (needed by common-sdl)
find_package(SDL2 REQUIRED)

# GPU backend — set exactly one of these to ON (requires system libraries):
#   GGML_CUDA    — NVIDIA GPU (needs CUDA toolkit)
#   GGML_VULKAN  — AMD/Intel/NVIDIA GPU (needs Vulkan SDK)
#   GGML_METAL   — Apple Silicon/Metal (macOS only)
#   GGML_HIP     — AMD ROCm GPU (needs ROCm/HIP SDK)
set(GGML_VULKAN ON CACHE BOOL "" FORCE)
set(GGML_CUDA   OFF CACHE BOOL "" FORCE)
set(GGML_METAL  OFF CACHE BOOL "" FORCE)
set(GGML_HIP    OFF CACHE BOOL "" FORCE)

# Add whisper.cpp as a subdirectory
add_subdirectory(vendor/whisper.cpp)

# whisper-stream executable
set(TARGET whisper-echo)
add_executable(${TARGET}
    src/stream.cpp
    src/audio_utils.cpp
    src/audio_capture.cpp
    src/commands.cpp
    src/uinput.cpp
    )

target_include_directories(${TARGET} PRIVATE
    src
    vendor/whisper.cpp/include
    vendor/whisper.cpp/examples
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(${TARGET} PRIVATE whisper ${SDL2_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
