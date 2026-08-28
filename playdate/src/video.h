#pragma once

#include "pd_api.h"
#include <stddef.h>
#include <stdint.h>

void jd_video_init(PlaydateAPI* playdate);
int jd_video_configure(uint16_t width, uint16_t height);
size_t jd_video_frame_size(void);
int jd_video_apply_delta(
    uint8_t* frame,
    size_t frame_length,
    const uint8_t* delta,
    size_t delta_length
);
void jd_video_render_packed(const uint8_t* packed_frame);
void jd_video_reset_queue(void);
void jd_video_queue_packed(const uint8_t* packed_frame, uint64_t timestamp_us);
int jd_video_present_for_time(uint64_t playhead_us);
void jd_video_redraw_last_frame(void);
uint32_t jd_video_queued_frames(void);
uint32_t jd_video_dropped_frames(void);
