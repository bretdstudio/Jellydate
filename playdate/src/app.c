#include "app.h"

#include "audio.h"
#include "config.h"
#include "controls.h"
#include "network.h"
#include "protocol.h"
#include "ui.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

#define JD_VIDEO_START_FRAMES 6
#define JD_CLIENT_STATS_INTERVAL_MS 1000
#define JD_RECONNECT_DELAY_MS 2000
#define JD_CATALOG_CONTINUE_WATCHING 0
#define JD_CATALOG_MOVIES 1
#define JD_MENU_ITEM_COUNT 2

typedef enum {
    JD_APP_TUNING,
    JD_APP_BUFFERING,
    JD_APP_PLAYING,
    JD_APP_PAUSED,
    JD_APP_SCRUBBING,
    JD_APP_SEEK_BUFFERING,
    JD_APP_ENDED,
    JD_APP_ERROR,
    JD_APP_MENU,
    JD_APP_CATALOG_LOADING,
    JD_APP_CATALOG
} JDAppMode;

typedef struct {
    PlaydateAPI* pd;
    JDNetwork network;
    JDProtocolParser parser;
    JDControls controls;
    JDAppMode mode;
    int handshake_sent;
    int play_sent;
    int catalog_requested;
    int catalog_active;
    uint8_t catalog_kind;
    int paused;
    int scrub_started_paused;
    int pause_after_seek;
    int seek_stream_ready;
    int stream_playing;
    int reconnect_pending;
    int system_resume_pending;
    JDAppMode mode_before_system_pause;
    uint32_t command_sequence;
    uint32_t last_stats_ms;
    uint32_t reconnect_at_ms;
    uint64_t position_ms;
    uint64_t duration_ms;
    uint16_t video_x;
    uint16_t video_y;
    uint16_t video_width;
    uint16_t video_height;
    int home_count;
    int home_selected;
    int menu_selected;
    JDHomeItem home_items[JD_HOME_MAX_ITEMS];
    char item_id[64];
    char title[96];
    char status[96];
} JDApp;

static JDApp app;
static uint8_t decoded_video_frame[JD_FRAME_SIZE];
static int decoded_video_ready;

static void write_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void queue_packet(uint8_t type, const uint8_t* payload, uint32_t length) {
    uint8_t packet[320];
    size_t encoded = jd_protocol_encode_packet(
        packet, sizeof(packet), type, 0, 0,
        app.command_sequence++, payload, length
    );
    if (encoded > 0) jd_network_send(&app.network, packet, encoded);
}

static void queue_seek(uint64_t milliseconds) {
    uint8_t payload[8];
    int index;
    for (index = 0; index < 8; index += 1) {
        payload[index] = (uint8_t)(milliseconds >> ((7 - index) * 8));
    }
    queue_packet(JD_PACKET_SEEK, payload, sizeof(payload));
}

static void queue_play(void) {
    uint8_t play[320];
    size_t play_length = jd_protocol_encode_play(
        play, sizeof(play), app.item_id, app.position_ms
    );
    if (play_length > 0 && jd_network_send(&app.network, play, play_length)) {
        app.play_sent = 1;
    }
}

static const char* catalog_heading(uint8_t kind) {
    return kind == JD_CATALOG_MOVIES ? "MOVIES" : "CONTINUE WATCHING";
}

static void queue_catalog_request(uint8_t kind) {
    queue_packet(JD_PACKET_HOME_REQUEST, &kind, 1);
    app.catalog_requested = 1;
    app.mode = JD_APP_CATALOG_LOADING;
}

static int parse_home_items(JDApp* state, const uint8_t* payload, size_t length) {
    size_t cursor = 0;
    int count;
    int index;
    if (length < 1) return 0;
    count = payload[cursor++];
    if (count > JD_HOME_MAX_ITEMS) return 0;
    memset(state->home_items, 0, sizeof(state->home_items));
    for (index = 0; index < count; index += 1) {
        JDHomeItem* item = &state->home_items[index];
        uint8_t field_length;
        size_t copy;
        if (cursor >= length) return 0;
        field_length = payload[cursor++];
        if (field_length == 0 || field_length > length - cursor) return 0;
        copy = field_length < sizeof(item->id) - 1 ? field_length : sizeof(item->id) - 1;
        memcpy(item->id, payload + cursor, copy);
        item->id[copy] = '\0';
        cursor += field_length;

        if (cursor >= length) return 0;
        field_length = payload[cursor++];
        if (field_length > length - cursor) return 0;
        copy = field_length < sizeof(item->title) - 1 ? field_length : sizeof(item->title) - 1;
        memcpy(item->title, payload + cursor, copy);
        item->title[copy] = '\0';
        cursor += field_length;

        if (cursor >= length) return 0;
        field_length = payload[cursor++];
        if (field_length > length - cursor) return 0;
        copy = field_length < sizeof(item->subtitle) - 1 ? field_length : sizeof(item->subtitle) - 1;
        memcpy(item->subtitle, payload + cursor, copy);
        item->subtitle[copy] = '\0';
        cursor += field_length;
        if (length - cursor < 16) return 0;
        item->position_ms = jd_protocol_read_u64(payload + cursor);
        item->duration_ms = jd_protocol_read_u64(payload + cursor + 8);
        cursor += 16;
    }
    if (cursor != length) return 0;
    state->home_count = count;
    if (state->home_selected >= count) state->home_selected = count > 0 ? count - 1 : 0;
    return 1;
}

static void send_client_stats(void) {
    uint8_t payload[28];
    write_u32(payload, jd_video_queued_frames());
    write_u32(payload + 4, jd_video_dropped_frames());
    write_u32(payload + 8, jd_audio_queued_samples());
    write_u32(payload + 12, jd_audio_underruns());
    write_u32(payload + 16, jd_audio_dropped_samples());
    write_u32(payload + 20, (uint32_t)app.mode);
    write_u32(payload + 24, (uint32_t)(jd_audio_playhead_us() / 1000));
    queue_packet(JD_PACKET_CLIENT_STATS, payload, sizeof(payload));
}

static void maybe_start_synced_playback(JDApp* state) {
    if (!state->stream_playing || state->paused || state->pause_after_seek) return;
    if (state->mode != JD_APP_BUFFERING && state->mode != JD_APP_SEEK_BUFFERING) return;
    if (jd_video_queued_frames() < JD_VIDEO_START_FRAMES || !jd_audio_ready()) return;
    jd_audio_set_paused(0);
    state->mode = JD_APP_PLAYING;
}

static void accept_video_frame(
    JDApp* state,
    const JDProtocolHeader* header,
    const uint8_t* frame
) {
    if (state->paused || state->mode == JD_APP_SCRUBBING ||
        (state->mode == JD_APP_SEEK_BUFFERING && !state->seek_stream_ready)) return;
    if (state->pause_after_seek) {
        jd_video_reset_queue();
        jd_video_render_packed(frame);
        state->position_ms = header->timestamp_us / 1000;
        state->paused = 1;
        jd_audio_set_paused(1);
        state->mode = JD_APP_PAUSED;
        queue_packet(JD_PACKET_PAUSE, NULL, 0);
    } else {
        jd_video_queue_packed(frame, header->timestamp_us);
    }
}

static void on_packet(
    void* context,
    const JDProtocolHeader* header,
    const uint8_t* payload
) {
    JDApp* state = (JDApp*)context;
    size_t copy;
    size_t title_length;
    size_t viewport_offset;
    uint16_t stream_width;
    uint16_t stream_height;
    uint16_t video_format;
    uint32_t audio_sample_rate;
    uint8_t audio_channels;
    uint8_t audio_format;
    switch (header->type) {
        case JD_PACKET_STREAM_INFO:
            jd_audio_set_paused(1);
            jd_audio_reset();
            jd_video_reset_queue();
            decoded_video_ready = 0;
            state->stream_playing = 0;
            if (header->payload_length < 24) {
                snprintf(state->status, sizeof(state->status), "bad stream information");
                state->mode = JD_APP_ERROR;
                break;
            }
            stream_width = jd_protocol_read_u16(payload);
            stream_height = jd_protocol_read_u16(payload + 2);
            video_format = jd_protocol_read_u16(payload + 6);
            audio_sample_rate = jd_protocol_read_u32(payload + 8);
            audio_channels = payload[12];
            audio_format = payload[13];
            if (video_format != 1 || audio_channels != 1 ||
                !jd_video_configure(stream_width, stream_height) ||
                !jd_audio_configure(audio_sample_rate, audio_format)) {
                snprintf(state->status, sizeof(state->status), "unsupported stream format");
                state->mode = JD_APP_ERROR;
                break;
            }
            state->duration_ms = jd_protocol_read_u64(payload + 16);
            if (header->payload_length >= 25) {
                title_length = payload[24];
                copy = title_length;
                if (copy > header->payload_length - 25) copy = header->payload_length - 25;
                if (copy > sizeof(state->title) - 1) copy = sizeof(state->title) - 1;
                memcpy(state->title, payload + 25, copy);
                state->title[copy] = '\0';
                viewport_offset = 25 + title_length;
                if (viewport_offset + 8 <= header->payload_length) {
                    state->video_x = jd_protocol_read_u16(payload + viewport_offset);
                    state->video_y = jd_protocol_read_u16(payload + viewport_offset + 2);
                    state->video_width = jd_protocol_read_u16(payload + viewport_offset + 4);
                    state->video_height = jd_protocol_read_u16(payload + viewport_offset + 6);
                }
            }
            if (state->mode == JD_APP_SEEK_BUFFERING) {
                /* TCP can still contain packets from the old stream after the
                   crank settles. STREAM_INFO is the boundary after which all
                   media belongs to the replacement stream. */
                state->seek_stream_ready = 1;
            } else {
                state->mode = JD_APP_BUFFERING;
            }
            break;
        case JD_PACKET_VIDEO_KEYFRAME:
            if (header->payload_length == jd_video_frame_size()) {
                memcpy(decoded_video_frame, payload, header->payload_length);
                decoded_video_ready = 1;
                accept_video_frame(state, header, decoded_video_frame);
            }
            break;
        case JD_PACKET_VIDEO_DELTA:
            if (decoded_video_ready &&
                jd_video_apply_delta(
                    decoded_video_frame,
                    jd_video_frame_size(),
                    payload,
                    header->payload_length
                )) {
                accept_video_frame(state, header, decoded_video_frame);
            } else {
                snprintf(state->status, sizeof(state->status), "bad video delta");
                state->mode = JD_APP_ERROR;
            }
            break;
        case JD_PACKET_AUDIO:
            if (state->mode != JD_APP_SCRUBBING &&
                (state->mode != JD_APP_SEEK_BUFFERING || state->seek_stream_ready)) {
                jd_audio_push_pcm(payload, header->payload_length, header->timestamp_us);
            }
            break;
        case JD_PACKET_PLAYBACK_STATE:
            copy = header->payload_length < sizeof(state->status) - 1
                ? header->payload_length : sizeof(state->status) - 1;
            memcpy(state->status, payload, copy);
            state->status[copy] = '\0';
            if (strcmp(state->status, "ready") == 0) {
                if (state->item_id[0] != '\0') {
                    if (!state->play_sent) queue_play();
                } else if (state->catalog_active) {
                    if (!state->catalog_requested) queue_catalog_request(state->catalog_kind);
                } else {
                    state->mode = JD_APP_MENU;
                }
            } else if (strcmp(state->status, "paused") == 0) {
                state->stream_playing = 0;
                if (state->mode != JD_APP_SEEK_BUFFERING) {
                    state->paused = 1;
                    jd_audio_set_paused(1);
                    state->pause_after_seek = 0;
                    if (state->mode != JD_APP_SCRUBBING) state->mode = JD_APP_PAUSED;
                }
            } else if (strcmp(state->status, "playing") == 0) {
                state->stream_playing = 1;
                if (!state->pause_after_seek) {
                    state->paused = 0;
                    if (state->mode != JD_APP_BUFFERING && state->mode != JD_APP_SEEK_BUFFERING) {
                        jd_audio_set_paused(0);
                    }
                }
                if (!state->pause_after_seek && state->mode != JD_APP_SCRUBBING &&
                    state->mode != JD_APP_BUFFERING && state->mode != JD_APP_SEEK_BUFFERING) {
                    state->mode = JD_APP_PLAYING;
                }
            } else if (strcmp(state->status, "buffering") == 0) {
                state->stream_playing = 0;
            }
            break;
        case JD_PACKET_HOME_RESPONSE:
            if (!parse_home_items(state, payload, header->payload_length)) {
                snprintf(state->status, sizeof(state->status), "bad home listing");
                state->mode = JD_APP_ERROR;
            } else {
                state->mode = JD_APP_CATALOG;
            }
            break;
        case JD_PACKET_END_OF_STREAM:
            jd_audio_set_paused(1);
            state->mode = JD_APP_ENDED;
            break;
        case JD_PACKET_ERROR:
            copy = header->payload_length < sizeof(state->status) - 1
                ? header->payload_length : sizeof(state->status) - 1;
            memcpy(state->status, payload, copy);
            state->status[copy] = '\0';
            jd_audio_set_paused(1);
            state->mode = JD_APP_ERROR;
            break;
        default:
            break;
    }
}

static void on_network_bytes(void* context, const uint8_t* bytes, size_t length) {
    JDApp* state = (JDApp*)context;
    if (!jd_protocol_parser_push(&state->parser, bytes, length)) {
        snprintf(state->status, sizeof(state->status), "bad transmission packet");
        state->mode = JD_APP_ERROR;
    }
}

static void send_handshake(void) {
    const uint8_t* token = (const uint8_t*)JELLYDATE_TOKEN;
    queue_packet(JD_PACKET_AUTH, token, (uint32_t)strlen(JELLYDATE_TOKEN));
    app.handshake_sent = 1;
    app.mode = JD_APP_BUFFERING;
}

void jd_app_init(PlaydateAPI* playdate) {
    memset(&app, 0, sizeof(app));
    app.pd = playdate;
    app.mode = JD_APP_TUNING;
    app.command_sequence = 10;
    app.video_width = 400;
    app.video_height = 240;
    snprintf(app.title, sizeof(app.title), "UNTITLED TRANSMISSION");
    snprintf(app.status, sizeof(app.status), "channel %d", JELLYDATE_STREAM_PORT);
    /* Run the scheduler faster than the media cadence so a small update-loop
       phase shift does not force two 30 FPS frames into one presentation. */
    playdate->display->setRefreshRate(50.0f);
    playdate->system->setAutoLockDisabled(1);
    jd_ui_init(playdate);
    jd_video_init(playdate);
    jd_audio_init(playdate);
    jd_controls_init(&app.controls);
    jd_protocol_parser_init(&app.parser, on_packet, &app);
    jd_network_init(&app.network, playdate, on_network_bytes, &app);
    jd_network_connect(&app.network, JELLYDATE_BRIDGE_HOST, JELLYDATE_STREAM_PORT);
}

int jd_app_update(void* userdata) {
    JDControlActions actions;
    JDBrowseActions browse_actions;
    uint64_t audio_playhead_us;
    uint32_t now;
    (void)userdata;
    now = app.pd->system->getCurrentTimeMilliseconds();
    jd_network_poll(&app.network);
    if (app.network.state == JD_NET_CONNECTED && !app.handshake_sent) send_handshake();
    if (app.network.state == JD_NET_FAILED) {
        if (!app.reconnect_pending) {
            int reconnecting_browser = app.item_id[0] == '\0';
            snprintf(app.status, sizeof(app.status), "%.76s; reconnecting", app.network.error);
            app.pause_after_seek = app.paused;
            app.paused = 0;
            app.stream_playing = 0;
            app.handshake_sent = 0;
            app.play_sent = 0;
            app.catalog_requested = 0;
            jd_audio_set_paused(1);
            jd_audio_reset();
            jd_video_reset_queue();
            jd_network_close(&app.network);
            app.reconnect_at_ms = now + JD_RECONNECT_DELAY_MS;
            app.reconnect_pending = 1;
            app.mode = reconnecting_browser ? JD_APP_TUNING : JD_APP_BUFFERING;
        }
    }
    if (app.reconnect_pending &&
        (int32_t)(now - app.reconnect_at_ms) >= 0) {
        app.reconnect_pending = 0;
        jd_network_connect(&app.network, JELLYDATE_BRIDGE_HOST, JELLYDATE_STREAM_PORT);
    }
    maybe_start_synced_playback(&app);
    if (app.mode == JD_APP_PLAYING) {
        audio_playhead_us = jd_audio_playhead_us();
        if (audio_playhead_us > 0) {
            app.position_ms = audio_playhead_us / 1000;
            jd_video_present_for_time(audio_playhead_us);
        }
    }

    memset(&actions, 0, sizeof(actions));
    memset(&browse_actions, 0, sizeof(browse_actions));
    if (app.mode == JD_APP_MENU) {
        browse_actions = jd_controls_update_browser(&app.controls, app.pd);
        if (browse_actions.movement != 0) {
            app.menu_selected += browse_actions.movement;
            while (app.menu_selected < 0) app.menu_selected += JD_MENU_ITEM_COUNT;
            while (app.menu_selected >= JD_MENU_ITEM_COUNT) app.menu_selected -= JD_MENU_ITEM_COUNT;
        }
        if (browse_actions.select) {
            app.catalog_kind = app.menu_selected == 1
                ? JD_CATALOG_MOVIES : JD_CATALOG_CONTINUE_WATCHING;
            app.catalog_active = 1;
            app.catalog_requested = 0;
            app.home_selected = 0;
            queue_catalog_request(app.catalog_kind);
        }
    } else if (app.mode == JD_APP_CATALOG) {
        browse_actions = jd_controls_update_browser(&app.controls, app.pd);
        if (browse_actions.back) {
            app.catalog_active = 0;
            app.catalog_requested = 0;
            app.mode = JD_APP_MENU;
        }
        if (app.home_count > 0 && browse_actions.movement != 0) {
            app.home_selected += browse_actions.movement;
            while (app.home_selected < 0) app.home_selected += app.home_count;
            while (app.home_selected >= app.home_count) app.home_selected -= app.home_count;
        }
        if (!browse_actions.back && app.home_count > 0 && browse_actions.select) {
            JDHomeItem* selected = &app.home_items[app.home_selected];
            snprintf(app.item_id, sizeof(app.item_id), "%s", selected->id);
            app.position_ms = selected->position_ms;
            app.duration_ms = selected->duration_ms;
            app.play_sent = 0;
            app.paused = 0;
            app.stream_playing = 0;
            app.pause_after_seek = 0;
            jd_audio_set_paused(1);
            jd_audio_reset();
            jd_video_reset_queue();
            queue_play();
            app.mode = JD_APP_BUFFERING;
        }
    } else if (app.mode != JD_APP_CATALOG_LOADING && app.mode != JD_APP_TUNING) {
        actions = jd_controls_update(&app.controls, app.pd, app.position_ms, app.duration_ms);
    }
    if (actions.toggle_pause && (app.mode == JD_APP_PLAYING || app.mode == JD_APP_PAUSED)) {
        app.paused = !app.paused;
        jd_audio_set_paused(app.paused);
        queue_packet(app.paused ? JD_PACKET_PAUSE : JD_PACKET_RESUME, NULL, 0);
        app.mode = app.paused ? JD_APP_PAUSED : JD_APP_PLAYING;
    }
    if (actions.stop) {
        jd_audio_set_paused(1);
        jd_audio_reset();
        jd_video_reset_queue();
        queue_packet(JD_PACKET_STOP, NULL, 0);
        app.reconnect_pending = 0;
        app.item_id[0] = '\0';
        app.paused = 0;
        app.stream_playing = 0;
        app.mode = app.catalog_active ? JD_APP_CATALOG : JD_APP_MENU;
    }
    if (actions.scrub_changed &&
        (app.mode == JD_APP_PLAYING || app.mode == JD_APP_PAUSED ||
         app.mode == JD_APP_SCRUBBING)) {
        if (app.mode != JD_APP_SCRUBBING) {
            app.scrub_started_paused = app.paused;
            if (!app.paused) queue_packet(JD_PACKET_PAUSE, NULL, 0);
            jd_audio_set_paused(1);
            jd_audio_reset();
            jd_video_reset_queue();
            jd_video_redraw_last_frame();
        }
        app.mode = JD_APP_SCRUBBING;
        jd_ui_draw_scrub_overlay(
            actions.seek_ms, app.duration_ms,
            app.video_x, app.video_y, app.video_width, app.video_height
        );
    }
    if (actions.seek_committed && app.mode == JD_APP_SCRUBBING) {
        app.position_ms = actions.seek_ms;
        app.pause_after_seek = app.scrub_started_paused;
        app.paused = 0;
        app.stream_playing = 0;
        app.seek_stream_ready = 0;
        queue_seek(actions.seek_ms);
        app.mode = JD_APP_SEEK_BUFFERING;
    }

    if (app.network.state == JD_NET_CONNECTED &&
        now - app.last_stats_ms >= JD_CLIENT_STATS_INTERVAL_MS) {
        app.last_stats_ms = now;
        send_client_stats();
    }

    switch (app.mode) {
        case JD_APP_TUNING:
            jd_ui_draw_tuning(jd_network_state_text(app.network.state));
            break;
        case JD_APP_BUFFERING:
            jd_ui_draw_tuning("BUFFERING... dramatically");
            break;
        case JD_APP_PAUSED:
            jd_ui_draw_paused_overlay(
                app.title, app.position_ms, app.duration_ms,
                app.video_x, app.video_y, app.video_width, app.video_height
            );
            break;
        case JD_APP_SCRUBBING:
            if (!actions.scrub_changed) {
                jd_ui_draw_scrub_overlay(
                    app.controls.target_ms, app.duration_ms,
                    app.video_x, app.video_y, app.video_width, app.video_height
                );
            }
            break;
        case JD_APP_SEEK_BUFFERING:
            jd_ui_draw_buffering_overlay(
                app.position_ms, app.duration_ms,
                app.video_x, app.video_y, app.video_width, app.video_height
            );
            break;
        case JD_APP_ENDED:
            jd_ui_draw_ended();
            break;
        case JD_APP_ERROR:
            jd_ui_draw_error(app.status);
            break;
        case JD_APP_MENU:
            jd_ui_draw_menu(app.menu_selected);
            break;
        case JD_APP_CATALOG_LOADING:
            jd_ui_draw_catalog(
                catalog_heading(app.catalog_kind),
                app.home_items, app.home_count, app.home_selected, 1
            );
            break;
        case JD_APP_CATALOG:
            jd_ui_draw_catalog(
                catalog_heading(app.catalog_kind),
                app.home_items, app.home_count, app.home_selected, 0
            );
            break;
        case JD_APP_PLAYING:
            break;
    }
    return 1;
}

void jd_app_system_pause(void) {
    if (app.system_resume_pending || app.paused || !app.stream_playing) return;
    if (app.mode != JD_APP_PLAYING && app.mode != JD_APP_BUFFERING &&
        app.mode != JD_APP_SEEK_BUFFERING) return;

    app.system_resume_pending = 1;
    app.mode_before_system_pause = app.mode;
    app.paused = 1;
    jd_audio_set_paused(1);
    queue_packet(JD_PACKET_PAUSE, NULL, 0);
    /* The update callback is suspended while the system menu is open, so give
       this small control packet one immediate opportunity to leave the queue.
       Do not enter the firmware receive path while the OS is suspending us. */
    jd_network_flush(&app.network);
}

void jd_app_system_resume(void) {
    if (!app.system_resume_pending) return;

    app.system_resume_pending = 0;
    app.paused = 0;
    app.mode = app.mode_before_system_pause;
    if (app.network.state == JD_NET_CONNECTED) {
        queue_packet(JD_PACKET_RESUME, NULL, 0);
        jd_network_flush(&app.network);
    }
    jd_audio_set_paused(app.mode_before_system_pause == JD_APP_PLAYING ? 0 : 1);
}

void jd_app_shutdown(void) {
    jd_audio_set_paused(1);
    queue_packet(JD_PACKET_STOP, NULL, 0);
    jd_network_flush(&app.network);
    jd_network_close(&app.network);
    jd_audio_shutdown();
    jd_ui_shutdown();
    app.pd->system->setAutoLockDisabled(0);
}
