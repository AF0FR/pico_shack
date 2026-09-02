#pragma once

#include <stdint.h>

#define AUDIO_ENVELOPE_MS 5u

void audio_init(void);
void audio_start_tone(uint32_t frequency_hz);
void audio_stop(void);
void audio_play_warble(void);
void audio_play_sweep(void);
