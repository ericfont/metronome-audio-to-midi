cmake_minimum_required(VERSION 3.16)

project(metronome-audio-to-midi)

add_executable(metronome-audio-to-midi metronome-audio-to-midi.c)

install(TARGETS metronome-audio-to-midi)

#enable_testing()
#add_test(NAME main COMMAND main)
