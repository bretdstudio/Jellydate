#include "audio.h"

#include <string.h>

#define JD_AUDIO_OUTPUT_RATE 44100
#define JD_AUDIO_RING_CAPACITY 131072
#define JD_AUDIO_RING_MASK (JD_AUDIO_RING_CAPACITY - 1)
#define JD_AUDIO_START_THRESHOLD 8820
#define JD_AUDIO_FORMAT_S16LE 1
#define JD_AUDIO_FORMAT_S8 2

static PlaydateAPI* pd;
static SoundSource* source;
static int16_t ring[JD_AUDIO_RING_CAPACITY];
static volatile uint32_t read_index;
static volatile uint32_t write_index;
static volatile uint32_t underrun_count;
static volatile uint32_t dropped_sample_count;
static volatile uint32_t consumed_samples;
static uint64_t base_timestamp_us;
static volatile int has_timestamp;
static volatile int paused = 1;
static int started;
static uint32_t wire_sample_rate = 22050;
static uint8_t wire_sample_format = JD_AUDIO_FORMAT_S16LE;
static uint32_t resample_accumulator;

static int render_audio(void* context, int16_t* left, int16_t* right, int len) {
    uint32_t read;
    uint32_t write;
    uint32_t available;
    int index;
    (void)context;
    (void)right;

    if (paused || len <= 0) return 0;
    read = read_index;
    write = write_index;
    available = write - read;
    if (!started) {
        if (available < JD_AUDIO_START_THRESHOLD) return 0;
        started = 1;
    }
    if (available < (uint32_t)len) {
        underrun_count += 1;
        started = 0;
        return 0;
    }

    for (index = 0; index < len; index += 1) {
        left[index] = ring[(read + (uint32_t)index) & JD_AUDIO_RING_MASK];
    }
    read_index = read + (uint32_t)len;
    consumed_samples += (uint32_t)len;
    return 1;
}

void jd_audio_init(PlaydateAPI* playdate) {
    pd = playdate;
    jd_audio_reset();
    source = playdate->sound->addSource(render_audio, NULL, 0);
}

int jd_audio_configure(uint32_t sample_rate, uint8_t sample_format) {
    if (sample_rate < 8000 || sample_rate > JD_AUDIO_OUTPUT_RATE ||
        (sample_format != JD_AUDIO_FORMAT_S16LE && sample_format != JD_AUDIO_FORMAT_S8)) {
        return 0;
    }
    wire_sample_rate = sample_rate;
    wire_sample_format = sample_format;
    jd_audio_reset();
    return 1;
}

void jd_audio_shutdown(void) {
    paused = 1;
    if (source != NULL) {
        pd->sound->removeSource(source);
        pd->system->realloc(source, 0);
        source = NULL;
    }
}

void jd_audio_reset(void) {
    read_index = 0;
    write_index = 0;
    consumed_samples = 0;
    base_timestamp_us = 0;
    has_timestamp = 0;
    started = 0;
    resample_accumulator = 0;
}

void jd_audio_set_paused(int should_pause) {
    paused = should_pause ? 1 : 0;
}

size_t jd_audio_push_pcm(const uint8_t* pcm, size_t length, uint64_t timestamp_us) {
    uint32_t read = read_index;
    uint32_t write = write_index;
    uint32_t used = write - read;
    uint32_t free_samples = used < JD_AUDIO_RING_CAPACITY
        ? JD_AUDIO_RING_CAPACITY - used : 0;
    size_t bytes_per_sample = wire_sample_format == JD_AUDIO_FORMAT_S8 ? 1 : 2;
    size_t input_samples = length / bytes_per_sample;
    size_t accepted = 0;

    if (!has_timestamp && input_samples > 0) {
        base_timestamp_us = timestamp_us;
        has_timestamp = 1;
    }
    while (accepted < input_samples) {
        size_t offset = accepted * bytes_per_sample;
        uint32_t next_accumulator = resample_accumulator + JD_AUDIO_OUTPUT_RATE;
        uint32_t output_samples = next_accumulator / wire_sample_rate;
        int16_t sample;
        uint32_t output;
        next_accumulator %= wire_sample_rate;
        if (free_samples < output_samples) break;
        if (wire_sample_format == JD_AUDIO_FORMAT_S8) {
            sample = (int16_t)((int8_t)pcm[offset]) * 256;
        } else {
            sample = (int16_t)((uint16_t)pcm[offset] | ((uint16_t)pcm[offset + 1] << 8));
        }
        for (output = 0; output < output_samples; output += 1) {
            ring[write & JD_AUDIO_RING_MASK] = sample;
            write += 1;
        }
        free_samples -= output_samples;
        resample_accumulator = next_accumulator;
        accepted += 1;
    }
    write_index = write;
    if (accepted < input_samples) {
        dropped_sample_count += (uint32_t)(input_samples - accepted);
    }
    return accepted;
}

uint32_t jd_audio_queued_samples(void) {
    return write_index - read_index;
}

int jd_audio_ready(void) {
    return jd_audio_queued_samples() >= JD_AUDIO_START_THRESHOLD;
}

uint32_t jd_audio_underruns(void) {
    return underrun_count;
}

uint32_t jd_audio_dropped_samples(void) {
    return dropped_sample_count;
}

uint64_t jd_audio_playhead_us(void) {
    if (!has_timestamp) return 0;
    return base_timestamp_us + (uint64_t)consumed_samples * 1000000 / JD_AUDIO_OUTPUT_RATE;
}

#ifdef JD_AUDIO_TEST
void jd_audio_test_set_clock(uint64_t timestamp_us, uint32_t samples) {
    base_timestamp_us = timestamp_us;
    consumed_samples = samples;
    has_timestamp = 1;
}
#endif
