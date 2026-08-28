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
#define JD_CATALOG_TV 2
#define JD_CATALOG_RECENTLY_ADDED 3
#define JD_CATALOG_TV_SEASONS 4
#define JD_CATALOG_TV_EPISODES 5
#define JD_MENU_ITEM_COUNT 4
#define JD_ALPHA_BUCKET_COUNT 27

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
    JD_APP_MOVIE_INDEX,
    JD_APP_TV_INDEX,
    JD_APP_CATALOG_LOADING,
    JD_APP_CATALOG,
    JD_APP_DETAILS_LOADING,
    JD_APP_DETAILS
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
    int movie_index_active;
    int tv_index_active;
    int details_requested;
    int detail_active;
    int catalog_input_armed;
    int catalog_neutral_frames;
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
    uint32_t catalog_input_unlock_ms;
    uint64_t position_ms;
    uint64_t duration_ms;
    uint16_t video_x;
    uint16_t video_y;
    uint16_t video_width;
    uint16_t video_height;
    int home_count;
    int home_selected;
    int menu_selected;
    int movie_letter_selected;
    int movie_saved_letter;
    int movie_saved_selected;
    int tv_letter_selected;
    int tv_saved_letter;
    int tv_series_selected;
    int tv_season_selected;
    uint16_t tv_series_page_start;
    uint16_t tv_season_page_start;
    uint16_t movie_saved_page_start;
    int catalog_has_more;
    uint16_t catalog_page_start;
    JDHomeItem home_items[JD_HOME_MAX_ITEMS];
    JDItemDetails detail;
    char item_id[64];
    char detail_id[64];
    char title[96];
    char status[96];
    char catalog_parent_id[64];
    char catalog_title[96];
    char tv_series_id[64];
    char tv_series_title[96];
    char tv_bucket[2];
    char tv_season_id[64];
    char tv_season_title[96];
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
    if (kind == JD_CATALOG_MOVIES) return "MOVIES";
    if (kind == JD_CATALOG_TV) return "TV SHOWS";
    if (kind == JD_CATALOG_RECENTLY_ADDED) return "RECENTLY ADDED";
    return "CONTINUE WATCHING";
}

static char alphabet_bucket_label(int selected) {
    return selected == 0 ? '#' : (char)('A' + selected - 1);
}

static void reset_browser_input(void) {
    app.catalog_input_armed = 0;
    app.catalog_neutral_frames = 0;
    app.catalog_input_unlock_ms = app.pd->system->getCurrentTimeMilliseconds() + 250;
}

static void show_movie_index(void) {
    app.catalog_active = 0;
    app.movie_index_active = 1;
    app.tv_index_active = 0;
    app.catalog_requested = 0;
    reset_browser_input();
    app.mode = JD_APP_MOVIE_INDEX;
}

static void show_tv_index(void) {
    app.catalog_active = 0;
    app.movie_index_active = 0;
    app.tv_index_active = 1;
    app.catalog_requested = 0;
    reset_browser_input();
    app.mode = JD_APP_TV_INDEX;
}

static void queue_catalog_request(uint8_t kind, const char* parent_id) {
    uint8_t payload[68];
    size_t parent_length = parent_id == NULL ? 0 : strlen(parent_id);
    if (parent_length > 63) parent_length = 63;
    payload[0] = kind;
    payload[1] = (uint8_t)parent_length;
    if (parent_length > 0) memcpy(payload + 2, parent_id, parent_length);
    payload[2 + parent_length] = (uint8_t)(app.catalog_page_start >> 8);
    payload[3 + parent_length] = (uint8_t)app.catalog_page_start;
    queue_packet(JD_PACKET_HOME_REQUEST, payload, (uint32_t)(4 + parent_length));
    app.catalog_requested = 1;
    reset_browser_input();
    app.mode = JD_APP_CATALOG_LOADING;
}

static void queue_details_request(const char* item_id) {
    uint8_t payload[64];
    size_t id_length = item_id == NULL ? 0 : strlen(item_id);
    if (id_length == 0) return;
    if (id_length > 63) id_length = 63;
    payload[0] = (uint8_t)id_length;
    memcpy(payload + 1, item_id, id_length);
    queue_packet(JD_PACKET_ITEM_DETAILS_REQUEST, payload, (uint32_t)(id_length + 1));
    app.details_requested = 1;
    reset_browser_input();
    app.mode = JD_APP_DETAILS_LOADING;
}

static int parse_home_items(JDApp* state, const uint8_t* payload, size_t length) {
    size_t cursor = 0;
    int count;
    int index;
    if (length < 2) return 0;
    count = payload[cursor++];
    state->catalog_has_more = (payload[cursor++] & 1) != 0;
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

static int parse_item_details(JDApp* state, const uint8_t* payload, size_t length) {
    size_t cursor = 0;
    size_t copy;
    uint8_t field_length;
    uint16_t overview_length;
    JDItemDetails* detail = &state->detail;
    memset(detail, 0, sizeof(*detail));

    if (cursor >= length) return 0;
    field_length = payload[cursor++];
    if (field_length == 0 || field_length > length - cursor) return 0;
    copy = field_length < sizeof(detail->title) - 1
        ? field_length : sizeof(detail->title) - 1;
    memcpy(detail->title, payload + cursor, copy);
    detail->title[copy] = '\0';
    cursor += field_length;

    if (cursor >= length) return 0;
    field_length = payload[cursor++];
    if (field_length > length - cursor) return 0;
    copy = field_length < sizeof(detail->subtitle) - 1
        ? field_length : sizeof(detail->subtitle) - 1;
    memcpy(detail->subtitle, payload + cursor, copy);
    detail->subtitle[copy] = '\0';
    cursor += field_length;

    if (length - cursor < 2) return 0;
    overview_length = jd_protocol_read_u16(payload + cursor);
    cursor += 2;
    if (overview_length > length - cursor) return 0;
    copy = overview_length < sizeof(detail->overview) - 1
        ? overview_length : sizeof(detail->overview) - 1;
    memcpy(detail->overview, payload + cursor, copy);
    detail->overview[copy] = '\0';
    cursor += overview_length;

    if (length - cursor != 16) return 0;
    detail->position_ms = jd_protocol_read_u64(payload + cursor);
    detail->duration_ms = jd_protocol_read_u64(payload + cursor + 8);
    return 1;
}

static void begin_playback(
    const char* item_id,
    uint64_t position_ms,
    uint64_t duration_ms
) {
    snprintf(app.item_id, sizeof(app.item_id), "%s", item_id);
    app.position_ms = position_ms;
    app.duration_ms = duration_ms;
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
                } else if (state->detail_active) {
                    if (!state->details_requested) {
                        queue_details_request(state->detail_id);
                    }
                } else if (state->movie_index_active) {
                    state->mode = JD_APP_MOVIE_INDEX;
                } else if (state->tv_index_active) {
                    state->mode = JD_APP_TV_INDEX;
                } else if (state->catalog_active) {
                    if (!state->catalog_requested) {
                        queue_catalog_request(state->catalog_kind, state->catalog_parent_id);
                    }
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
        case JD_PACKET_ITEM_DETAILS_RESPONSE:
            if (!parse_item_details(state, payload, header->payload_length)) {
                snprintf(state->status, sizeof(state->status), "bad item details");
                state->mode = JD_APP_ERROR;
            } else {
                state->mode = JD_APP_DETAILS;
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
    app.movie_letter_selected = 1;
    app.movie_saved_letter = 1;
    app.tv_letter_selected = 1;
    app.tv_saved_letter = 1;
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
            app.details_requested = 0;
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
        if (browse_actions.horizontal != 0) {
            int row = app.menu_selected / 2;
            int column = app.menu_selected % 2;
            column = (column + browse_actions.horizontal + 2) % 2;
            app.menu_selected = row * 2 + column;
        } else if (browse_actions.vertical != 0) {
            int row = app.menu_selected / 2;
            int column = app.menu_selected % 2;
            row = (row + browse_actions.vertical + 2) % 2;
            app.menu_selected = row * 2 + column;
        } else if (browse_actions.movement != 0) {
            app.menu_selected += browse_actions.movement;
            while (app.menu_selected < 0) app.menu_selected += JD_MENU_ITEM_COUNT;
            while (app.menu_selected >= JD_MENU_ITEM_COUNT) app.menu_selected -= JD_MENU_ITEM_COUNT;
        }
        if (browse_actions.select) {
            app.catalog_kind = (uint8_t)app.menu_selected;
            app.catalog_active = app.catalog_kind != JD_CATALOG_MOVIES &&
                                 app.catalog_kind != JD_CATALOG_TV;
            app.movie_index_active = app.catalog_kind == JD_CATALOG_MOVIES;
            app.tv_index_active = app.catalog_kind == JD_CATALOG_TV;
            app.detail_active = 0;
            app.details_requested = 0;
            app.catalog_requested = 0;
            app.catalog_has_more = 0;
            app.catalog_page_start = 0;
            app.home_selected = 0;
            app.catalog_parent_id[0] = '\0';
            snprintf(
                app.catalog_title, sizeof(app.catalog_title), "%s",
                catalog_heading(app.catalog_kind)
            );
            if (app.catalog_kind == JD_CATALOG_TV) {
                app.tv_series_id[0] = '\0';
                app.tv_season_id[0] = '\0';
                app.tv_season_selected = 0;
                app.tv_season_page_start = 0;
            }
            if (app.catalog_kind == JD_CATALOG_MOVIES) {
                reset_browser_input();
                app.mode = JD_APP_MOVIE_INDEX;
            } else if (app.catalog_kind == JD_CATALOG_TV) {
                reset_browser_input();
                app.mode = JD_APP_TV_INDEX;
            } else {
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            }
        }
    } else if (app.mode == JD_APP_MOVIE_INDEX || app.mode == JD_APP_TV_INDEX) {
        int is_tv_index = app.mode == JD_APP_TV_INDEX;
        int* letter_selected = is_tv_index
            ? &app.tv_letter_selected : &app.movie_letter_selected;
        int saved_letter = is_tv_index
            ? app.tv_saved_letter : app.movie_saved_letter;
        browse_actions = jd_controls_update_browser(&app.controls, app.pd);
        if (!app.catalog_input_armed) {
            if (!browse_actions.select_held && !browse_actions.back_held &&
                !browse_actions.select && !browse_actions.back &&
                browse_actions.movement == 0) {
                app.catalog_neutral_frames += 1;
                if (app.catalog_neutral_frames >= 2) app.catalog_input_armed = 1;
            } else {
                app.catalog_neutral_frames = 0;
            }
            memset(&browse_actions, 0, sizeof(browse_actions));
        } else if ((int32_t)(now - app.catalog_input_unlock_ms) < 0) {
            memset(&browse_actions, 0, sizeof(browse_actions));
        }
        if (browse_actions.back) {
            if (is_tv_index) app.tv_index_active = 0;
            else app.movie_index_active = 0;
            app.mode = JD_APP_MENU;
        } else if (browse_actions.horizontal != 0) {
            int row = *letter_selected / 9;
            int column = *letter_selected % 9;
            column = (column + browse_actions.horizontal + 9) % 9;
            *letter_selected = row * 9 + column;
        } else if (browse_actions.vertical != 0) {
            int row = *letter_selected / 9;
            int column = *letter_selected % 9;
            row = (row + browse_actions.vertical + 3) % 3;
            *letter_selected = row * 9 + column;
        } else if (browse_actions.movement != 0) {
            *letter_selected += browse_actions.movement;
            while (*letter_selected < 0) {
                *letter_selected += JD_ALPHA_BUCKET_COUNT;
            }
            while (*letter_selected >= JD_ALPHA_BUCKET_COUNT) {
                *letter_selected -= JD_ALPHA_BUCKET_COUNT;
            }
        }
        if (browse_actions.select) {
            char bucket = alphabet_bucket_label(*letter_selected);
            app.catalog_kind = is_tv_index ? JD_CATALOG_TV : JD_CATALOG_MOVIES;
            app.catalog_active = 1;
            app.movie_index_active = 0;
            app.tv_index_active = 0;
            app.catalog_has_more = 0;
            app.catalog_parent_id[0] = bucket;
            app.catalog_parent_id[1] = '\0';
            snprintf(
                app.catalog_title, sizeof(app.catalog_title),
                is_tv_index ? "TV SHOWS - %c" : "MOVIES - %c", bucket
            );
            if (is_tv_index) {
                app.tv_bucket[0] = bucket;
                app.tv_bucket[1] = '\0';
            }
            if (*letter_selected == saved_letter) {
                app.catalog_page_start = is_tv_index
                    ? app.tv_series_page_start : app.movie_saved_page_start;
                app.home_selected = is_tv_index
                    ? app.tv_series_selected : app.movie_saved_selected;
            } else {
                app.catalog_page_start = 0;
                app.home_selected = 0;
            }
            app.catalog_requested = 0;
            queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
        }
    } else if (app.mode == JD_APP_CATALOG) {
        browse_actions = jd_controls_update_browser(&app.controls, app.pd);
        if (!app.catalog_input_armed) {
            if (!browse_actions.select_held && !browse_actions.back_held &&
                !browse_actions.select && !browse_actions.back &&
                browse_actions.movement == 0) {
                app.catalog_neutral_frames += 1;
                if (app.catalog_neutral_frames >= 2) app.catalog_input_armed = 1;
            } else {
                app.catalog_neutral_frames = 0;
            }
            memset(&browse_actions, 0, sizeof(browse_actions));
        } else if ((int32_t)(now - app.catalog_input_unlock_ms) < 0) {
            memset(&browse_actions, 0, sizeof(browse_actions));
        }
        if (browse_actions.back) {
            if (app.catalog_kind == JD_CATALOG_TV_EPISODES) {
                app.catalog_kind = JD_CATALOG_TV_SEASONS;
                app.catalog_has_more = 0;
                app.catalog_page_start = app.tv_season_page_start;
                snprintf(
                    app.catalog_parent_id, sizeof(app.catalog_parent_id), "%s",
                    app.tv_series_id
                );
                snprintf(
                    app.catalog_title, sizeof(app.catalog_title), "%s",
                    app.tv_series_title
                );
                app.home_selected = app.tv_season_selected;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            } else if (app.catalog_kind == JD_CATALOG_TV_SEASONS) {
                app.catalog_kind = JD_CATALOG_TV;
                app.catalog_has_more = 0;
                app.catalog_page_start = app.tv_series_page_start;
                snprintf(
                    app.catalog_parent_id, sizeof(app.catalog_parent_id), "%s",
                    app.tv_bucket
                );
                snprintf(
                    app.catalog_title, sizeof(app.catalog_title),
                    "TV SHOWS - %c", app.tv_bucket[0]
                );
                app.home_selected = app.tv_series_selected;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            } else if (app.catalog_kind == JD_CATALOG_TV &&
                       app.catalog_parent_id[0] != '\0') {
                app.tv_saved_letter = app.tv_letter_selected;
                app.tv_series_page_start = app.catalog_page_start;
                app.tv_series_selected = app.home_selected;
                show_tv_index();
            } else if (app.catalog_kind == JD_CATALOG_MOVIES &&
                       app.catalog_parent_id[0] != '\0') {
                app.movie_saved_letter = app.movie_letter_selected;
                app.movie_saved_page_start = app.catalog_page_start;
                app.movie_saved_selected = app.home_selected;
                show_movie_index();
            } else {
                app.catalog_active = 0;
                app.catalog_requested = 0;
                app.movie_index_active = 0;
                app.tv_index_active = 0;
                app.detail_active = 0;
                app.mode = JD_APP_MENU;
            }
        } else if (app.home_count > 0 &&
                   (app.catalog_kind == JD_CATALOG_MOVIES ||
                    app.catalog_kind == JD_CATALOG_TV) &&
                   app.catalog_parent_id[0] != '\0' &&
                   browse_actions.horizontal != 0) {
            if (browse_actions.horizontal > 0 && app.catalog_has_more) {
                app.catalog_page_start += (uint16_t)app.home_count;
                app.home_selected = 0;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            } else if (browse_actions.horizontal < 0 && app.catalog_page_start > 0) {
                app.catalog_page_start = app.catalog_page_start >= JD_HOME_MAX_ITEMS
                    ? app.catalog_page_start - JD_HOME_MAX_ITEMS
                    : 0;
                app.home_selected = 0;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            }
        } else if (app.home_count > 0 && browse_actions.movement != 0) {
            if (browse_actions.movement > 0) {
                if (app.home_selected < app.home_count - 1) {
                    app.home_selected += 1;
                } else if (app.catalog_has_more) {
                    app.catalog_page_start += (uint16_t)app.home_count;
                    app.home_selected = 0;
                    app.catalog_requested = 0;
                    queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
                }
            } else if (app.home_selected > 0) {
                app.home_selected -= 1;
            } else if (app.catalog_page_start > 0) {
                app.catalog_page_start = app.catalog_page_start >= JD_HOME_MAX_ITEMS
                    ? app.catalog_page_start - JD_HOME_MAX_ITEMS
                    : 0;
                app.home_selected = JD_HOME_MAX_ITEMS - 1;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            }
        }
        if (app.mode == JD_APP_CATALOG && !browse_actions.back &&
            app.home_count > 0 && browse_actions.select) {
            JDHomeItem* selected = &app.home_items[app.home_selected];
            if (app.catalog_kind == JD_CATALOG_TV) {
                app.tv_series_selected = app.home_selected;
                app.tv_series_page_start = app.catalog_page_start;
                snprintf(app.tv_series_id, sizeof(app.tv_series_id), "%s", selected->id);
                snprintf(app.tv_series_title, sizeof(app.tv_series_title), "%s", selected->title);
                snprintf(app.catalog_parent_id, sizeof(app.catalog_parent_id), "%s", selected->id);
                snprintf(app.catalog_title, sizeof(app.catalog_title), "%s", selected->title);
                app.catalog_kind = JD_CATALOG_TV_SEASONS;
                app.catalog_has_more = 0;
                app.catalog_page_start = 0;
                app.home_selected = 0;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            } else if (app.catalog_kind == JD_CATALOG_TV_SEASONS) {
                app.tv_season_selected = app.home_selected;
                app.tv_season_page_start = app.catalog_page_start;
                snprintf(app.tv_season_id, sizeof(app.tv_season_id), "%s", selected->id);
                snprintf(app.tv_season_title, sizeof(app.tv_season_title), "%s", selected->title);
                snprintf(app.catalog_parent_id, sizeof(app.catalog_parent_id), "%s", selected->id);
                snprintf(app.catalog_title, sizeof(app.catalog_title), "%s", selected->title);
                app.catalog_kind = JD_CATALOG_TV_EPISODES;
                app.catalog_has_more = 0;
                app.catalog_page_start = 0;
                app.home_selected = 0;
                app.catalog_requested = 0;
                queue_catalog_request(app.catalog_kind, app.catalog_parent_id);
            } else {
                snprintf(app.detail_id, sizeof(app.detail_id), "%s", selected->id);
                memset(&app.detail, 0, sizeof(app.detail));
                snprintf(app.detail.title, sizeof(app.detail.title), "%s", selected->title);
                app.detail.position_ms = selected->position_ms;
                app.detail.duration_ms = selected->duration_ms;
                app.detail_active = 1;
                app.details_requested = 0;
                queue_details_request(app.detail_id);
            }
        }
    } else if (app.mode == JD_APP_DETAILS) {
        browse_actions = jd_controls_update_browser(&app.controls, app.pd);
        if (!app.catalog_input_armed) {
            if (!browse_actions.select_held && !browse_actions.back_held &&
                !browse_actions.select && !browse_actions.back &&
                browse_actions.movement == 0) {
                app.catalog_neutral_frames += 1;
                if (app.catalog_neutral_frames >= 2) app.catalog_input_armed = 1;
            } else {
                app.catalog_neutral_frames = 0;
            }
            memset(&browse_actions, 0, sizeof(browse_actions));
        } else if ((int32_t)(now - app.catalog_input_unlock_ms) < 0) {
            memset(&browse_actions, 0, sizeof(browse_actions));
        }
        if (browse_actions.back) {
            app.detail_active = 0;
            app.details_requested = 0;
            app.mode = JD_APP_CATALOG;
        } else if (browse_actions.select) {
            begin_playback(
                app.detail_id,
                app.detail.position_ms,
                app.detail.duration_ms
            );
        }
    } else if (app.mode == JD_APP_CATALOG_LOADING ||
               app.mode == JD_APP_DETAILS_LOADING) {
        /* Consume button transitions while a catalog request is in flight so
           one A/B press cannot activate two hierarchy levels. */
        (void)jd_controls_update_browser(&app.controls, app.pd);
    } else if (app.mode != JD_APP_TUNING) {
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
        if (app.detail_active) {
            app.detail.position_ms = app.position_ms;
            app.detail.duration_ms = app.duration_ms;
            if (app.home_selected >= 0 && app.home_selected < app.home_count &&
                strcmp(app.home_items[app.home_selected].id, app.detail_id) == 0) {
                app.home_items[app.home_selected].position_ms = app.position_ms;
                app.home_items[app.home_selected].duration_ms = app.duration_ms;
            }
            app.mode = JD_APP_DETAILS;
        } else {
            app.mode = app.catalog_active ? JD_APP_CATALOG : JD_APP_MENU;
        }
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
        case JD_APP_MOVIE_INDEX:
            jd_ui_draw_alpha_index("MOVIES A-Z", app.movie_letter_selected);
            break;
        case JD_APP_TV_INDEX:
            jd_ui_draw_alpha_index("TV SHOWS A-Z", app.tv_letter_selected);
            break;
        case JD_APP_CATALOG_LOADING:
            jd_ui_draw_catalog(
                app.catalog_title,
                app.home_items, app.home_count, app.home_selected, 1
            );
            break;
        case JD_APP_CATALOG:
            jd_ui_draw_catalog(
                app.catalog_title,
                app.home_items, app.home_count, app.home_selected, 0
            );
            break;
        case JD_APP_DETAILS_LOADING:
            jd_ui_draw_details(&app.detail, 1);
            break;
        case JD_APP_DETAILS:
            jd_ui_draw_details(&app.detail, 0);
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
