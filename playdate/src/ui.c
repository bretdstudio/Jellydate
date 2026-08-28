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

static void draw_continue_icon(int x, int y) {
    pd->graphics->drawEllipse(x + 2, y + 1, 38, 38, 2, 0, 360, kColorBlack);
    pd->graphics->fillTriangle(x + 17, y + 10, x + 17, y + 30, x + 31, y + 20, kColorBlack);
    pd->graphics->drawLine(x + 5, y + 42, x + 39, y + 42, 2, kColorBlack);
    pd->graphics->fillRect(x + 5, y + 40, 14, 5, kColorBlack);
}

static void draw_movie_icon(int x, int y) {
    pd->graphics->fillRect(x + 1, y + 3, 42, 9, kColorBlack);
    pd->graphics->drawLine(x + 7, y + 3, x + 14, y + 11, 2, kColorWhite);
    pd->graphics->drawLine(x + 21, y + 3, x + 28, y + 11, 2, kColorWhite);
    pd->graphics->drawLine(x + 35, y + 3, x + 42, y + 11, 2, kColorWhite);
    pd->graphics->drawRect(x + 1, y + 14, 42, 28, kColorBlack);
    pd->graphics->drawLine(x + 1, y + 21, x + 43, y + 21, 2, kColorBlack);
    pd->graphics->fillTriangle(x + 18, y + 25, x + 18, y + 37, x + 28, y + 31, kColorBlack);
}

static void draw_tv_icon(int x, int y) {
    pd->graphics->drawLine(x + 22, y + 9, x + 11, y, 2, kColorBlack);
    pd->graphics->drawLine(x + 22, y + 9, x + 34, y, 2, kColorBlack);
    pd->graphics->drawRect(x + 1, y + 9, 43, 31, kColorBlack);
    pd->graphics->drawRect(x + 5, y + 13, 29, 22, kColorBlack);
    pd->graphics->fillEllipse(x + 37, y + 15, 4, 4, 0, 360, kColorBlack);
    pd->graphics->fillEllipse(x + 37, y + 26, 4, 4, 0, 360, kColorBlack);
    pd->graphics->drawLine(x + 8, y + 43, x + 15, y + 39, 2, kColorBlack);
    pd->graphics->drawLine(x + 37, y + 43, x + 30, y + 39, 2, kColorBlack);
}

static void draw_recent_icon(int x, int y) {
    pd->graphics->drawEllipse(x + 2, y + 10, 31, 31, 2, 0, 360, kColorBlack);
    pd->graphics->drawLine(x + 17, y + 25, x + 17, y + 16, 2, kColorBlack);
    pd->graphics->drawLine(x + 17, y + 25, x + 25, y + 29, 2, kColorBlack);
    pd->graphics->drawLine(x + 38, y, x + 38, y + 16, 2, kColorBlack);
    pd->graphics->drawLine(x + 30, y + 8, x + 46, y + 8, 2, kColorBlack);
    pd->graphics->drawLine(x + 33, y + 3, x + 43, y + 13, 1, kColorBlack);
    pd->graphics->drawLine(x + 43, y + 3, x + 33, y + 13, 1, kColorBlack);
}

static void draw_home_icon(int index, int x, int y) {
    if (index == 0) draw_continue_icon(x, y);
    else if (index == 1) draw_movie_icon(x, y);
    else if (index == 2) draw_tv_icon(x, y);
    else draw_recent_icon(x, y);
}

void jd_ui_draw_menu(int selected) {
    static const char* title_top[] = { "CONTINUE", "MOVIES", "TV SHOWS", "RECENTLY" };
    static const char* title_bottom[] = { "WATCHING", "", "", "ADDED" };
    int index;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    for (index = 0; index < 4; index += 1) {
        int x = 7 + (index % 2) * 195;
        int y = 57 + (index / 2) * 75;
        if (index == selected) {
            pd->graphics->drawRect(x, y, 191, 69, kColorBlack);
            pd->graphics->drawRect(x + 2, y + 2, 187, 65, kColorBlack);
            pd->graphics->fillRect(x + 7, y + 7, 5, 5, kColorBlack);
        } else {
            pd->graphics->drawRect(x, y, 191, 69, kColorBlack);
        }
        draw_home_icon(index, x + 13, y + 13);
        if (title_bottom[index][0] == '\0') {
            scaled_text(title_top[index], x + 67, y + 23, 112, 0.98f);
        } else {
            scaled_text(title_top[index], x + 67, y + 12, 112, 0.90f);
            scaled_text(title_bottom[index], x + 67, y + 35, 112, 0.90f);
        }
    }
    pd->graphics->drawText("A: SELECT", 9, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("D-PAD / CRANK", 13, kUTF8Encoding, 258, 222);
}

void jd_ui_draw_catalog(
    const char* heading,
    const JDHomeItem* items,
    int count,
    int selected,
    int loading
) {
    int first;
    int visible;
    int index;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    pd->graphics->drawTextInRect(
        heading, strlen(heading), kUTF8Encoding,
        12, 35, 376, 18, kWrapClip, kAlignTextLeft
    );
    if (loading) {
        static const char* frames[] = {
            "Loading data.",
            "Loading data..",
            "Loading data..."
        };
        uint32_t phase = (pd->system->getCurrentTimeMilliseconds() / 250) % 3;
        text_centered(frames[phase], 112);
        return;
    }
    if (count <= 0) {
        text_centered("Nothing tuned in here yet.", 104);
        text_centered("B: RETURN HOME", 132);
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
    pd->graphics->drawText("A: SELECT B: BACK", 17, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("CRANK: TUNE", 11, kUTF8Encoding, 275, 222);
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
