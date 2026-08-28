#include "audio.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint8_t sample = 127;
    jd_audio_test_set_clock(5 * 1000000ULL, 44100);
    assert(jd_audio_playhead_us() == 6 * 1000000ULL);

    jd_audio_test_set_clock(0, 44100U * 60U * 60U * 3U);
    assert(jd_audio_playhead_us() == 3ULL * 60ULL * 60ULL * 1000000ULL);

    assert(jd_audio_configure(8000, 2));
    assert(jd_audio_push_pcm(&sample, 1, 7000000ULL) == 1);
    assert(jd_audio_queued_samples() == 5);
    assert(jd_audio_playhead_us() == 7000000ULL);

    puts("audio_test: ok");
    return 0;
}
