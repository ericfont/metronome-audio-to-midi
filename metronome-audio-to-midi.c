/** @file jack-curses-lowpass-compressor-gain.c
 *
 * @brief Applies IIR averaging filter, chained into dynamic range compressor, chained into final gain.
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

jack_port_t *input_port;
jack_port_t *output_port;
jack_client_t *client;

float makeupGain_dB = 0.0f; // user inputs gain in dB
float makeupGain = 1.0f; // automatically calculated

float compressorThreshold_dB = 0.0f; // user input in dB
float compressorThreshold = 1.0f; // user input in dB

float compressorRatio = 1.0f; // user input
float compressorRatioReciprocal = 1.0f; // automatically calculated

float maxAmplitudeInput = 0.0f; // updates between every screen redraw
float maxAmplitudeOutput = 0.0f; // updates bewtween every screen redraw

float lowpassFilterSteepness = 0.0f; // user input
float averagingAlpha = 1.0f; // automatically calcuated from lowpassFilterSteepness

int maxRows, maxCols; // screen dimensions

static inline float linear_from_dB(float dB) {
	return powf(10.0f, 0.05f * dB);
}

static inline float dB_from_linear(float linear) {
	return 20.0f * log10f(linear);
}

float simpleRecursiveAverageInfiniteImpulseResponseFilter(float currentInput)
{
	static float runningAverage = 0.0f; // keeps running average

	runningAverage += averagingAlpha * (currentInput - runningAverage);
	return runningAverage;
}

float compress(float absoluteValueInput)
{
	if (absoluteValueInput > compressorThreshold) {
		float absoluteValueInput_dB = dB_from_linear(absoluteValueInput);
		return linear_from_dB(compressorThreshold_dB + (absoluteValueInput_dB - compressorThreshold_dB) * compressorRatioReciprocal);
	}
	else
		return absoluteValueInput;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 */
int
process (jack_nframes_t nframes, void *arg)
{
	jack_default_audio_sample_t *in, *out;
	
	in = jack_port_get_buffer (input_port, nframes);
	out = jack_port_get_buffer (output_port, nframes);

	int i;

	for (i = 0; i < nframes; i++) {

		float absoluteInput = fabs(in[i]);
		if (absoluteInput > maxAmplitudeInput)
			maxAmplitudeInput = absoluteInput;

		float filterResult = simpleRecursiveAverageInfiniteImpulseResponseFilter(in[i]);
		float absoluteFilterResult = fabs(filterResult);
		bool isNegative = (filterResult < 0.0f);

		float absoluteCompressed = compress(absoluteFilterResult);
		float absoluteCompressedGained = absoluteCompressed * makeupGain;

		if (absoluteCompressedGained > 1.0f)
			absoluteCompressedGained = 1.0f;

		if (absoluteCompressedGained > maxAmplitudeOutput)
			maxAmplitudeOutput = absoluteCompressedGained;

		out[i] = isNegative ? -absoluteCompressedGained : absoluteCompressedGained;
	}

	return 0;      
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */

void
jack_shutdown (void *arg)
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

int
main (int argc, char *argv[])
{
	const char **ports;
	const char *client_name = "compressor-filter";
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

	printf ("engine sample rate: %" PRIu32 "\n",
		jack_get_sample_rate (client));

	/* create two ports */

	input_port = jack_port_register (client, "input",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);
	output_port = jack_port_register (client, "output",
					  JACK_DEFAULT_AUDIO_TYPE,
					  JackPortIsOutput, 0);

	if ((input_port == NULL) || (output_port == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}

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

	if (jack_connect (client, ports[0], jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);
	
	ports = jack_get_ports (client, NULL, NULL,
				JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		fprintf(stderr, "no physical playback ports\n");
		exit (1);
	}

	if (jack_connect (client, jack_port_name (output_port), ports[0])) {
		fprintf (stderr, "cannot connect output ports\n");
	}

	free (ports);

	int selectedParameterIndex = 3;
	static const char *parameterNames[4];
	static float *parameterValuePointers[4];
	static const char *parameterNumberStringFormat[4];

	parameterNames[0] = "low-pass filter steepness";
	parameterValuePointers[0] = &lowpassFilterSteepness;
	parameterNumberStringFormat[0] = " %1.2f    ";

	parameterNames[1] = "compressor ratio";
	parameterValuePointers[1] = &compressorRatio;
	parameterNumberStringFormat[1] = " %1.2f    ";

	parameterNames[2] = "compressor threshold";
	parameterValuePointers[2] = &compressorThreshold_dB;
	parameterNumberStringFormat[2] = "%+1.2f dB ";

	parameterNames[3] = "makeup gain";
	parameterValuePointers[3] = &makeupGain_dB;
	parameterNumberStringFormat[3] = "%+1.2f dB ";

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
			if (selectedParameterIndex < 3)
				selectedParameterIndex++;
			break;

			// adjust selected parameter

			case KEY_RIGHT:
			case '=':
			*parameterValuePointers[selectedParameterIndex] += 0.1f;
			break;

			case KEY_SRIGHT:
			case '+':
			*parameterValuePointers[selectedParameterIndex] += 0.01f;
			break;

			case KEY_LEFT:
			case '-':
			*parameterValuePointers[selectedParameterIndex] -= 0.1f;
			break;

			case KEY_SLEFT:			
			case '_':
			*parameterValuePointers[selectedParameterIndex] -= 0.01f;
			break;

			// catch escape codes
			case 3:
			case 'q':
			case 'Q':
			goto exit;
		  }
		}

		if (compressorRatio < 1.0f)
			compressorRatio = 1.0f;

		if (lowpassFilterSteepness > 0.99f)
			lowpassFilterSteepness = 0.99f;
		else if (lowpassFilterSteepness < 0.0f)
			lowpassFilterSteepness = 0.0f;

		averagingAlpha = 1.0f - lowpassFilterSteepness;

		// calculate linear from 10 ^ (dB/10)
		makeupGain = linear_from_dB(makeupGain_dB);
		compressorThreshold = linear_from_dB(compressorThreshold_dB);

		compressorRatioReciprocal = 1.0f / compressorRatio;

		erase(); // clear screen

		getmaxyx(stdscr, maxRows, maxCols);
		int barCols = maxCols > 24 ? maxCols - 24 : 0;

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

		mvprintw( 3, 0, "Parameters:");


		for (int i=0; i<4; i++) {
			if (selectedParameterIndex == i)
				attron(A_REVERSE);

			mvprintw( i+4, 0, parameterNumberStringFormat[i], *parameterValuePointers[i]);
			attroff(A_REVERSE);

			printw( parameterNames[i] );
		}

		mvprintw( 9, 0, "Usage: UP/DOWN to select a parameter, and LEFT/RIGHT to modify the selected parameter's value. Exit with Q.");

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
