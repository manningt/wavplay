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
	char wav_file[3][96]= {0};
	char alsa_device[36]= "bluealsa:DEV=F4:4E:FD:00:65:5E";
	int opt;
	int period = -1;
	short gray_noise= 0; //defaults to silence
	short i;


	while ((opt = getopt(argc, argv, "ga:b:c:d:p:")) != -1)
	{
		switch (opt)
		{
		case 'g':
			gray_noise= 1;
			break;
		case 'a':
			strcpy(wav_file[0], optarg);
			break;
		case 'b':
			strcpy(wav_file[1], optarg);
			break;
		case 'c':
			strcpy(wav_file[2], optarg);
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
	
	for (i=0; i < 3;i++)
		if (read_wav_file(wav_file[i], i) != 0)
			exit(-1);

	if (alsa_init(alsa_device, period) != 0)
		exit(-1);

	for (i=0; i < 65; i++)
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
