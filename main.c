#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include <errno.h>
#include <stdint.h>

#include "alsa_play.h"

int main(int argc, char *argv[])
{
	char wav_file[4][96]= {0};
	char alsa_device[36]= "bluealsa:DEV=F4:4E:FD:00:65:5E";
	int opt;
	int period = -1;
	short gray_noise= 0; //defaults to silence
	short i, file_number;
	int update_interval_millis= 9;
	int length_seconds= 4;
	short speak_modulus= 69;

	while ((opt = getopt(argc, argv, "gqa:b:c:d:p:l:m:t:")) != -1)
	{
		switch (opt)
		{
		case 'g':
			gray_noise= 1;
			break;
		case 'q':
			queue_mode= 1;
			break;
		case 'a':
			strcpy(wav_file[1], optarg);
			break;
		case 'b':
			strcpy(wav_file[2], optarg);
			break;
		case 'c':
			strcpy(wav_file[3], optarg);
			break;
		case 'p':
			period = atoi(optarg);
			break;
		case 'l':
			length_seconds = atoi(optarg);
			break;
		case 't':
			update_interval_millis = atoi(optarg);
			break;
		case 'm':
			speak_modulus = atoi(optarg);
			break;
		case 'd':
			strcpy(alsa_device, optarg);
			break;
		default:
			fprintf(stderr, "unknown/invalid option: '-%c'\n", optopt);
			exit(-1);
		}
	}

	if (gray_noise && read_wav_file("/home/pi/grey.wav", 0) != 0)
		exit(-1);
	
	#define READ_FILES_AT_START 0
	if (READ_FILES_AT_START)
		for (file_number=FIRST; file_number < 4; file_number++)
			if ((wav_file[file_number][0] != 0) && (read_wav_file(wav_file[file_number], file_number) != 0))
				exit(-1);

	if (alsa_init(alsa_device, period) != 0)
		exit(-1);

	file_number= 1;
	for (i=0; i < (length_seconds*1000/update_interval_millis); i++)
	{
		if (alsa_update() < 0)
			exit(1);
		sleepMicros(update_interval_millis*1000);
		if (queue_mode)
		{
			if (i % speak_modulus == 0)
			{
				// NOTE: running with a modulus of 1 will do back-to-back
				if ( (i/speak_modulus) == 1 || wav_buffer_done) 
				{
					//always read into buffer 1
					// printf("Opening %s (file=%u) on count=%u\n", wav_file[file_number], file_number, i);
					if ((wav_file[file_number][0] != 0) && (read_wav_file(wav_file[file_number], FIRST) == 0))
					{
						wav_buffer_ready= 1; //start
						wav_buffer_done= 0;
						if (++file_number > 3 || wav_file[file_number][0] == 0)
							file_number= 1;
					}
				}
			}
		}
	}
	// alsa_play();
	alsa_deinit();

	return 0;
}
