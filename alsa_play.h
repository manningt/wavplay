#ifndef ALSA_PLAY_H
#define ALSA_PLAY_H
#include <stdint.h>
#include <stdbool.h>

int alsa_play(void);
int alsa_init(char *device_name, int period);
void alsa_deinit(void);
int alsa_update(void);
int read_wav_file(char *wav_file, uint8_t buffer_number);

extern bool queue_mode;
extern bool wav_buffer_ready;
extern bool wav_buffer_done;

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
