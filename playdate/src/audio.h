#pragma once

#include "pd_api.h"
#include <stddef.h>
#include <stdint.h>

void jd_audio_init(PlaydateAPI* playdate);
int jd_audio_configure(uint32_t sample_rate, uint8_t sample_format);
void jd_audio_shutdown(void);
void jd_audio_reset(void);
void jd_audio_set_paused(int paused);
size_t jd_audio_push_pcm(const uint8_t* pcm, size_t length, uint64_t timestamp_us);
uint32_t jd_audio_queued_samples(void);
int jd_audio_ready(void);
uint32_t jd_audio_underruns(void);
uint32_t jd_audio_dropped_samples(void);
uint64_t jd_audio_playhead_us(void);

#ifdef JD_AUDIO_TEST
void jd_audio_test_set_clock(uint64_t timestamp_us, uint32_t samples);
#endif
