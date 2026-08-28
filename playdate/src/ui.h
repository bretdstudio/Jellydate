#pragma once

#include "pd_api.h"
#include <stdint.h>

#define JD_HOME_MAX_ITEMS 8

typedef struct {
    char id[64];
    char title[96];
    char subtitle[96];
    uint64_t position_ms;
    uint64_t duration_ms;
} JDHomeItem;

void jd_ui_init(PlaydateAPI* playdate);
void jd_ui_shutdown(void);
void jd_ui_draw_tuning(const char* detail);
void jd_ui_draw_menu(int selected);
void jd_ui_draw_catalog(
    const char* heading,
    const JDHomeItem* items,
    int count,
    int selected,
    int loading
);
void jd_ui_draw_error(const char* detail);
void jd_ui_draw_paused_overlay(
    const char* title, uint64_t position_ms, uint64_t duration_ms,
    uint16_t video_x, uint16_t video_y, uint16_t video_width, uint16_t video_height
);
void jd_ui_draw_scrub_overlay(
    uint64_t position_ms, uint64_t duration_ms,
    uint16_t video_x, uint16_t video_y, uint16_t video_width, uint16_t video_height
);
void jd_ui_draw_buffering_overlay(
    uint64_t position_ms, uint64_t duration_ms,
    uint16_t video_x, uint16_t video_y, uint16_t video_width, uint16_t video_height
);
void jd_ui_draw_ended(void);
