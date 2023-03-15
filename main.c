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
	char wav_file[96]= {0};
	char alsa_device[36]= "bluealsa:DEV=F4:4E:FD:00:65:5E";
	int opt;
	int period = -1;

	while ((opt = getopt(argc, argv, "f:d:p:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			strcpy(wav_file, optarg);
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

	if (1 && read_wav_file("/home/pi/grey.wav", 0) != 0)
		exit(-1);
	
	// if (read_wav_file(wav_file, 1) != 0)
	// 	exit(-1);
	if (read_wav_file("/home/pi/repos/audio/one.wav", 1) != 0)
		exit(-1);
	if (read_wav_file("/home/pi/repos/audio/point.wav", 2) != 0)
		exit(-1);

	if (alsa_init(alsa_device, period) != 0)
		exit(-1);

	int i=0;
	for (; i < 63; i++)
	{
		alsa_update();
		sleepMicros(25000);
	}

	// printf("sleeping...\n");
	// sleepMicros(2000000);
	// alsa_play();
	alsa_deinit();

	return 0;
}
