#include "video.h"

#include <string.h>

#define DISPLAY_ROWS 240
#define DISPLAY_COLUMNS 400
#define DISPLAY_ROW_BYTES 52
#define MAX_WIRE_ROW_BYTES 50
#define MAX_FRAME_BYTES (DISPLAY_ROWS * MAX_WIRE_ROW_BYTES)
#define VIDEO_QUEUE_CAPACITY 8
#define VIDEO_EARLY_TOLERANCE_US 16000

static PlaydateAPI* pd;
static uint8_t last_frame[MAX_FRAME_BYTES];
static uint8_t queued_frames[VIDEO_QUEUE_CAPACITY][MAX_FRAME_BYTES];
static uint64_t queued_timestamps[VIDEO_QUEUE_CAPACITY];
static uint16_t wire_width;
static uint16_t wire_height;
static uint16_t wire_row_bytes;
static size_t wire_frame_bytes;
static uint8_t horizontal_scale;
static uint8_t vertical_scale;
static uint8_t source_x_byte[DISPLAY_COLUMNS];
static uint8_t source_x_mask[DISPLAY_COLUMNS];
static uint16_t source_row_offset[DISPLAY_ROWS];
static uint32_t queue_read;
static uint32_t queue_count;
static uint32_t dropped_frames;
static int has_last_frame;

void jd_video_init(PlaydateAPI* playdate) {
    pd = playdate;
    jd_video_configure(DISPLAY_COLUMNS, DISPLAY_ROWS);
    dropped_frames = 0;
    has_last_frame = 0;
}

int jd_video_configure(uint16_t width, uint16_t height) {
    int column;
    int row;
    if (width < 8 || width > DISPLAY_COLUMNS || width % 8 != 0 ||
        height < 1 || height > DISPLAY_ROWS ||
        (uint32_t)width * DISPLAY_ROWS != (uint32_t)height * DISPLAY_COLUMNS) {
        return 0;
    }
    wire_width = width;
    wire_height = height;
    wire_row_bytes = width / 8;
    wire_frame_bytes = (size_t)wire_row_bytes * height;
    if (DISPLAY_COLUMNS % width == 0 && DISPLAY_ROWS % height == 0) {
        horizontal_scale = (uint8_t)(DISPLAY_COLUMNS / width);
        vertical_scale = (uint8_t)(DISPLAY_ROWS / height);
    } else {
        horizontal_scale = 0;
        vertical_scale = 0;
    }
    for (column = 0; column < DISPLAY_COLUMNS; column += 1) {
        uint16_t source_x = (uint16_t)((uint32_t)column * width / DISPLAY_COLUMNS);
        source_x_byte[column] = (uint8_t)(source_x >> 3);
        source_x_mask[column] = (uint8_t)(0x80 >> (source_x & 7));
    }
    for (row = 0; row < DISPLAY_ROWS; row += 1) {
        uint16_t source_y = (uint16_t)((uint32_t)row * height / DISPLAY_ROWS);
        source_row_offset[row] = (uint16_t)(source_y * wire_row_bytes);
    }
    queue_read = 0;
    queue_count = 0;
    has_last_frame = 0;
    return 1;
}

size_t jd_video_frame_size(void) {
    return wire_frame_bytes;
}

int jd_video_apply_delta(
    uint8_t* frame,
    size_t frame_length,
    const uint8_t* delta,
    size_t delta_length
) {
    size_t frame_cursor = 0;
    size_t delta_cursor = 0;
    while (delta_cursor < delta_length) {
        uint16_t skip_length;
        uint16_t control;
        uint16_t literal_length;
        int repeated;
        if (delta_length - delta_cursor < 4) return 0;
        skip_length = (uint16_t)(((uint16_t)delta[delta_cursor] << 8) |
                                 delta[delta_cursor + 1]);
        control = (uint16_t)(((uint16_t)delta[delta_cursor + 2] << 8) |
                             delta[delta_cursor + 3]);
        repeated = (control & 0x8000) != 0;
        literal_length = control & 0x7fff;
        delta_cursor += 4;
        if (skip_length > frame_length - frame_cursor) return 0;
        frame_cursor += skip_length;
        if (literal_length == 0 || literal_length > frame_length - frame_cursor ||
            (repeated ? 1 : literal_length) > delta_length - delta_cursor) return 0;
        if (repeated) {
            memset(frame + frame_cursor, delta[delta_cursor], literal_length);
            delta_cursor += 1;
        } else {
            memcpy(frame + frame_cursor, delta + delta_cursor, literal_length);
            delta_cursor += literal_length;
        }
        frame_cursor += literal_length;
    }
    return 1;
}

static uint16_t expand_byte_2x(uint8_t value) {
    uint16_t result = 0;
    int bit;
    for (bit = 0; bit < 8; bit += 1) {
        if (value & (0x80 >> bit)) result |= (uint16_t)(0xC000 >> (bit * 2));
    }
    return result;
}

static void render_packed(const uint8_t* packed_frame) {
    uint8_t* framebuffer = pd->graphics->getFrame();
    int row;
    if (horizontal_scale == 0 || vertical_scale == 0) {
        for (row = 0; row < DISPLAY_ROWS; row += 1) {
            const uint8_t* source = packed_frame + source_row_offset[row];
            uint8_t* destination = framebuffer + row * DISPLAY_ROW_BYTES;
            int column;
            memset(destination, 0, DISPLAY_ROW_BYTES);
            for (column = 0; column < DISPLAY_COLUMNS; column += 1) {
                if (source[source_x_byte[column]] & source_x_mask[column]) {
                    destination[column >> 3] |= (uint8_t)(0x80 >> (column & 7));
                }
            }
        }
        pd->graphics->markUpdatedRows(0, DISPLAY_ROWS - 1);
        return;
    }
    for (row = 0; row < wire_height; row += 1) {
        const uint8_t* source = packed_frame + row * wire_row_bytes;
        uint8_t* destination = framebuffer + row * vertical_scale * DISPLAY_ROW_BYTES;
        int output_row;
        if (horizontal_scale == 1) {
            memcpy(destination, source, wire_row_bytes);
        } else {
            int column;
            for (column = 0; column < wire_row_bytes; column += 1) {
                uint16_t expanded = expand_byte_2x(source[column]);
                destination[column * 2] = (uint8_t)(expanded >> 8);
                destination[column * 2 + 1] = (uint8_t)expanded;
            }
        }
        destination[50] = 0;
        destination[51] = 0;
        for (output_row = 1; output_row < vertical_scale; output_row += 1) {
            memcpy(destination + output_row * DISPLAY_ROW_BYTES, destination, DISPLAY_ROW_BYTES);
        }
    }
    pd->graphics->markUpdatedRows(0, DISPLAY_ROWS - 1);
}

void jd_video_render_packed(const uint8_t* packed_frame) {
    if (packed_frame != last_frame) memcpy(last_frame, packed_frame, wire_frame_bytes);
    has_last_frame = 1;
    render_packed(last_frame);
}

void jd_video_reset_queue(void) {
    queue_read = 0;
    queue_count = 0;
}

void jd_video_queue_packed(const uint8_t* packed_frame, uint64_t timestamp_us) {
    uint32_t write;
    if (queue_count == VIDEO_QUEUE_CAPACITY) {
        queue_read = (queue_read + 1) % VIDEO_QUEUE_CAPACITY;
        queue_count -= 1;
        dropped_frames += 1;
    }
    write = (queue_read + queue_count) % VIDEO_QUEUE_CAPACITY;
    memcpy(queued_frames[write], packed_frame, wire_frame_bytes);
    queued_timestamps[write] = timestamp_us;
    queue_count += 1;
}

int jd_video_present_for_time(uint64_t playhead_us) {
    uint32_t eligible = 0;
    uint32_t selected;
    while (eligible < queue_count) {
        uint32_t index = (queue_read + eligible) % VIDEO_QUEUE_CAPACITY;
        if (queued_timestamps[index] > playhead_us + VIDEO_EARLY_TOLERANCE_US) break;
        eligible += 1;
    }
    if (eligible == 0) return 0;
    selected = (queue_read + eligible - 1) % VIDEO_QUEUE_CAPACITY;
    memcpy(last_frame, queued_frames[selected], wire_frame_bytes);
    has_last_frame = 1;
    if (eligible > 1) dropped_frames += eligible - 1;
    queue_read = (queue_read + eligible) % VIDEO_QUEUE_CAPACITY;
    queue_count -= eligible;
    render_packed(last_frame);
    return 1;
}

void jd_video_redraw_last_frame(void) {
    if (has_last_frame) render_packed(last_frame);
}

uint32_t jd_video_queued_frames(void) {
    return queue_count;
}

uint32_t jd_video_dropped_frames(void) {
    return dropped_frames;
}
