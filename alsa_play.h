#ifndef ALSA_PLAY_H
#define ALSA_PLAY_H
#include <stdint.h>

int alsa_play(void);
int alsa_init(char *device_name, int period);
void alsa_deinit(void);
int alsa_update(void);
int read_wav_file(char *wav_file, uint8_t buffer_number);

extern uint8_t wav_buffer_available;

typedef struct format_chunk {
   int file_size;
   short audio_format; //PCM = 1
   short number_of_channels;	//Mono = 1, Stereo = 2, etc.
   int sample_rate; //8000, 44100, etc.
   int byte_rate; //== SampleRate * NumChannels * BitsPerSample/8
   short block_align; //== NumChannels * BitsPerSample/8
   short bits_per_sample; //8 bits = 8, 16 bits = 16, etc.
} format_chunk_t;

typedef struct data_chunk {
   int chunk_size; //== NumSamples * NumChannels * BitsPerSample/8
   char* data;		 //The actual sound data.
} data_chunk_t;

#define FOREACH_BUFFER_TYPE(BUFFER_TYPE_NAME) \
	BUFFER_TYPE_NAME(FILL) \
	BUFFER_TYPE_NAME(FIRST) \
	BUFFER_TYPE_NAME(SECOND) \
	BUFFER_TYPE_NAME(THIRD)

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

enum BUFFER_TYPE_ENUM {
    FOREACH_BUFFER_TYPE(GENERATE_ENUM)
};

uint64_t micros();
void log_main(const char * format, ...);
void sleepMicros(uint32_t micros);

#endif /* ALSA_PLAY_H */
