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
	while (nanosleep(&sleep, &sleep) && errno == EINTR)
		;
}

int main(int argc, char *argv[])
{
	char wav_file[96] = {0};
	char alsa_device[36] = "bluealsa:DEV=F4:4E:FD:00:65:5E";
	int opt;
	int period = -1;

	while ((opt = getopt(argc, argv, "f:d:p:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			strcpy(wav_file, optarg);
			printf("wav file %s\n", wav_file);
			break;
		case 'p':
			period = atoi(optarg);
			break;
		case 'd':
			strcpy(alsa_device, optarg);
			break;
		case '?':
		/* fall though */
		default:
			fprintf(stderr, "unknown/invalid option: '-%c'\n", optopt);
			exit(-1);
		}
	}

	if (alsa_init(alsa_device, wav_file, period) != 0)
	{
		printf("alsa init failed\n");
		exit(-1);
	}

	int i;
	for (i = 0; i < 60; i++)
	{
		alsa_update();
		sleepMicros(25);
		printf("%d..", i);
	}
	printf("\n Done.\n");
	return 0;

	alsa_play();
	// printf("between plays\n");
	// alsa_play();
	alsa_deinit();

	return 0;
}
