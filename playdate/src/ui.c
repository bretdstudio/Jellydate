#include "ui.h"

#include <stdio.h>
#include <string.h>

static PlaydateAPI* pd;
static LCDBitmap* scaled_text_bitmap;
static LCDBitmap* boot_screen_bitmap;

#define TITLE_TEXT_SCALE 1.15f
#define META_TEXT_SCALE 0.82f
#define SCALED_TEXT_BITMAP_WIDTH 480
#define SCALED_TEXT_BITMAP_HEIGHT 24

static void text_centered(const char* text, int y) {
    int width = pd->graphics->getTextWidth(NULL, text, strlen(text), kUTF8Encoding, 0);
    pd->graphics->drawText(text, strlen(text), kUTF8Encoding, (400 - width) / 2, y);
}

static void loading_data(int y) {
    static const char* frames[] = {
        "Loading data.",
        "Loading data..",
        "Loading data..."
    };
    uint32_t phase = (pd->system->getCurrentTimeMilliseconds() / 250) % 3;
    text_centered(frames[phase], y);
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
    const char* bitmap_error = NULL;
    pd = playdate;
    scaled_text_bitmap = pd->graphics->newBitmap(
        SCALED_TEXT_BITMAP_WIDTH, SCALED_TEXT_BITMAP_HEIGHT, kColorClear
    );
    boot_screen_bitmap = pd->graphics->loadBitmap(
        "images/jellydate-boot", &bitmap_error
    );
    if (boot_screen_bitmap == NULL && bitmap_error != NULL) {
        pd->system->logToConsole("Could not load Jellydate boot art: %s", bitmap_error);
    }
}

void jd_ui_shutdown(void) {
    if (scaled_text_bitmap != NULL) {
        pd->graphics->freeBitmap(scaled_text_bitmap);
        scaled_text_bitmap = NULL;
    }
    if (boot_screen_bitmap != NULL) {
        pd->graphics->freeBitmap(boot_screen_bitmap);
        boot_screen_bitmap = NULL;
    }
}

void jd_ui_draw_boot(void) {
    uint32_t phase;
    int index;
    if (boot_screen_bitmap != NULL) {
        phase = (pd->system->getCurrentTimeMilliseconds() / 250) % 3;
        pd->graphics->clear(kColorWhite);
        pd->graphics->drawBitmap(boot_screen_bitmap, 0, 0, kBitmapUnflipped);
        pd->graphics->fillRect(178, 216, 44, 24, kColorWhite);
        for (index = 0; index < 3; index += 1) {
            int x = 185 + index * 12;
            pd->graphics->drawEllipse(x, 222, 7, 7, 1, 0, 360, kColorBlack);
            if (index == (int)phase) {
                pd->graphics->fillEllipse(x + 2, 224, 3, 3, 0, 360, kColorBlack);
            }
        }
        return;
    }
    jd_ui_draw_tuning("starting receiver");
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

void jd_ui_draw_setup(
    const char* host,
    size_t token_length,
    int selected,
    const char* status,
    int can_cancel
) {
    char token_status[40];
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    pd->graphics->drawText("BRIDGE SETUP", 12, kUTF8Encoding, 12, 35);

    if (selected == 0) pd->graphics->drawRect(8, 58, 384, 48, kColorBlack);
    pd->graphics->drawText("HOST", 4, kUTF8Encoding, 16, 61);
    scaled_text(host[0] == '\0' ? "Not set" : host, 16, 82, 368, META_TEXT_SCALE);

    if (selected == 1) pd->graphics->drawRect(8, 109, 384, 48, kColorBlack);
    pd->graphics->drawText("TOKEN", 5, kUTF8Encoding, 16, 112);
    if (token_length == 0) snprintf(token_status, sizeof(token_status), "Not set");
    else snprintf(token_status, sizeof(token_status), "%u characters saved", (unsigned int)token_length);
    scaled_text(token_status, 16, 133, 368, META_TEXT_SCALE);

    if (selected == 2) pd->graphics->drawRect(8, 160, 238, 37, kColorBlack);
    if (selected == 3) pd->graphics->drawRect(254, 160, 138, 37, kColorBlack);
    pd->graphics->drawText("TEST & SAVE", 11, kUTF8Encoding, 20, 168);
    pd->graphics->drawText(
        can_cancel ? "CANCEL" : "REQUIRED", can_cancel ? 6 : 8,
        kUTF8Encoding, can_cancel ? 274 : 262, 168
    );
    if (status != NULL && status[0] != '\0') {
        scaled_text(status, 12, 199, 376, 0.62f);
    }
    pd->graphics->drawText("A: SELECT", 9, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("CRANK: MOVE", 11, kUTF8Encoding, 282, 222);
}

void jd_ui_draw_text_entry(const char* label, const char* text, int selected) {
    const char* characters = JD_TEXT_ENTRY_CHARACTERS;
    const char* visible = text;
    size_t text_length = strlen(text);
    int index;
    if (text_length > 42) visible = text + text_length - 42;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText(label, strlen(label), kUTF8Encoding, 8, 5);
    pd->graphics->drawText("A: TYPE  B: DELETE", 18, kUTF8Encoding, 207, 5);
    pd->graphics->drawTextInRect(
        visible, strlen(visible), kUTF8Encoding,
        8, 30, 384, 24, kWrapClip, kAlignTextLeft
    );
    pd->graphics->drawLine(8, 55, 392, 55, 1, kColorBlack);

    for (index = 0; index < JD_TEXT_ENTRY_ITEM_COUNT; index += 1) {
        char character[2];
        const char* item;
        int item_length;
        int column = index % 13;
        int row = index / 13;
        int x = 8 + column * 30;
        int y = 68 + row * 25;
        int width;
        if (index < JD_TEXT_ENTRY_CHARACTER_COUNT) {
            character[0] = characters[index];
            character[1] = '\0';
            item = character;
            item_length = 1;
        } else if (index == JD_TEXT_ENTRY_CLEAR_INDEX) {
            item = "CLR";
            item_length = 3;
        } else if (index == JD_TEXT_ENTRY_CANCEL_INDEX) {
            item = "X";
            item_length = 1;
        } else {
            item = "OK";
            item_length = 2;
        }
        if (index == selected) pd->graphics->drawRect(x, y, 28, 23, kColorBlack);
        width = pd->graphics->getTextWidth(NULL, item, item_length, kUTF8Encoding, 0);
        pd->graphics->drawText(
            item, item_length, kUTF8Encoding, x + (28 - width) / 2, y + 2
        );
    }
}

void jd_ui_draw_setup_testing(const char* host) {
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    text_centered("TESTING BRIDGE", 62);
    scaled_text(host, 20, 92, 360, META_TEXT_SCALE);
    loading_data(132);
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
        loading_data(112);
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
        if (item->duration_ms > 0) {
            progress(292, y + 40, 87, 5, item->position_ms, item->duration_ms);
        }
    }
    pd->graphics->drawText("A: SELECT B: BACK", 17, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("CRANK: TUNE", 11, kUTF8Encoding, 275, 222);
}

void jd_ui_draw_alpha_index(const char* heading, int selected) {
    int index;
    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    pd->graphics->drawText(heading, strlen(heading), kUTF8Encoding, 12, 35);

    for (index = 0; index < 27; index += 1) {
        char label[2];
        int column = index % 9;
        int row = index / 9;
        int x = 14 + column * 42;
        int y = 58 + row * 48;
        int width;
        label[0] = index == 0 ? '#' : (char)('A' + index - 1);
        label[1] = '\0';
        if (index == selected) {
            pd->graphics->drawRect(x, y, 36, 36, kColorBlack);
            pd->graphics->drawRect(x + 2, y + 2, 32, 32, kColorBlack);
        }
        width = pd->graphics->getTextWidth(
            NULL, label, 1, kUTF8Encoding, 0
        );
        pd->graphics->drawText(label, 1, kUTF8Encoding, x + (36 - width) / 2, y + 8);
    }

    pd->graphics->drawText("A: OPEN B: HOME", 15, kUTF8Encoding, 12, 222);
    pd->graphics->drawText("CRANK: LETTER", 13, kUTF8Encoding, 270, 222);
}

void jd_ui_draw_details(const JDItemDetails* details, int loading) {
    char position[16];
    char duration[16];
    int duration_width;
    const char* action;

    pd->graphics->clear(kColorWhite);
    pd->graphics->drawText("JELLYDATE", 9, kUTF8Encoding, 12, 7);
    pd->graphics->drawLine(12, 28, 388, 28, 2, kColorBlack);
    if (loading) {
        scaled_text(details->title, 12, 38, 376, TITLE_TEXT_SCALE);
        loading_data(112);
        return;
    }

    scaled_text(details->title, 12, 36, 376, TITLE_TEXT_SCALE);
    scaled_text(details->subtitle, 12, 61, 376, META_TEXT_SCALE);
    progress(20, 84, 360, 7, details->position_ms, details->duration_ms);
    format_time(position, sizeof(position), details->position_ms);
    format_time(duration, sizeof(duration), details->duration_ms);
    duration_width = pd->graphics->getTextWidth(
        NULL, duration, strlen(duration), kUTF8Encoding, 0
    );
    pd->graphics->drawText(position, strlen(position), kUTF8Encoding, 20, 96);
    pd->graphics->drawText(
        duration, strlen(duration), kUTF8Encoding,
        380 - duration_width, 96
    );
    pd->graphics->drawText("ABOUT", 5, kUTF8Encoding, 12, 121);
    pd->graphics->drawLine(12, 140, 388, 140, 1, kColorBlack);
    pd->graphics->drawTextInRect(
        details->overview, strlen(details->overview), kUTF8Encoding,
        12, 146, 376, 65, kWrapWord, kAlignTextLeft
    );

    action = details->position_ms > 0 ? "A: RESUME" : "A: WATCH";
    pd->graphics->drawText(action, strlen(action), kUTF8Encoding, 12, 222);
    pd->graphics->drawText("B: BACK", 7, kUTF8Encoding, 316, 222);
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

static void draw_mini_tv(int x, int y, int static_phase) {
    int line;

    /* Rabbit ears and their round tips. */
    pd->graphics->drawLine(x + 23, y + 14, x + 12, y + 3, 2, kColorBlack);
    pd->graphics->drawLine(x + 35, y + 14, x + 46, y + 3, 2, kColorBlack);
    pd->graphics->drawEllipse(x + 9, y, 6, 6, 1, 0, 360, kColorBlack);
    pd->graphics->drawEllipse(x + 43, y, 6, 6, 1, 0, 360, kColorBlack);
    pd->graphics->fillRect(x + 25, y + 13, 8, 3, kColorBlack);

    /* A tiny CRT cabinet with a white face and black control panel. */
    pd->graphics->fillRect(x + 1, y + 16, 56, 38, kColorBlack);
    pd->graphics->fillRect(x + 4, y + 19, 39, 32, kColorWhite);
    pd->graphics->drawRect(x + 6, y + 21, 35, 28, kColorBlack);
    pd->graphics->fillEllipse(x + 47, y + 21, 6, 6, 0, 360, kColorWhite);
    pd->graphics->fillEllipse(x + 47, y + 31, 6, 6, 0, 360, kColorWhite);
    pd->graphics->fillRect(x + 47, y + 43, 2, 2, kColorWhite);
    pd->graphics->fillRect(x + 52, y + 43, 2, 2, kColorWhite);
    pd->graphics->fillRect(x + 47, y + 48, 2, 2, kColorWhite);
    pd->graphics->fillRect(x + 52, y + 48, 2, 2, kColorWhite);
    pd->graphics->fillRect(x + 8, y + 54, 10, 2, kColorBlack);
    pd->graphics->fillRect(x + 40, y + 54, 10, 2, kColorBlack);

    if (static_phase == 1) {
        for (line = 0; line < 3; line += 1) {
            int inset = (line + static_phase) % 2 == 0 ? 0 : 4;
            pd->graphics->drawLine(
                x + 10 + inset, y + 27 + line * 6,
                x + 37 - inset, y + 27 + line * 6,
                1, kColorBlack
            );
        }
    } else {
        /* Friendly face; phase two adds one passing line of television static. */
        pd->graphics->fillRect(x + 14, y + 29, 3, 7, kColorBlack);
        pd->graphics->fillRect(x + 30, y + 29, 3, 7, kColorBlack);
        pd->graphics->fillTriangle(
            x + 21, y + 39, x + 27, y + 39, x + 24, y + 43, kColorBlack
        );
        if (static_phase == 2) {
            pd->graphics->drawLine(x + 10, y + 37, x + 37, y + 37, 1, kColorBlack);
        }
    }
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
    int badge_height;
    int state_x;
    int state_width;
    int buffering_state;
    int scrub_state;
    int transport_state;
    int transport_time_width;
    int text_block_width;
    int tv_x;
    int tv_y;
    int right_width;
    char transport_time[40];

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
    scrub_state = strcmp(state, "SCRUB") == 0;
    transport_state = buffering_state || scrub_state;
    state_width = pd->graphics->getTextWidth(
        NULL,
        buffering_state ? "BUFFERING..." : state,
        strlen(buffering_state ? "BUFFERING..." : state),
        kUTF8Encoding,
        0
    );
    transport_time_width = 0;
    if (transport_state) {
        snprintf(transport_time, sizeof(transport_time), "%s / %s", left, right);
        transport_time_width = pd->graphics->getTextWidth(
            NULL, transport_time, strlen(transport_time), kUTF8Encoding, 0
        );
        text_block_width = state_width > transport_time_width
            ? state_width : transport_time_width;
        badge_width = 94 + text_block_width;
        if (badge_width > 360) badge_width = 360;
        badge_height = 76;
    } else {
        badge_width = state_width + (strcmp(state, "PAUSE") == 0 ? 52 : 28);
        badge_height = 40;
    }
    badge_x = center_x - badge_width / 2;
    badge_y = center_y - badge_height / 2;
    pd->graphics->fillRect(
        badge_x - 2, badge_y - 2, badge_width + 4, badge_height + 4, kColorWhite
    );
    pd->graphics->fillRect(badge_x, badge_y, badge_width, badge_height, kColorBlack);
    pd->graphics->fillRect(
        badge_x + 2, badge_y + 2, badge_width - 4, badge_height - 4, kColorWhite
    );
    if (transport_state) {
        tv_x = badge_x + 12;
        tv_y = badge_y + 9;
        draw_mini_tv(
            tv_x, tv_y,
            buffering_state
                ? (int)((pd->system->getCurrentTimeMilliseconds() / 250) % 3)
                : -1
        );
        state_x = badge_x + 82;
        pd->graphics->drawText(
            state, strlen(state), kUTF8Encoding, state_x, badge_y + 15
        );
        pd->graphics->drawText(
            transport_time, strlen(transport_time), kUTF8Encoding,
            state_x, badge_y + 42
        );
    } else if (strcmp(state, "PAUSE") == 0) {
        state_x = badge_x + 14;
        pd->graphics->fillRect(badge_x + 13, badge_y + 10, 5, 20, kColorBlack);
        pd->graphics->fillRect(badge_x + 23, badge_y + 10, 5, 20, kColorBlack);
        state_x = badge_x + 39;
        pd->graphics->drawText(state, strlen(state), kUTF8Encoding, state_x, badge_y + 9);
    } else {
        state_x = badge_x + 14;
        pd->graphics->drawText(state, strlen(state), kUTF8Encoding, state_x, badge_y + 9);
    }
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
