#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <time.h>
#include <errno.h>
#include <stdint.h>

#include "alsa_play.h"

void sleepMicros(uint32_t micros)
{
	struct timespec sleep;
	sleep.tv_sec = micros / 1000000L;
	sleep.tv_nsec = (micros % 1000000L) * 1000L;
	while (nanosleep(&sleep, &sleep) && errno == EINTR);
}

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

	if (read_wav_file("/home/pi/grey.wav") != 0)
		exit(-1);

	if (read_wav_file(wav_file) != 0)
		exit(-1);

	if (alsa_init(alsa_device, period) != 0)
		exit(-1);

	int i;
	for (i = 0; i < 120; i++)
	{
		alsa_update();
		sleepMicros(22000);
	}
	printf("sleeping...\n");
	sleepMicros(4000000);

	// alsa_play();
	// printf("between plays\n");
	// alsa_play();
	alsa_deinit();

	return 0;
}
