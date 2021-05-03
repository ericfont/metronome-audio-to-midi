/** @file metronome-audio-to-midi.c
 *
 * @brief Jack client that converts the inputted metronome audio stream and converts it to a midi clock output.
 * by Eric Fontaine (CC0 2020).
 * used jackaudio example program simple_client.c as starting point
 */

#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <math.h>

#include <jack/jack.h>
#include <jack/midiport.h>

jack_port_t *input_audio_port;
jack_port_t *output_audio_port;
jack_port_t *output_midi_port;
jack_client_t *client;

int maxRows, maxCols; // screen dimensions

#define linear_from_dB(dB) powf(10.0f, 0.05f * (dB))
#define dB_from_linear(linear) (20.0f * log10f(linear))

float risingThreshold_dB;
float risingThreshold;

float fallingThreshold_dB;
float fallingThreshold;

float lowMinTime_ms;
jack_nframes_t lowMinTime_frames;

bool detectedBeat = false;
int nDetectedBeats = 0;

float beatMaxAmplitude = 0.0f;

jack_nframes_t currBeatStart = 0;
jack_nframes_t currBeatEnd = 0;

jack_nframes_t lastBeatStart = 0;
jack_nframes_t lastBeatEnd = 0;

jack_nframes_t earliestNextBeatStart = 0;

jack_nframes_t framesPerClockTick = 0;
jack_nframes_t nextClockTick = 0;

#define ms_to_frames(x) (((float) (sample_rate)) * ((float) (x)) / 1000.0f)

/*
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int process (jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in, *out	;
	
	in = jack_port_get_buffer (input_audio_port, nframes);
	out = jack_port_get_buffer (output_audio_port, nframes);

	jack_nframes_t jack_callback_start_frame = jack_last_frame_time(client);

	void* midi_out_buffer = jack_port_get_buffer(output_midi_port, nframes);
	jack_midi_clear_buffer(midi_out_buffer); // this should be called at beginning of each process cycle

	unsigned char jbuffer[4];
	jbuffer[0] = 0xF8;
	jbuffer[1] = 0;
	jbuffer[2] = 0;
	jbuffer[3] = 0;

	int i;

	for (i = 0; i < nframes; i++) {

		float absoluteInput = fabs(in[i]);

		jack_nframes_t currFrame = jack_callback_start_frame + i;

		if (!detectedBeat && currFrame > earliestNextBeatStart && absoluteInput > risingThreshold) {
			detectedBeat = true;
			nDetectedBeats++;
			beatMaxAmplitude = absoluteInput;
			lastBeatStart = currBeatStart;
			currBeatStart = currFrame;

			if (nDetectedBeats > 1) {
				framesPerClockTick = (currBeatStart - lastBeatStart) / 24;
				nextClockTick = currFrame + framesPerClockTick;
			}
		}
		else if (detectedBeat && absoluteInput < fallingThreshold) {
			detectedBeat = false;
			lastBeatEnd = currBeatEnd;
			currBeatEnd = jack_callback_start_frame + i;
			earliestNextBeatStart = lowMinTime_frames + currFrame;
		}

		out[i] = absoluteInput;

		if (currFrame == nextClockTick && nDetectedBeats > 4) {
			jack_midi_event_write(midi_out_buffer, 0, jbuffer, 3);
			nextClockTick = currFrame + framesPerClockTick;
		}
	}

	return 0;      
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */

void jack_shutdown (void *arg)
{
	exit (1);
}


void printbar( float amplitude, int columnsavailable)
{
		int nfullchars = (columnsavailable > 0) ? amplitude * (float) columnsavailable : 0;

		int i;
		for ( i=0; i < nfullchars; i++)
			addch(ACS_CKBOARD);
}

int main (int argc, char *argv[])
{
	const char **ports;
	const char *client_name = "metronome-audio-to-midi";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;

	// ncurses setup
	initscr(); // ncurses init terminal
	cbreak; // only input one character at a time
	noecho(); // disable echoing of typed keyboard input
	keypad(stdscr, TRUE); // allow capture of special keystrokes, like arrow keys
	timeout(16); // make getch() only block for 16 ms (to allow realtime output 60 fps)

	/* open a client connection to the JACK server */

	client = jack_client_open (client_name, options, &status, server_name);
	if (client == NULL) {
		fprintf (stderr, "jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		exit (1);
	}
	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}
	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		fprintf (stderr, "unique name `%s' assigned\n", client_name);
	}

	/* tell the JACK server to call `process()' whenever
	   there is work to be done.
	*/

	jack_set_process_callback (client, process, 0);

	/* tell the JACK server to call `jack_shutdown()' if
	   it ever shuts down, either entirely, or if it
	   just decides to stop calling us.
	*/

	jack_on_shutdown (client, jack_shutdown, 0);

	/* display the current sample rate. 
	 */

	jack_nframes_t sample_rate = jack_get_sample_rate(client);
	printf ("engine sample rate: %" PRIu32 "\n", sample_rate);

	input_audio_port = jack_port_register (client, "Metronome Audio input",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);
	output_audio_port = jack_port_register (client, "Metronome Audio ouput",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);
	output_midi_port = jack_port_register (client, "MIDI Clock output",
					  JACK_DEFAULT_MIDI_TYPE,
					  JackPortIsOutput, 0);

	if ((input_audio_port == NULL) || (output_audio_port == NULL) || (output_midi_port == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}

	// initialize parameters

	risingThreshold_dB = -30.0f;
	fallingThreshold_dB = -50.0f;
	risingThreshold = linear_from_dB(risingThreshold_dB);
	fallingThreshold_dB = linear_from_dB(fallingThreshold_dB);

	lowMinTime_ms = 20.0f;
	lowMinTime_frames = ms_to_frames(lowMinTime_ms);


	/* Tell the JACK server that we are ready to roll.  Our
	 * process() callback will start running now. */

	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		exit (1);
	}

	/* Connect the ports.  You can't do this before the client is
	 * activated, because we can't make connections to clients
	 * that aren't running.  Note the confusing (but necessary)
	 * orientation of the driver backend ports: playback ports are
	 * "input" to the backend, and capture ports are "output" from
	 * it.
	 */

	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports\n");
		exit (1);
	}

	if (jack_connect (client, ports[0], jack_port_name (input_audio_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);
	
	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		fprintf(stderr, "no physical playback ports\n");
		exit (1);
	}

	if (jack_connect (client, jack_port_name (output_midi_port), ports[0])) {
		fprintf (stderr, "cannot connect output midi ports\n");
	}

	if (jack_connect (client, jack_port_name (output_audio_port), ports[0])) {
		fprintf (stderr, "cannot connect output audio ports\n");
	}

	free (ports);

	int selectedParameterIndex = 0;
	static const char *parameterNames[4];
	static float *parameterValuePointers[3];
	static const char *parameterNumberStringFormat[3];

	parameterNames[0] = "Rising threshold (dB)";
	parameterValuePointers[0] = &risingThreshold_dB;
	parameterNumberStringFormat[0] = " %1.2f dB ";

	parameterNames[1] = "Falling threshold (dB)";
	parameterValuePointers[1] = &fallingThreshold_dB;
	parameterNumberStringFormat[1] = " %1.2f dB ";

	parameterNames[2] = "Low Minimum Time (milliseconds)";
	parameterValuePointers[2] = &lowMinTime_ms;
	parameterNumberStringFormat[2] = " %1.2f ms ";

	/* keep running until stopped by the user */
	while (TRUE) {

		int keystroke = getch();
		if (keystroke != ERR) {
		  switch (keystroke) {

			// select another parameter

			case KEY_UP:
			if (selectedParameterIndex > 0)
				selectedParameterIndex--;
			break;

			case KEY_DOWN:
			if (selectedParameterIndex < 2)
				selectedParameterIndex++;
			break;

			// adjust selected parameter

			case KEY_RIGHT:
			case '=':
			*parameterValuePointers[selectedParameterIndex] += 1.0f;
			break;

			case KEY_SRIGHT:
			case '+':
			*parameterValuePointers[selectedParameterIndex] += 0.1f;
			break;

			case KEY_LEFT:
			case '-':
			*parameterValuePointers[selectedParameterIndex] -= 1.0f;
			break;

			case KEY_SLEFT:			
			case '_':
			*parameterValuePointers[selectedParameterIndex] -= 0.1f;
			break;

			// catch escape codes
			case 3:
			case 'q':
			case 'Q':
			goto exit;
		  }
		}

		if (risingThreshold_dB > 0.0f)
			risingThreshold_dB = 0.0f;

		if (fallingThreshold_dB < -100.0f)
			fallingThreshold_dB = -100.0f;

		if (fallingThreshold_dB > risingThreshold_dB)
			fallingThreshold_dB = risingThreshold_dB;

		if (lowMinTime_ms < 0.0f)
			lowMinTime_ms = 0.0f;
		
		lowMinTime_frames = ms_to_frames(lowMinTime_ms);

		// calculate linear from 10 ^ (dB/10)
		risingThreshold = linear_from_dB(risingThreshold_dB);
		fallingThreshold = linear_from_dB(fallingThreshold_dB);

		erase(); // clear screen

		getmaxyx(stdscr, maxRows, maxCols);
		int barCols = maxCols > 24 ? maxCols - 24 : 0;

		int col = 24 + ((float) 100.0f + risingThreshold_dB) / 100.0f * barCols;
			mvprintw(0, col, "R");
/*
		mvprintw( 0, 0, "input amplitude:  %1.4f ", maxAmplitudeInput);
		printbar( maxAmplitudeInput, barCols);
		maxAmplitudeInput = 0.0f;
		if (compressorThreshold < 1.0f) {
			int col = 24 + ((float) compressorThreshold) * barCols;
			mvprintw(0, col, "|");
		}

		mvprintw( 1, 0, "output amplitude: %1.4f ", maxAmplitudeOutput);
		printbar( maxAmplitudeOutput, barCols);
		maxAmplitudeOutput = 0.0f;
		float compressorThresholdTimesMakeupGain = compressorThreshold * makeupGain;
		if (compressorThresholdTimesMakeupGain < 1.0f) {
			int col = 24 + ((float) compressorThresholdTimesMakeupGain) * barCols;
			mvprintw(1, col, "|");
		}
*/
		mvprintw( 3, 0, "Parameters:");

		for (int i=0; i<3; i++) {
			if (selectedParameterIndex == i)
				attron(A_REVERSE);

			mvprintw( i+4, 0, parameterNumberStringFormat[i], *parameterValuePointers[i]);
			attroff(A_REVERSE);

			printw( parameterNames[i] );
		}

		mvprintw( 9, 0, "Usage: UP/DOWN to select a parameter, and LEFT/RIGHT to modify the selected parameter's value. Exit with Q.");
	
		mvprintw( 10, 0, "Detected Beat = %d", detectedBeat);
		mvprintw( 11, 0, "falling = %f, rising = %f", fallingThreshold, risingThreshold);

		jack_nframes_t diffBeatStart = currBeatStart - lastBeatStart;
		mvprintw( 12, 0, "diffBeatStart = %d frames or %f seconds.", diffBeatStart, ((float) diffBeatStart) / ((float)sample_rate));
		mvprintw( 13, 0, "currBeatStart = %d", currBeatStart);
		mvprintw( 14, 0, "lastBeatStart = %d", lastBeatStart);
		mvprintw( 15, 0, "lowMinTime_frames = %d", lowMinTime_frames);
		mvprintw( 16, 0, "earliestNextBeatStart = %d", earliestNextBeatStart);
		mvprintw( 17, 0, "nDetectedBeats = %d", nDetectedBeats);
	}

	/* this is never reached but if the program
	   had some other way to exit besides being killed,
	   they would be important to call.
	*/
exit:
	endwin();
	jack_client_close (client);
	exit (0);
}
