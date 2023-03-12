#ifndef ALSA_PLAY_H
#define ALSA_PLAY_H
#include <stdint.h>

int alsa_play(void);
int alsa_init(char *device_name, char *wav_file, int period);
void alsa_deinit(void);
int alsa_update(void);

// uint64_t micros();
// void log_main(const char * format, ...);
// void sleepMicros(uint32_t micros);

#endif /* ALSA_PLAY_H */
