#pragma once

#include "pd_api.h"
#include <stdint.h>

#define JD_HOME_MAX_ITEMS 8
#define JD_DETAIL_ARTWORK_WIDTH 96
#define JD_DETAIL_ARTWORK_HEIGHT 144
#define JD_DETAIL_ARTWORK_BYTES \
    ((JD_DETAIL_ARTWORK_WIDTH / 8) * JD_DETAIL_ARTWORK_HEIGHT)
#define JD_TEXT_ENTRY_CHARACTERS "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:~/+=@"
#define JD_TEXT_ENTRY_CHARACTER_COUNT ((int)(sizeof(JD_TEXT_ENTRY_CHARACTERS) - 1))
#define JD_TEXT_ENTRY_CLEAR_INDEX JD_TEXT_ENTRY_CHARACTER_COUNT
#define JD_TEXT_ENTRY_CANCEL_INDEX (JD_TEXT_ENTRY_CHARACTER_COUNT + 1)
#define JD_TEXT_ENTRY_DONE_INDEX (JD_TEXT_ENTRY_CHARACTER_COUNT + 2)
#define JD_TEXT_ENTRY_ITEM_COUNT (JD_TEXT_ENTRY_CHARACTER_COUNT + 3)

typedef struct {
    char id[64];
    char title[96];
    char subtitle[96];
    uint64_t position_ms;
    uint64_t duration_ms;
} JDHomeItem;

typedef struct {
    char title[96];
    char subtitle[96];
    char overview[512];
    uint64_t position_ms;
    uint64_t duration_ms;
} JDItemDetails;

typedef enum {
    JD_ARTWORK_LOADING,
    JD_ARTWORK_READY,
    JD_ARTWORK_MISSING
} JDArtworkState;

typedef struct {
    JDArtworkState state;
    uint8_t packed[JD_DETAIL_ARTWORK_BYTES];
} JDItemArtwork;

void jd_ui_init(PlaydateAPI* playdate);
void jd_ui_shutdown(void);
void jd_ui_draw_boot(void);
void jd_ui_draw_tuning(const char* detail);
void jd_ui_draw_menu(int selected);
void jd_ui_draw_setup(
    const char* host, size_t token_length, int selected,
    const char* status, int can_cancel
);
void jd_ui_draw_text_entry(const char* label, const char* text, int selected);
void jd_ui_draw_setup_testing(const char* host);
void jd_ui_draw_alpha_index(const char* heading, int selected);
void jd_ui_draw_catalog(
    const char* heading,
    const JDHomeItem* items,
    int count,
    int selected,
    int loading
);
void jd_ui_draw_details(
    const JDItemDetails* details,
    const JDItemArtwork* artwork,
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
