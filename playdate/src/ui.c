#include "ui.h"

#include <stdio.h>
#include <string.h>

static PlaydateAPI* pd;
static LCDBitmap* scaled_text_bitmap;

#define TITLE_TEXT_SCALE 1.15f
#define META_TEXT_SCALE 0.82f
#define SCALED_TEXT_BITMAP_WIDTH 480
#define SCALED_TEXT_BITMAP_HEIGHT 24

static void text_centered(const char* text, int y) {
    int width = pd->graphics->getTextWidth(NULL, text, strlen(text), kUTF8Encoding, 0);
    pd->graphics->drawText(text, strlen(text), kUTF8Encoding, (400 - width) / 2, y);
}

static void antenna(int x, int y) {
    pd->graphics->drawLine(x, y, x - 13, y - 18, 2, kColorBlack);
    pd->graphics->drawLine(x, y, x + 13, y - 18, 2, kColorBlack);
    pd->graphics->drawEllipse(x - 4, y - 4, 8, 8, 2, 0, 360, kColorBlack);
    pd->graphics->drawLine(x, y + 5, x, y + 20, 2, kColorBlack);
}

static void format_time(char* output, size_t capacity, uint64_t milliseconds) {
    uint64_t seconds = milliseconds / 1000;
    unsigned long hours = (unsigned long)(seconds / 3600);
    if (hours > 99) hours = 99;
    snprintf(
        output,
        capacity,
        "%02lu:%02lu:%02lu",
        hours,
        (unsigned long)((seconds / 60) % 60),
        (unsigned long)(seconds % 60)
    );
}

static void progress(int x, int y, int width, int height, uint64_t position_ms, uint64_t duration_ms) {
    int filled = duration_ms > 0 ? (int)((position_ms * (uint64_t)width) / duration_ms) : 0;
    if (filled > width) filled = width;
    pd->graphics->drawRect(x, y, width, height, kColorBlack);
    if (filled > 2 && height > 4) {
        pd->graphics->fillRect(x + 2, y + 2, filled - 2, height - 4, kColorBlack);
    }
}

static void scaled_text(const char* text, int x, int y, int width, float scale) {
    int source_width;
    if (scaled_text_bitmap == NULL || text == NULL || text[0] == '\0') return;

    source_width = (int)((float)width / scale);
    if (source_width > SCALED_TEXT_BITMAP_WIDTH) source_width = SCALED_TEXT_BITMAP_WIDTH;

    pd->graphics->clearBitmap(scaled_text_bitmap, kColorClear);
    pd->graphics->pushContext(scaled_text_bitmap);
    pd->graphics->drawTextInRect(
        text, strlen(text), kUTF8Encoding,
        0, 0, source_width, SCALED_TEXT_BITMAP_HEIGHT,
        kWrapClip, kAlignTextLeft
    );
    pd->graphics->popContext();
    pd->graphics->drawScaledBitmap(scaled_text_bitmap, x, y, scale, scale);
}

void jd_ui_init(PlaydateAPI* playdate) {
    pd = playdate;
    scaled_text_bitmap = pd->graphics->newBitmap(
        SCALED_TEXT_BITMAP_WIDTH, SCALED_TEXT_BITMAP_HEIGHT, kColorClear
    );
}

void jd_ui_shutdown(void) {
    if (scaled_text_bitmap != NULL) {
        pd->graphics->freeBitmap(scaled_text_bitmap);
        scaled_text_bitmap = NULL;
    }
}

void jd_ui_draw_tuning(const char* detail) {
    pd->graphics->clear(kColorWhite);
    text_centered("JELLYDATE", 54);
    text_centered("tuning in...", 91);
    text_centered(detail, 119);
    antenna(200, 181);
}

void jd_ui_draw_home(const JDHomeItem* items, int count, int selected, int loading) {
    int first;
    int visible;
    int index;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    pd->graphics->drawText("CONTINUE WATCHING", 17, kUTF8Encoding, 12, 35);
    if (loading) {
        text_centered("consulting the jelly oracle...", 112);
        return;
    }
    if (count <= 0) {
        text_centered("Nothing half-watched. Impressive.", 104);
        text_centered("Recently added is coming next.", 132);
        return;
    }

    first = selected >= 3 ? selected - 2 : 0;
    visible = count - first;
    if (visible > 3) visible = 3;
    for (index = 0; index < visible; index += 1) {
        const JDHomeItem* item = &items[first + index];
        int y = 55 + index * 51;
        if (first + index == selected) {
            pd->graphics->drawRect(7, y - 1, 386, 48, kColorBlack);
            pd->graphics->drawText(">", 1, kUTF8Encoding, 12, y + 5);
        }
        scaled_text(item->title, 29, y, 350, TITLE_TEXT_SCALE);
        scaled_text(item->subtitle, 29, y + 23, 350, META_TEXT_SCALE);
        progress(292, y + 40, 87, 5, item->position_ms, item->duration_ms);
    }
    pd->graphics->drawText("A: WATCH", 8, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("D-PAD / CRANK: TUNE", 19, kUTF8Encoding, 207, 222);
}

void jd_ui_draw_error(const char* detail) {
    pd->graphics->clear(kColorWhite);
    text_centered("SIGNAL LOST", 58);
    pd->graphics->drawTextInRect(
        detail, strlen(detail), kUTF8Encoding,
        40, 102, 320, 70, kWrapWord, kAlignTextCenter
    );
    text_centered("Jellyfin vanished into the static.", 190);
}

static void draw_transport_overlay(
    const char* title,
    const char* state,
    int show_title,
    uint64_t position_ms,
    uint64_t duration_ms,
    uint16_t video_x,
    uint16_t video_y,
    uint16_t video_width,
    uint16_t video_height
) {
    char left[16];
    char right[16];
    int center_x;
    int center_y;
    int top_bar;
    int bottom_start;
    int bottom_bar;
    int timeline_y;
    int title_y;
    int badge_x;
    int badge_y;
    int badge_width;
    int state_x;
    int state_width;
    int buffering_state;
    int right_width;

    if (video_width == 0 || video_height == 0 ||
        video_x + video_width > 400 || video_y + video_height > 240) {
        video_x = 0;
        video_y = 0;
        video_width = 400;
        video_height = 240;
    }
    center_x = video_x + video_width / 2;
    center_y = video_y + video_height / 2;
    top_bar = video_y;
    bottom_start = video_y + video_height;
    bottom_bar = 240 - bottom_start;

    if (show_title) {
        /* Keep the title inside the upper letterbox. For nearly full-screen
           sources, fall back to a small floating plaque. */
        if (top_bar >= 22) {
            pd->graphics->fillRect(0, 0, 400, top_bar, kColorWhite);
            title_y = (top_bar - 20) / 2;
        } else {
            pd->graphics->fillRect(36, 4, 328, 25, kColorWhite);
            pd->graphics->drawRect(36, 4, 328, 25, kColorBlack);
            title_y = 7;
        }
        pd->graphics->drawTextInRect(
            title, strlen(title), kUTF8Encoding,
            46, title_y, 308, 20, kWrapClip, kAlignTextCenter
        );
    }

    /* The transport timeline lives entirely in the lower letterbox. */
    if (bottom_bar >= 8) {
        pd->graphics->fillRect(0, bottom_start, 400, bottom_bar, kColorWhite);
        timeline_y = bottom_start + 3;
    } else {
        bottom_start = 232;
        bottom_bar = 8;
        pd->graphics->fillRect(0, bottom_start, 400, bottom_bar, kColorWhite);
        timeline_y = 233;
    }
    progress(20, timeline_y, 360, bottom_bar >= 28 ? 8 : 5, position_ms, duration_ms);
    format_time(left, sizeof(left), position_ms);
    format_time(right, sizeof(right), duration_ms);
    if (bottom_bar >= 28) {
        right_width = pd->graphics->getTextWidth(NULL, right, strlen(right), kUTF8Encoding, 0);
        pd->graphics->drawText(left, strlen(left), kUTF8Encoding, 20, bottom_start + 13);
        pd->graphics->drawText(
            right, strlen(right), kUTF8Encoding,
            380 - right_width, bottom_start + 13
        );
    }

    /* A bordered plaque remains readable over both light and dark frames. */
    buffering_state = strncmp(state, "BUFFERING", 9) == 0;
    state_width = pd->graphics->getTextWidth(
        NULL,
        buffering_state ? "BUFFERING..." : state,
        strlen(buffering_state ? "BUFFERING..." : state),
        kUTF8Encoding,
        0
    );
    badge_width = state_width + (strcmp(state, "PAUSE") == 0 ? 52 : 28);
    badge_x = center_x - badge_width / 2;
    badge_y = center_y - 20;
    pd->graphics->fillRect(badge_x - 2, badge_y - 2, badge_width + 4, 44, kColorWhite);
    pd->graphics->fillRect(badge_x, badge_y, badge_width, 40, kColorBlack);
    pd->graphics->fillRect(badge_x + 2, badge_y + 2, badge_width - 4, 36, kColorWhite);
    state_x = badge_x + 14;
    if (strcmp(state, "PAUSE") == 0) {
        pd->graphics->fillRect(badge_x + 13, badge_y + 10, 5, 20, kColorBlack);
        pd->graphics->fillRect(badge_x + 23, badge_y + 10, 5, 20, kColorBlack);
        state_x = badge_x + 39;
    }
    pd->graphics->drawText(state, strlen(state), kUTF8Encoding, state_x, badge_y + 9);
}

void jd_ui_draw_paused_overlay(
    const char* title,
    uint64_t position_ms,
    uint64_t duration_ms,
    uint16_t video_x,
    uint16_t video_y,
    uint16_t video_width,
    uint16_t video_height
) {
    draw_transport_overlay(
        title, "PAUSE", 1, position_ms, duration_ms,
        video_x, video_y, video_width, video_height
    );
}

void jd_ui_draw_scrub_overlay(
    uint64_t position_ms,
    uint64_t duration_ms,
    uint16_t video_x,
    uint16_t video_y,
    uint16_t video_width,
    uint16_t video_height
) {
    draw_transport_overlay(
        "", "SCRUB", 0, position_ms, duration_ms,
        video_x, video_y, video_width, video_height
    );
}

void jd_ui_draw_buffering_overlay(
    uint64_t position_ms,
    uint64_t duration_ms,
    uint16_t video_x,
    uint16_t video_y,
    uint16_t video_width,
    uint16_t video_height
) {
    static const char* frames[] = {
        "BUFFERING.",
        "BUFFERING..",
        "BUFFERING..."
    };
    uint32_t phase = (pd->system->getCurrentTimeMilliseconds() / 250) % 3;
    draw_transport_overlay(
        "", frames[phase], 0, position_ms, duration_ms,
        video_x, video_y, video_width, video_height
    );
}

void jd_ui_draw_ended(void) {
    pd->graphics->clear(kColorWhite);
    text_centered("END OF TRANSMISSION", 100);
    text_centered("B: RETURN", 145);
}
