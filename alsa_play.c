#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* malloc() */
#include <unistd.h> /* read() */
#include <time.h>

#include <alsa/asoundlib.h>
#include <stdbool.h>

#include "alsa_play.h"

#define PCM_DEVICE "hw:1,0"

#define WAV_HEADER 44
#define SAMPLE_SIZE 2
#define NUM_CHANNELS 1
#define WAV_FILE_RATE 11025
#define FRAME_SIZE (SAMPLE_SIZE * NUM_CHANNELS)
#define PERIOD_SIZE 240
#define BUFFER_SIZE_MULTIPLIER 20
#define IDLE_FRAMES_AVAILABLE 4800 //found by experiment when period was 240
// 4800 (idle buffers) - 960 (43 mSec) = 3840

// data rate = channels * audio sample * rate = 22050
// interrupt period = period size * frame size / data rate = 240*(2/22050) = 21.768 millis

static snd_pcm_t *pcm_handle;
#define NUMBER_OF_BUFFERS 4
long wav_size[NUMBER_OF_BUFFERS];
char wav_buff[4][200000]= {0};
#define FILLER_START 1800

uint8_t wav_buffer_available= 0;

int alsa_update()
{
	int ret;
	int avail_buffs;
	bool err = false;
	char *wav_buff_ptr;
	snd_pcm_sframes_t frames_requested, frames_written;
	uint32_t frames_written_count=0;
	uint8_t loop_count=0;

	static int index = WAV_HEADER; // skip header and start at PCM data
	static int filler_index= FILLER_START;
	static uint32_t call_count=0;
	static uint32_t total_frames_written_count=0;

	ret = snd_pcm_wait(pcm_handle, 1000); // returns 1 normally
	if (ret == 0)
	{
		fprintf(stderr, "PCM timeout occurred\n");
		return -1;
	}
	else if (ret < 0)
	{
		fprintf(stderr, "PCM device error: %s\n", snd_strerror(ret));
		return ret;
	}

	if (call_count == 16)
		wav_buffer_available= 1;

	avail_buffs = snd_pcm_avail(pcm_handle);
	while (avail_buffs >= (IDLE_FRAMES_AVAILABLE - (4 * PERIOD_SIZE)))
	{
		frames_requested = snd_pcm_avail_update(pcm_handle);
		if (frames_requested < 0)
		{
			fprintf(stderr, "PCM error requesting frames: %s\n",
					  snd_strerror(ret));
			err = true;
			return -1;
		}
		frames_requested =
			(frames_requested > PERIOD_SIZE) ? PERIOD_SIZE : frames_requested;

		if (!wav_buffer_available)
			wav_buff_ptr= wav_buff[0]+filler_index;
		else
		{
			wav_buff_ptr= wav_buff[1]+index;
			// if ((frames_requested * FRAME_SIZE) + index > (wav_size[1]))
			// {
			// 	snd_pcm_sframes_t temp= ((wav_size[1] - index) / FRAME_SIZE) >> 3;
			// 	temp= temp << 3;
			// 	log_main("frames_requested_before=%u index=%u wav_size=%u frames_requested_after=%d",
			// 		frames_requested, index, wav_size, temp);
			// 	frames_requested= temp;
			// }
		}

		frames_written = snd_pcm_writei(pcm_handle, wav_buff_ptr, frames_requested);

		if (frames_written == -EAGAIN)
			continue;
		else if (frames_written == -EPIPE)
		{
			fprintf(stderr, "PCM write error: Underrun event\n");
			frames_written = snd_pcm_prepare(pcm_handle);
			if (frames_written < 0)
			{
				fprintf(stderr,
						  "Can't recover from underrun, prepare failed: %s\n",
						  snd_strerror(frames_written));
				err = true;
			return -1;
			}
		}
		else if (frames_written == -ESTRPIPE)
		{
			fprintf(stderr, "PCM write error: Stream is suspended\n");
			while ((frames_written = snd_pcm_resume(pcm_handle)) == -EAGAIN)
			{
				sleep(1); // wait until the suspend flag is released
			}
			if (frames_written < 0)
			{
				frames_written = snd_pcm_prepare(pcm_handle);
				if (frames_written < 0)
				{
					fprintf(stderr,
							  "Can't recover from suspend, prepare failed: %s\n",
							  snd_strerror(frames_written));
					err = true;
				return -1;
				}
			}
		}
		else 
		{
			if (!wav_buffer_available)
			{
				if ((filler_index += (frames_requested * FRAME_SIZE)) >= wav_size[0])
					filler_index= FILLER_START;
			}
			else
			{
				if ((index += (frames_requested * FRAME_SIZE)) >= wav_size[1])
				{
					// log_main("elapsed micros in play: %lld", (micros() - alsa_play_start_micros));
					wav_buffer_available= 0;
				}
			}
		}

		frames_written_count += frames_written;

		avail_buffs = snd_pcm_avail(pcm_handle);
		// log_main("call_count=%d  loop_count=%d frames_count=%d avail=%d",
		// 	call_count, loop_count, frames_written_count, avail_buffs);
		loop_count += 1;
	}
	
	call_count++;
	total_frames_written_count += frames_written_count;
	log_main("call_count=%u total_frames=%u avail=%u playing_wav=%u index=%d state=%s",
   	call_count, total_frames_written_count, avail_buffs, wav_buffer_available,
		index, snd_pcm_state_name(snd_pcm_state(pcm_handle)));

	return err;
}

int alsa_play(void)
{
	printf("Starting alsa_play");
	uint64_t alsa_play_start_micros = micros();

	int frame_count = 0;
	int ret = 0;
	int index = WAV_HEADER; // skip header and start at PCM data
	snd_pcm_sframes_t frames_requested, frames_written;

	while (1)
	{
		frame_count++;
		ret = snd_pcm_wait(pcm_handle, 1000); // returns 1 normally
		if (ret == 0)
		{
			fprintf(stderr, "PCM timeout occurred\n");
			return -1;
		}
		else if (ret < 0)
		{
			fprintf(stderr, "PCM device error: %s\n", snd_strerror(ret));
			return ret;
		}

		frames_requested = snd_pcm_avail_update(pcm_handle);
		if (frames_requested < 0)
		{
			fprintf(stderr, "PCM error requesting frames: %s\n",
					  snd_strerror(ret));
			return ret;
		}

		int avail = snd_pcm_avail(pcm_handle);
		// if (avail < 1200)
		// {
		// 	int sleep_time = 24000;
		// 	log_main("sleeping %d micros", sleep_time);
		// 	sleepMicros(sleep_time);
		// 	continue;
		// }

		/* deliver data one period at a time */
		frames_requested =
			 (frames_requested > PERIOD_SIZE) ? PERIOD_SIZE : frames_requested;

		/* don't overrun wav file buffer */
		frames_requested = (frames_requested * FRAME_SIZE + index > wav_size[1])
									  ? (wav_size[1] - index) / FRAME_SIZE
									  : frames_requested;

		// attempt to get rid of glitch at end of file
		// #define LAST_WRITE_SIZE 16
		// if (frames_requested < 50)
		//     frames_requested= LAST_WRITE_SIZE;
		frames_written = snd_pcm_writei(pcm_handle, &wav_buff[0][index], frames_requested);

		log_main("frame_write_count=%u  frames_written=%u; avail=%u",
					frame_count, frames_written, avail); // avail_delay);

		if (frames_written == -EAGAIN)
		{
			continue;
		}
		// attempt to get rid of glitch at end of file
		// if (frames_written == LAST_WRITE_SIZE) {
		//     log_main("early return.");
		//     return -1;
		// }

		if (frames_written == -EPIPE)
		{ // underrun
			fprintf(stderr, "PCM write error: Underrun event\n");
			frames_written = snd_pcm_prepare(pcm_handle);
			if (frames_written < 0)
			{
				fprintf(stderr,
						  "Can't recover from underrun, prepare failed: %s\n",
						  snd_strerror(frames_written));
				return frames_written;
			}
		}
		else if (frames_written == -ESTRPIPE)
		{
			fprintf(stderr, "PCM write error: Stream is suspended\n");
			while ((frames_written = snd_pcm_resume(pcm_handle)) == -EAGAIN)
			{
				sleep(1); // wait until the suspend flag is released
			}
			if (frames_written < 0)
			{
				frames_written = snd_pcm_prepare(pcm_handle);
				if (frames_written < 0)
				{
					fprintf(stderr,
							  "Can' recover from suspend, prepare failed: %s\n",
							  snd_strerror(frames_written));
					return frames_written;
				}
			}
		}

		index += frames_requested * FRAME_SIZE;

		if (index >= wav_size[1])
		{
			printf("End of file\n");
			log_main("elapsed micros in play: %lld", (micros() - alsa_play_start_micros));
			return 0;
		}
	}
	return -1; // we should never get here
}

int pcm_set_sw_params(snd_pcm_t *handle, snd_pcm_sw_params_t *params, int period)
{
	int ret;
	snd_pcm_uframes_t period_size, threshold;

	// get a fully populated configuration space
	ret = snd_pcm_sw_params_current(handle, params);
	if (ret)
	{
		fprintf(stderr, "Cannot init sw params struct: %s\n", snd_strerror(ret));
		return ret;
	}

	// set the software wakeup period in frames
	period_size = PERIOD_SIZE; // NOTE: this may need to be a power of 2
	ret = snd_pcm_sw_params_set_avail_min(handle, params, period_size);
	if (ret)
	{
		fprintf(stderr, "Cannot set min available frames: %s\n", snd_strerror(ret));
		return ret;
	}

	// set start threshold. make this equal to period size to avoid underrun during first playback
	threshold = (period < 0) ? PERIOD_SIZE : period;
	threshold= 128; //5.8 millis
	ret = snd_pcm_sw_params_set_start_threshold(handle, params, threshold);
	if (ret)
	{
		fprintf(stderr, "Couldn't set start threshold: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_sw_params(handle, params);
	if (ret)
	{
		fprintf(stderr, "Couldn't write sw params to PCM device: %s\n", snd_strerror(ret));
		return ret;
	}

	// ret = snd_pcm_sw_params_get_start_threshold(params, &threshold);
	// if (ret)
	// {
	// 	fprintf(stderr, "Couldn't get start threshold: %s\n", snd_strerror(ret));
	// 	return ret;
	// }
	// printf("Start threshold is %lu frames\n", threshold);

	return 0;
}

int pcm_print_hw_params(snd_pcm_hw_params_t *params)
{
	int ret, dir;
	enum MIN_MAX {MIN, MAX};
	snd_pcm_uframes_t buffer_frames[2], period_frames[2];

	ret = snd_pcm_hw_params_get_period_size_min(params, &period_frames[MIN], &dir);
	if (ret)
	{
		fprintf(stderr, "Failed to get min period size: %s\n", snd_strerror(ret));
		return ret;
	}
	ret = snd_pcm_hw_params_get_period_size_max(params, &period_frames[MAX], &dir);
	if (ret)
	{
		fprintf(stderr, "Failed to get max period size: %s\n", snd_strerror(ret));
		return ret;
	}
	printf("period size: min=%lu frames (%lu bytes) max=%lu frames (%lu bytes)\n", 
		period_frames[MIN], period_frames[MIN] * FRAME_SIZE, 
		period_frames[MAX], period_frames[MAX] * FRAME_SIZE);


	ret = snd_pcm_hw_params_get_buffer_size_min(params, &buffer_frames[MIN]);
	if (ret)
	{
		fprintf(stderr, "Failed to get min buffer size: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_get_buffer_size_max(params, &buffer_frames[MAX]);
	if (ret)
	{
		fprintf(stderr, "Failed to get max buffer size: %s\n", snd_strerror(ret));
		return ret;
	}

	printf("buffer size: min=%lu frames (%lu bytes) max=%lu frames (%lu bytes)\n", 
		buffer_frames[MIN], buffer_frames[MIN] * FRAME_SIZE, 
		buffer_frames[MAX], buffer_frames[MAX] * FRAME_SIZE);

	return 0;
}

void pcm_print_state(snd_pcm_t *handle)
{
	printf("PCM device state: %s\n", snd_pcm_state_name(snd_pcm_state(handle)));
}

int pcm_set_hw_params(snd_pcm_t *handle, snd_pcm_hw_params_t *params, int period)
{
	int ret;
	snd_pcm_uframes_t buffer_size, period_size;

	/* Get a fully populated configuration space */
	ret = snd_pcm_hw_params_any(pcm_handle, params);
	if (ret)
	{
		fprintf(stderr, "Couldn't initialize hw params: %s\n",
				  snd_strerror(ret));
		return ret;
	}

	/*
	 * Hardware parameters must be set in this order:
	 * 1. access
	 * 2. format
	 * 3. subformat
	 * 4. channels
	 * 5. rate
	 * 6. min period
	 * 7. max buffer
	 * 8. tick time <- where's this set?
	 */

	ret = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret)
	{
		fprintf(stderr, "Access type not available: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16);
	if (ret)
	{
		fprintf(stderr, "Sample format not available: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_subformat(handle, params, SND_PCM_SUBFORMAT_STD);
	if (ret)
	{
		fprintf(stderr, "Subformat not available: %s\n", snd_strerror(ret));
		return ret;
	}

	// enable hardware resampling
	// ret = snd_pcm_hw_params_set_rate_resample(handle, params, 0);
	ret = snd_pcm_hw_params_set_rate_resample(handle, params, 1); // enable hardware resample - plays 4x slow
	if (ret)
	{
		fprintf(stderr, "Resampling setup failed: %s\n", snd_strerror(ret));
		return ret;
	}

	ret = snd_pcm_hw_params_set_channels(handle, params, NUM_CHANNELS);
	if (ret)
	{
		fprintf(stderr, "Channel count setup failed: %s\n", snd_strerror(ret));
		return ret;
	}

	// set playback rate
	unsigned int requested_rate= WAV_FILE_RATE;
	unsigned int set_rate= requested_rate;
	ret = snd_pcm_hw_params_set_rate_near(handle, params, &set_rate, 0);
	if (ret)
	{
		fprintf(stderr, "Rate no available for playback: %s\n", snd_strerror(ret));
		return ret;
	}

	if (set_rate < requested_rate - 2 && set_rate > requested_rate + 2)
	{
		fprintf(stderr,
				  "Set rate ( %u Hz ) does not match requested rate ( %u Hz )\n",
				  set_rate, requested_rate);
		return -EINVAL;
	}

	// pcm_print_hw_params(params);

	period_size = (period < 0) ? PERIOD_SIZE : period;
	ret = snd_pcm_hw_params_set_period_size(handle, params, period_size, 0);
	if (ret)
	{
		fprintf(stderr, "Period size=%ld not available: %s\n", period_size, snd_strerror(ret));
		return ret;
	}
	// printf("Period size set to %lu frames\n", period_size);

	buffer_size = (period < 0) ? (BUFFER_SIZE_MULTIPLIER * PERIOD_SIZE) : (BUFFER_SIZE_MULTIPLIER * period);
	ret = snd_pcm_hw_params_set_buffer_size(handle, params, buffer_size);
	if (ret)
	{
		fprintf(stderr, "Buffer size not available: %s\n", snd_strerror(ret));
		return ret;
	}
	// printf("Buffer size set to %lu frames\n", buffer_size);

	ret = snd_pcm_hw_params(handle, params); // write hardware parameters
	if (ret)
	{
		fprintf(stderr,
				  "Unable to write hardware parameters to PCM device: %s\n",
				  snd_strerror(ret));
		return ret;
	}

	// snd_pcm_hw_params() should call snd_pcm_prepare()
	pcm_print_state(handle);

	int dir;
	ret = snd_pcm_hw_params_get_period_size(params, &period_size, &dir);
	if (ret)
	{
		fprintf(stderr, "Can't get period size\n");
	}
	unsigned int period_time;
	ret = snd_pcm_hw_params_get_period_time(params, &period_time, &dir);
	if (ret)
	{
		fprintf(stderr, "Can't get period time\n");
	}
	ret = snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (ret)
	{
		fprintf(stderr, "Can't get buffer size\n");
	}
	printf("Actuals: period size=%lu (time=%0.3f millis)  buffer size=%lu  rate=%u\n", 
		period_size, period_time / 1000.0, buffer_size, set_rate);

	return 0;
}

uint32_t parse_wav_header(uint8_t buffer_number)
{
	// wav format: https://docs.fileformat.com/audio/wav/
	uint32_t i;
	char chunkID[5]= {0};
	uint32_t file_size= 0;
	uint32_t data_size= 0;
	uint16_t audio_format= 0; //PCM = 1
   uint16_t number_of_channels= 0;
   uint32_t sample_rate= 0; //check for 11025
   // int byte_rate; //== SampleRate * NumChannels * BitsPerSample/8
   // uint16_t block_align; //== NumChannels * BitsPerSample/8
   uint16_t bits_per_sample= 0; //8 bits = 8, 16 bits = 16, etc.

	for (i= 0 ; i < 4 ; i++)
		chunkID[i]= wav_buff[buffer_number][i];
	
	for (i= 7 ; i >= 4 ; i--)
		file_size= (file_size << 8) + wav_buff[buffer_number][i];

	for (i= 21 ; i >= 20 ; i--)
		audio_format= (audio_format << 8) + wav_buff[buffer_number][i];

	for (i= 23 ; i >= 22 ; i--)
		number_of_channels= (number_of_channels << 8) + wav_buff[buffer_number][i];

	for (i= 27 ; i >= 24 ; i--)
		sample_rate= (sample_rate << 8) + wav_buff[buffer_number][i];

	for (i= 35 ; i >= 34 ; i--)
		bits_per_sample= (bits_per_sample << 8) + wav_buff[buffer_number][i];

	for (i= 43 ; i >= 40 ; i--)
		data_size= (data_size << 8) + wav_buff[buffer_number][i];

	printf("chunkID=%s file_size=%u format=%d chan=%u rate=%d bits=%d data_size=%d file_pad=%ld\n", 
		chunkID, file_size, audio_format, number_of_channels, sample_rate, bits_per_sample, data_size,
		(wav_size[buffer_number] - WAV_HEADER - data_size));

	wav_size[buffer_number]= data_size+WAV_HEADER;
	return 0;
}

int read_wav_file(char *wav_file, uint8_t buffer_number)
{
	int32_t bytes_read;

	FILE *wav_fd = fopen(wav_file, "rb");
	if (wav_fd == NULL)
	{
		fprintf(stderr, "Error opening file=%s\n", wav_file);
		return -1;
	}
	fseek(wav_fd, 0, SEEK_END);
	wav_size[buffer_number] = ftell(wav_fd);
	rewind(wav_fd);

	bytes_read= fread(wav_buff[buffer_number], 1, wav_size[buffer_number], wav_fd);

	if (bytes_read != wav_size[buffer_number])
	{
		fprintf(stderr, "Error reading wave file\n");
		return -1;
	}
	fclose(wav_fd);
	parse_wav_header(buffer_number);
	return 0;
}

int alsa_init(char *device_name, int period)
{
	int ret;

	snd_pcm_hw_params_t *hw_params;
	snd_pcm_sw_params_t *sw_params;
	if (device_name != NULL)
		ret= snd_pcm_open(&pcm_handle, device_name, SND_PCM_STREAM_PLAYBACK, 0);
	else
		ret = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_PLAYBACK, 0);

	if (ret)
	{
		fprintf(stderr, "PCM device open error: %s\n", snd_strerror(ret));
		return ret;
	}

	pcm_print_state(pcm_handle);

	ret = snd_pcm_hw_params_malloc(&hw_params);
	if (ret)
	{
		fprintf(stderr, "Cannot allocate hw param struct: %s\n", snd_strerror(ret));
		return ret;
	}
	ret = pcm_set_hw_params(pcm_handle, hw_params, period);
	if (ret)
		return ret;
	snd_pcm_hw_params_free(hw_params);


	ret = snd_pcm_sw_params_malloc(&sw_params);
	if (ret)
	{
		fprintf(stderr, "Cannot allocate sw param struct: %s\n",  snd_strerror(ret));
		return ret;
	}
	ret = pcm_set_sw_params(pcm_handle, sw_params, period);
	if (ret)
		return ret;
	snd_pcm_sw_params_free(sw_params);

	// pcm_print_state(pcm_handle);
	// printf("PCM device name: %s\n", snd_pcm_name(pcm_handle));
	return 0;
}

void alsa_deinit(void)
{
	uint64_t before_drain_micros = micros();
	snd_pcm_drain(pcm_handle);
	log_main("drain_micros=%llu", micros() - before_drain_micros);
	snd_pcm_close(pcm_handle);
}

// --------------- common code

void sleepMicros(uint32_t micros)
{
	struct timespec sleep;
	sleep.tv_sec = micros / 1000000L;
	sleep.tv_nsec = (micros % 1000000L) * 1000L;
	while (nanosleep(&sleep, &sleep) && errno == EINTR);
}

uint64_t micros()
{
	struct timespec now;
	uint64_t m;
	clock_gettime(CLOCK_MONOTONIC, &now);
	m = now.tv_sec * 1e6;
	m += now.tv_nsec / 1e3;
	return m;
}

void log_main(const char *format, ...)
{
	char info_string[96];
	struct timespec curTime;
	struct tm *info;
	clock_gettime(CLOCK_REALTIME, &curTime);
	info = localtime(&curTime.tv_sec);
	sprintf(info_string, "%d-%02d-%02dT%02d:%02d:%02d.%03d", 1900 + info->tm_year, info->tm_mon + 1, info->tm_mday,
			  info->tm_hour, info->tm_min, info->tm_sec, (int)(curTime.tv_nsec / 1000000));

	char log_string[192];
	va_list aptr;
	va_start(aptr, format);
	vsnprintf(log_string, sizeof(log_string), format, aptr);
	va_end(aptr);

	printf("%s>%s\n", info_string, log_string);
}
