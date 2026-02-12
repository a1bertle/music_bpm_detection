cmake_minimum_required(VERSION 3.16)
project(music_bpm_detection LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(MINIMP3_DIR ${THIRD_PARTY_DIR}/minimp3)
set(POCKETFFT_DIR ${THIRD_PARTY_DIR}/pocketfft)

if (NOT EXISTS ${MINIMP3_DIR}/minimp3_ex.h)
  message(FATAL_ERROR "Missing minimp3 in ${MINIMP3_DIR}. Expected minimp3_ex.h.")
endif()
if (NOT EXISTS ${POCKETFFT_DIR}/pocketfft.h)
  message(FATAL_ERROR "Missing pocketfft in ${POCKETFFT_DIR}. Expected pocketfft.h.")
endif()

add_library(minimp3_headers INTERFACE)
target_include_directories(minimp3_headers INTERFACE ${MINIMP3_DIR})

add_library(pocketfft_headers INTERFACE)
target_include_directories(pocketfft_headers INTERFACE ${POCKETFFT_DIR})

add_library(bpm STATIC
  src/audio_buffer.cpp
  src/mp3_decoder.cpp
  src/mp4_decoder.cpp
  src/youtube_decoder.cpp
  src/wav_reader.cpp
  src/onset_detector.cpp
  src/tempo_estimator.cpp
  src/beat_tracker.cpp
  src/metronome.cpp
  src/wav_writer.cpp
  src/pipeline.cpp
  ${POCKETFFT_DIR}/pocketfft.c
)

target_include_directories(bpm PUBLIC include)
target_link_libraries(bpm PUBLIC minimp3_headers pocketfft_headers)

target_compile_options(bpm PRIVATE -Wall -Wextra -Wpedantic)

add_executable(bpm_detect src/main.cpp)
target_link_libraries(bpm_detect PRIVATE bpm)
