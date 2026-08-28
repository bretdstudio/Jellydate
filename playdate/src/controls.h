#pragma once

#include "pd_api.h"
#include <stdint.h>

typedef struct {
    int scrubbing;
    uint64_t target_ms;
    uint32_t last_motion_ms;
    float browse_crank_degrees;
} JDControls;

typedef struct {
    int toggle_pause;
    int stop;
    int scrub_changed;
    int seek_committed;
    uint64_t seek_ms;
} JDControlActions;

typedef struct {
    int movement;
    int horizontal;
    int vertical;
    int select;
    int back;
    int select_held;
    int back_held;
} JDBrowseActions;

void jd_controls_init(JDControls* controls);
JDControlActions jd_controls_update(
    JDControls* controls,
    PlaydateAPI* playdate,
    uint64_t current_ms,
    uint64_t duration_ms
);
JDBrowseActions jd_controls_update_browser(JDControls* controls, PlaydateAPI* playdate);
