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

// #define U8
#ifdef U8
	#define BITS_PER_SAMPLE 8
	#define PCM_FORMAT SND_PCM_FORMAT_U8
#else
	#define BITS_PER_SAMPLE 16
	#define PCM_FORMAT SND_PCM_FORMAT_S16
#endif

#define NUM_CHANNELS 1
#define WAV_SAMPLE_RATE 11025
#define FRAME_SIZE (BITS_PER_SAMPLE/8 * NUM_CHANNELS)
#define PERIOD_SIZE 160 //a period size of 120 caused pipe errors
// data rate = channels * bits_per_sample/8 * sample_rate * BITS_PER_SAMPLE/2 = 22050 (S16 format)
// ?? interrupt period = period size * (BITS_PER_SAMPLE/8 * NUM_CHANNELS) / data rate =120*2/22050 = 2.721 millis
#define ALSA_BUFFER_SIZE_MULTIPLIER 20 //has to be at least 20
#define IDLE_FRAMES_AVAILABLE (PERIOD_SIZE * ALSA_BUFFER_SIZE_MULTIPLIER)

static snd_pcm_t *pcm_handle;
#define NUMBER_OF_BUFFERS 4
#define BUFFER_SIZE 200000
uint32_t wav_data_size[NUMBER_OF_BUFFERS]= {0};
uint32_t wav_data_start[NUMBER_OF_BUFFERS]= {0};
char wav_buff[NUMBER_OF_BUFFERS][BUFFER_SIZE]= {0};

bool wav_buffer_ready;
bool wav_buffer_done;
bool queue_mode= false;

#define FILE_PARSE_LOGGING 0

int alsa_update()
{
	int ret;
	int avail_buffs;
	bool err = false;
	snd_pcm_sframes_t frames_requested, frames_written;
	uint32_t frames_written_count=0;
	uint8_t loop_count=0;

	enum prev_curr {PREVIOUS, CURRENT};
	enum buff_state {READY, IN_PROGRESS, DONE};
	static bool wav_buffer[2][3]= {0};
	wav_buffer[CURRENT][READY]= wav_buffer_ready;
	wav_buffer[CURRENT][DONE]= wav_buffer_done;

	static uint32_t current_buffer_position[4]= {0};
	static uint32_t call_count=0;
	static uint32_t total_frames_written_count=0;
	static uint8_t buffer_being_written=0;
	uint32_t previous_buffer_position;
	static uint8_t previous_buffer_being_written=0;
	static bool done= false;

	if (call_count == 0)
	{
		buffer_being_written= FILL;
		wav_data_size[0]= PERIOD_SIZE * FRAME_SIZE * 8; //reduce silence or grey to 44 mSec
		if (!queue_mode)
			for (loop_count=0; loop_count < NUMBER_OF_BUFFERS; loop_count++)
				current_buffer_position[loop_count]= wav_data_start[loop_count];
	}

	if (queue_mode && wav_buffer_ready && !wav_buffer[CURRENT][IN_PROGRESS] && !wav_buffer_done)
	{
		current_buffer_position[1]= wav_data_start[1];
		buffer_being_written= 1;
		wav_buffer[CURRENT][IN_PROGRESS]= true;
		wav_buffer_ready= false;
		if (0)
			log_main("starting buffer=%u on call_count=%u", buffer_being_written, call_count);
	}

	ret = snd_pcm_wait(pcm_handle, 1000); // returns 1 normally
	if (ret == 0)
	{
		log_main("PCM timeout occurred");
		return -1;
	}
	else if (ret < 0)
	{
		log_main("PCM device error: %s", snd_strerror(ret));
		return ret;
	}

	avail_buffs = snd_pcm_avail(pcm_handle);
	while (avail_buffs >= (IDLE_FRAMES_AVAILABLE - (2 * PERIOD_SIZE))) 
	{
		frames_requested = snd_pcm_avail_update(pcm_handle);
		if (frames_requested < 0)
		{
			fprintf(stderr, "PCM error requesting frames: %s\n",
					  snd_strerror(ret));
			err = true;
			return -1;
		}
		frames_requested= (frames_requested > PERIOD_SIZE) ? PERIOD_SIZE : frames_requested;

		char *wav_buff_ptr= wav_buff[buffer_being_written]+current_buffer_position[buffer_being_written];
		frames_written = snd_pcm_writei(pcm_handle, wav_buff_ptr, frames_requested);

		if (frames_written == -EAGAIN)
			continue;
		else if (frames_written == -EPIPE)
		{
			log_main("PCM write error: Underrun event");
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
			log_main("PCM write error: Stream is suspended");
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
			current_buffer_position[buffer_being_written] += (frames_written * FRAME_SIZE);
			if (current_buffer_position[buffer_being_written] >= wav_data_size[buffer_being_written])
			{
				previous_buffer_position= current_buffer_position[buffer_being_written];
				current_buffer_position[buffer_being_written]= wav_data_start[buffer_being_written];
				if (queue_mode)
				{ 
					if (buffer_being_written != FILL)
					{
						log_main("buffer=%u done on call_count=%u", buffer_being_written, call_count);
						buffer_being_written= FILL;
						wav_buffer[CURRENT][IN_PROGRESS]= false;
						wav_buffer_done= true;
					}
				} else
				{
					if (!done)
						buffer_being_written++;
					if (wav_data_size[buffer_being_written] == 0)
					{
						buffer_being_written= FILL;
						done= true;
					}
				}
			}
			// the following needs to be fixed to be useful, since the info was changed before this printout
			if (0 && previous_buffer_being_written != buffer_being_written)
			{
				log_main("calls=%u frames: written=%u total=%u avail=%u  buffer: writing=%u prev_ending_position=%d",
					call_count, frames_written, total_frames_written_count, avail_buffs, buffer_being_written,
					previous_buffer_position);
			}
			previous_buffer_being_written= buffer_being_written;
		}

		frames_written_count += frames_written;

		avail_buffs = snd_pcm_avail(pcm_handle);
		if (0 && (wav_buffer[CURRENT][READY] != wav_buffer[PREVIOUS][READY] ||
			wav_buffer[CURRENT][IN_PROGRESS] != wav_buffer[PREVIOUS][IN_PROGRESS] ||
			wav_buffer[CURRENT][DONE] != wav_buffer[PREVIOUS][DONE] ))
				log_main("calls=%d  loops=%d frames_written=%d avail=%d buff=%u ready=%u in_prog=%u done=%u",
					call_count, loop_count, frames_written_count, avail_buffs, buffer_being_written,
					wav_buffer[CURRENT][READY], wav_buffer[CURRENT][IN_PROGRESS], wav_buffer[CURRENT][DONE]);
		wav_buffer[PREVIOUS][READY]= wav_buffer[CURRENT][READY];
		wav_buffer[PREVIOUS][DONE]= wav_buffer[CURRENT][DONE];
		wav_buffer[PREVIOUS][IN_PROGRESS]= wav_buffer[CURRENT][IN_PROGRESS];
		// log_main("call_count=%d  loop_count=%d frames_count=%d avail=%d",
		// 	call_count, loop_count, frames_written_count, avail_buffs);
		loop_count += 1;
	}
	
	call_count++;
	total_frames_written_count += frames_written_count;
	// log_main("calls=%u frames: written=%u total=%u avail=%u  buffer: writing=%u ending_position=%d", // state=%s",
   // 	call_count, frames_written, total_frames_written_count, avail_buffs, buffer_being_written,
	// 	current_wav_position[buffer_being_written]); //, snd_pcm_state_name(snd_pcm_state(pcm_handle)));

	return err;
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

	if (0)
	{
		ret = snd_pcm_sw_params_get_start_threshold(params, &threshold);
		if (ret)
		{
			fprintf(stderr, "Couldn't get start threshold: %s\n", snd_strerror(ret));
			return ret;
		}
		printf("Start threshold is %lu frames\n", threshold);
	}

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

	ret = snd_pcm_hw_params_set_format(handle, params, PCM_FORMAT);
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
	unsigned int requested_rate= WAV_SAMPLE_RATE;
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

	buffer_size = (period < 0) ? (ALSA_BUFFER_SIZE_MULTIPLIER * PERIOD_SIZE) : (ALSA_BUFFER_SIZE_MULTIPLIER * period);
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

int32_t parse_wav_header(uint8_t buffer_number)
{
	// wav format: https://www.videoproc.com/resource/wav-file.htm
	// there are potentially 1 or more chunks between the WAVEfmt chunk and the data chunk: LIST and fact
	uint32_t i, j, k;
	char chunkID[4][5]= {0}; //using 1 based, so chunkID[0] is unused
	uint32_t file_size= 0;
	uint32_t chunk_size= 0;
	uint16_t audio_format= 0; //PCM = 1
   uint16_t number_of_channels= 0;
   uint32_t sample_rate= 0; //check for 11025
   // int byte_rate; //== SampleRate * NumChannels * BitsPerSample/8
   // uint16_t block_align; //== NumChannels * BitsPerSample/8
   uint16_t bits_per_sample= 0; //8 bits = 8, 16 bits = 16, etc.
   uint32_t chunk_offset[4]= {0}; //0&1 are not used, accumulate chunk_offsets for chunk 2 & 3

	for (i= 0 ; i < 4 ; i++)
		chunkID[1][i]= wav_buff[buffer_number][i];

	if (strncmp(chunkID[1], "RIFF", 4) != 0)
	{
		log_main("Not a WAV file: first 4 characters=%s instead of RIFF", chunkID);
		return -1;
	}
	
	// get file_size, fmt, channels, rate, bits_per_sample
	for (i= 7 ; i >= 4 ; i--)
		file_size= (file_size << 8) + wav_buff[buffer_number][i];

	for (i= 19 ; i >= 16 ; i--)
		chunk_offset[2]= (chunk_offset[2] << 8) + wav_buff[buffer_number][i];
	chunk_offset[2] += (8 + 12) ; //add in the chunkID and the size bytes plus the current offset

	for (i= 21 ; i >= 20 ; i--)
		audio_format= (audio_format << 8) + wav_buff[buffer_number][i];

	for (i= 23 ; i >= 22 ; i--)
		number_of_channels= (number_of_channels << 8) + wav_buff[buffer_number][i];

	for (i= 27 ; i >= 24 ; i--)
		sample_rate= (sample_rate << 8) + wav_buff[buffer_number][i];

	for (i= 35 ; i >= 34 ; i--)
		bits_per_sample= (bits_per_sample << 8) + wav_buff[buffer_number][i];

	// got through the chunks looking for the 'data' chunk
	for (k= 0; k < 5; k++)
	{
		//accumulate chunk ID
		j=0;
		for (i= chunk_offset[2] ; i < chunk_offset[2]+4 ; i++)
			chunkID[2][j++]= wav_buff[buffer_number][i];

		//accumulate chunk size
		for (i= chunk_offset[2]+7; i >= chunk_offset[2]+4 ; i--)  //equivalent to: for (i= 43 ; i >= 40 ; i--)
			chunk_size= (chunk_size << 8) + wav_buff[buffer_number][i];

		// log_main("chunk_offset[2]=0x%x chunkID2=%s chunk_size=0x%x", chunk_offset[2], chunkID[2], chunk_size);
		// check if data chunk:
		if (strncmp(chunkID[2], "data", 4) == 0)
		{
			wav_data_size[buffer_number]= chunk_size;
			wav_data_start[buffer_number]= chunk_offset[2]+8;
			break;
		} else 
			chunk_offset[2] += (chunk_size + 8); // advance to next chunk offset
	}
	if (wav_data_size[buffer_number] == 0)
	{
		log_main("WAV parse error: didn't find sound data");
		return -1;
	}

	if (0) 
		log_main("file_size=%u format=%d chan=%u rate=%d bits=%d data_size=%d data_start=0x%x", 
			file_size, audio_format, number_of_channels, sample_rate, bits_per_sample, wav_data_size[buffer_number],
			wav_data_start[buffer_number]);

	if (audio_format != 1 || number_of_channels != NUM_CHANNELS || sample_rate != WAV_SAMPLE_RATE || bits_per_sample != BITS_PER_SAMPLE)
	{
		log_main("Unsupported wav format: format=%d channels=%u sample_rate=%d bits_per_sample=%d", 
			audio_format, number_of_channels, sample_rate, bits_per_sample);
		return -1;
	}
	return wav_data_size[buffer_number];
}

int read_wav_file(char *wav_file, uint8_t buffer_number)
{
	uint64_t micros_at_start = micros();
	uint32_t bytes_read;
	int32_t data_size;
	
	FILE *wav_fd = fopen(wav_file, "rb");
	if (wav_fd == NULL)
	{
		log_main("Error opening file=%s\n", wav_file);
		return -1;
	}

	fseek(wav_fd, 0, SEEK_END);
	wav_data_size[buffer_number] = ftell(wav_fd);
	rewind(wav_fd);
	if (wav_data_size[buffer_number] >= BUFFER_SIZE)
	{
		log_main("Error: file=%s larger than buffer; file_size=%u buffer_size=%u",
			wav_file, wav_data_size[buffer_number], BUFFER_SIZE);
		fclose(wav_fd);
		return -1;
	}

	bytes_read= fread(wav_buff[buffer_number], 1, wav_data_size[buffer_number], wav_fd);
	if (bytes_read != wav_data_size[buffer_number])
	{
		log_main("Error reading file=%s: file_size=%u bytes_read=%u reading wave file, bytes_read=",
			wav_file, wav_data_size[buffer_number], bytes_read);
		fclose(wav_fd);
		return -1;
	}
	fclose(wav_fd);

	data_size= parse_wav_header(buffer_number);
	if (data_size <= 0)
		return -1;
	
	uint32_t remainder= data_size % (PERIOD_SIZE * FRAME_SIZE);
	// uint32_t bytes_to_add= 0;
	// if (remainder != 0)
	// {
	// 	bytes_to_add= (PERIOD_SIZE * FRAME_SIZE) - remainder; //round up to the nearest buffer size
	// 	memset(wav_buff[buffer_number]+remainder, 0, bytes_to_add); //clear the remaining bytes in the buffer
	// }

	// !! currently truncating the file by the remainder instead of padding
	wav_data_size[buffer_number]= (uint32_t) data_size - remainder; // + bytes_to_add;
	if (FILE_PARSE_LOGGING)
		log_main("file=%s: data_start=0x%x orig_data_size=%u remainder=%u final_size=%u; read_micros=%u", //added_bytes=%u 
			wav_file, wav_data_start[buffer_number], data_size, remainder, wav_data_size[buffer_number], //bytes_to_add, 
			(uint32_t) (micros() - micros_at_start));

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
	// uint64_t before_drain_micros = micros();
	snd_pcm_drain(pcm_handle);
	// log_main("drain_micros=%llu", micros() - before_drain_micros);
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
