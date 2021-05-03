cmake_minimum_required(VERSION 3.16)

project(metronome-audio-to-midi)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(PkgConfig REQUIRED)
find_package(Curses REQUIRED)
find_package(Jack REQUIRED)

include_directories(${CURSES_INCLUDE_DIR} ${JACK_INCLUDE_DIR})

add_executable(metronome-audio-to-midi metronome-audio-to-midi.c)

target_link_libraries(metronome-audio-to-midi ${CURSES_LIBRARIES} ${JACK_LIBRARIES} m)

### Install

install(TARGETS metronome-audio-to-midi)

### Testing (not used)

#enable_testing()
#add_test(NAME main COMMAND main)
