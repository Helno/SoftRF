/*
 * Sound.h — stub for platforms without audio (e.g. RPi)
 */
#ifndef SOFTRF_SOUND_H
#define SOFTRF_SOUND_H

#include <stdint.h>

extern void Sound_test(int var);
extern void Sound_tone(int hz, uint8_t volume);
extern void Sound_loop(void);

#endif /* SOFTRF_SOUND_H */
