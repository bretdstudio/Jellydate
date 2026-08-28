#include "video.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FRAME_BYTES 12000
#define DISPLAY_BYTES (52 * 240)

static uint8_t framebuffer[DISPLAY_BYTES];
static int marked;

static uint8_t* get_frame(void) {
    return framebuffer;
}

static void mark_rows(int start, int end) {
    assert(start == 0);
    assert(end == 239);
    marked += 1;
}

int main(void) {
    struct playdate_graphics graphics = {0};
    PlaydateAPI api = {0};
    uint8_t frame[FRAME_BYTES];
    int index;

    graphics.getFrame = get_frame;
    graphics.markUpdatedRows = mark_rows;
    api.graphics = &graphics;
    jd_video_init(&api);

    memset(frame, 0x11, sizeof(frame));
    jd_video_queue_packed(frame, 0);
    memset(frame, 0x22, sizeof(frame));
    jd_video_queue_packed(frame, 33333);
    memset(frame, 0x33, sizeof(frame));
    jd_video_queue_packed(frame, 66666);

    assert(jd_video_queued_frames() == 3);
    assert(jd_video_present_for_time(50000) == 1);
    assert(framebuffer[0] == 0x22);
    assert(jd_video_queued_frames() == 1);
    assert(jd_video_dropped_frames() == 1);
    assert(jd_video_present_for_time(50000) == 0);
    assert(jd_video_present_for_time(70000) == 1);
    assert(framebuffer[0] == 0x33);
    assert(marked == 2);

    jd_video_reset_queue();
    for (index = 0; index < 10; index += 1) {
        memset(frame, index, sizeof(frame));
        jd_video_queue_packed(frame, (uint64_t)index * 33333);
    }
    assert(jd_video_queued_frames() == 8);
    assert(jd_video_dropped_frames() == 3);

    assert(jd_video_configure(200, 120));
    assert(jd_video_frame_size() == 3000);
    memset(framebuffer, 0, sizeof(framebuffer));
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x80;
    jd_video_render_packed(frame);
    assert(framebuffer[0] == 0xc0);
    assert(framebuffer[52] == 0xc0);
    assert(framebuffer[1] == 0x00);

    assert(jd_video_configure(240, 144));
    assert(jd_video_frame_size() == 4320);
    memset(framebuffer, 0, sizeof(framebuffer));
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x80;
    jd_video_render_packed(frame);
    assert(framebuffer[0] == 0xc0);
    assert(framebuffer[52] == 0xc0);
    assert(framebuffer[104] == 0x00);

    {
        uint8_t decoded[16] = {0};
        const uint8_t delta[] = {
            0x00, 0x02, 0x00, 0x03, 0xaa, 0xbb, 0xcc,
            0x00, 0x04, 0x00, 0x01, 0xdd
        };
        const uint8_t malformed[] = {0x00, 0x10, 0x00, 0x01, 0xff};
        const uint8_t repeated[] = {0x00, 0x01, 0x80, 0x06, 0xee};
        assert(jd_video_apply_delta(decoded, sizeof(decoded), delta, sizeof(delta)));
        assert(decoded[2] == 0xaa);
        assert(decoded[3] == 0xbb);
        assert(decoded[4] == 0xcc);
        assert(decoded[9] == 0xdd);
        assert(jd_video_apply_delta(
            decoded, sizeof(decoded), repeated, sizeof(repeated)
        ));
        assert(decoded[1] == 0xee);
        assert(decoded[6] == 0xee);
        assert(!jd_video_apply_delta(
            decoded, sizeof(decoded), malformed, sizeof(malformed)
        ));
    }

    puts("video_test: ok");
    return 0;
}
