metronome-audio-to-midi: metronome-audio-to-midi.c
	gcc metronome-audio-to-midi.c -o metronome-audio-to-midi -lcurses -lm -ljack

clean:
	rm -f metronome-audio-to-midi \
	      metronome-audio-to-midi.exe
