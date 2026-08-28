#include "controls.h"

#include <string.h>

void jd_controls_init(JDControls* controls) {
    memset(controls, 0, sizeof(*controls));
}

JDControlActions jd_controls_update(
    JDControls* controls,
    PlaydateAPI* playdate,
    uint64_t current_ms,
    uint64_t duration_ms
) {
    JDControlActions actions;
    PDButtons current;
    PDButtons pushed;
    PDButtons released;
    float change;
    float magnitude;
    float milliseconds_per_degree;
    int64_t candidate;
    uint32_t now = playdate->system->getCurrentTimeMilliseconds();
    memset(&actions, 0, sizeof(actions));
    playdate->system->getButtonState(&current, &pushed, &released);
    (void)current;
    (void)released;
    if (pushed & kButtonA) actions.toggle_pause = 1;
    if (pushed & kButtonB) actions.stop = 1;

    change = playdate->system->getCrankChange();
    magnitude = change < 0.0f ? -change : change;
    if (magnitude >= 0.25f) {
        if (!controls->scrubbing) {
            controls->scrubbing = 1;
            controls->target_ms = current_ms;
        }
        /* Slow turns are precise; fast turns rapidly wind the imaginary reel. */
        milliseconds_per_degree = magnitude < 3.0f ? 100.0f :
                                  magnitude < 10.0f ? 500.0f : 2000.0f;
        candidate = (int64_t)controls->target_ms + (int64_t)(change * milliseconds_per_degree);
        if (candidate < 0) candidate = 0;
        if (duration_ms > 0 && (uint64_t)candidate > duration_ms) candidate = (int64_t)duration_ms;
        controls->target_ms = (uint64_t)candidate;
        controls->last_motion_ms = now;
        actions.scrub_changed = 1;
        actions.seek_ms = controls->target_ms;
    } else if (controls->scrubbing && now - controls->last_motion_ms > 500) {
        controls->scrubbing = 0;
        actions.seek_committed = 1;
        actions.seek_ms = controls->target_ms;
    }
    return actions;
}

JDBrowseActions jd_controls_update_browser(JDControls* controls, PlaydateAPI* playdate) {
    JDBrowseActions actions;
    PDButtons current;
    PDButtons pushed;
    PDButtons released;
    float change;
    memset(&actions, 0, sizeof(actions));
    playdate->system->getButtonState(&current, &pushed, &released);
    (void)current;
    (void)released;
    if (pushed & (kButtonUp | kButtonLeft)) actions.movement -= 1;
    if (pushed & (kButtonDown | kButtonRight)) actions.movement += 1;
    if (pushed & kButtonA) actions.select = 1;
    if (pushed & kButtonB) actions.back = 1;

    change = playdate->system->getCrankChange();
    controls->browse_crank_degrees += change;
    while (controls->browse_crank_degrees >= 30.0f && actions.movement < 3) {
        actions.movement += 1;
        controls->browse_crank_degrees -= 30.0f;
    }
    while (controls->browse_crank_degrees <= -30.0f && actions.movement > -3) {
        actions.movement -= 1;
        controls->browse_crank_degrees += 30.0f;
    }
    return actions;
}
