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


	while ((opt = getopt(argc, argv, "gqa:b:c:d:p:")) != -1)
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

	file_number= 1 ;
	for (i=0; i < 300; i++)
	{
		if (alsa_update() < 0)
			exit(1);
		sleepMicros(9000);
		if (queue_mode)
		{
			if (i == 30 || i == 90)
			{
				//always read into buffer 1
				if ((wav_file[file_number][0] != 0) && (read_wav_file(wav_file[file_number], FIRST) == 0))
				{
					wav_buffer_ready= 1; //start
					wav_buffer_done= 0;
				}
				file_number++;
			}
			// the following is for back-to-back plays:
			// if (wav_buffer_done)
			// {
			// 	printf("restarting buffer load on count=%u",i);
			// 	wav_buffer_ready= 1;
			// 	wav_buffer_done= 0;
			// }
		}
	}

	// printf("sleeping...\n");
	// sleepMicros(2000000);
	// alsa_play();
	alsa_deinit();

	return 0;
}
