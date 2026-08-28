#include "settings.h"

#include "config.h"

#include <stdio.h>
#include <string.h>

#define JD_SETTINGS_FILE "bridge-settings.dat"
#define JD_SETTINGS_MAGIC "JDS1"
#define JD_SETTINGS_BUFFER_CAPACITY 384

void jd_settings_defaults(JDSettings* settings) {
    memset(settings, 0, sizeof(*settings));
    snprintf(settings->host, sizeof(settings->host), "%s", JELLYDATE_BRIDGE_HOST);
    snprintf(settings->token, sizeof(settings->token), "%s", JELLYDATE_TOKEN);
    settings->port = JELLYDATE_STREAM_PORT;
}

int jd_settings_valid(const JDSettings* settings) {
    size_t host_length;
    size_t token_length;
    if (settings == NULL) return 0;
    host_length = strlen(settings->host);
    token_length = strlen(settings->token);
    return host_length > 0 && host_length < sizeof(settings->host) &&
           token_length >= 16 && token_length < sizeof(settings->token) &&
           settings->port > 0;
}

int jd_settings_load(PlaydateAPI* playdate, JDSettings* settings) {
    uint8_t bytes[JD_SETTINGS_BUFFER_CAPACITY];
    SDFile* file;
    int length;
    size_t cursor;
    uint8_t host_length;
    uint8_t token_length;
    JDSettings loaded;
    file = playdate->file->open(JD_SETTINGS_FILE, kFileReadData);
    if (file == NULL) return 0;
    length = playdate->file->read(file, bytes, sizeof(bytes));
    playdate->file->close(file);
    if (length < 8 || memcmp(bytes, JD_SETTINGS_MAGIC, 4) != 0) return 0;

    memset(&loaded, 0, sizeof(loaded));
    cursor = 4;
    host_length = bytes[cursor++];
    if (host_length == 0 || host_length >= sizeof(loaded.host) ||
        cursor + host_length + 1 > (size_t)length) return 0;
    memcpy(loaded.host, bytes + cursor, host_length);
    cursor += host_length;
    token_length = bytes[cursor++];
    if (token_length >= sizeof(loaded.token) ||
        cursor + token_length + 2 != (size_t)length) return 0;
    memcpy(loaded.token, bytes + cursor, token_length);
    cursor += token_length;
    loaded.port = (uint16_t)((bytes[cursor] << 8) | bytes[cursor + 1]);
    if (!jd_settings_valid(&loaded)) return 0;
    *settings = loaded;
    return 1;
}

int jd_settings_save(PlaydateAPI* playdate, const JDSettings* settings) {
    uint8_t bytes[JD_SETTINGS_BUFFER_CAPACITY];
    SDFile* file;
    size_t cursor = 0;
    size_t host_length;
    size_t token_length;
    int written;
    if (!jd_settings_valid(settings)) return 0;
    host_length = strlen(settings->host);
    token_length = strlen(settings->token);
    if (host_length > 255 || token_length > 255) return 0;

    memcpy(bytes + cursor, JD_SETTINGS_MAGIC, 4);
    cursor += 4;
    bytes[cursor++] = (uint8_t)host_length;
    memcpy(bytes + cursor, settings->host, host_length);
    cursor += host_length;
    bytes[cursor++] = (uint8_t)token_length;
    memcpy(bytes + cursor, settings->token, token_length);
    cursor += token_length;
    bytes[cursor++] = (uint8_t)(settings->port >> 8);
    bytes[cursor++] = (uint8_t)settings->port;

    file = playdate->file->open(JD_SETTINGS_FILE, kFileWrite);
    if (file == NULL) return 0;
    written = playdate->file->write(file, bytes, (unsigned int)cursor);
    if (written == (int)cursor) playdate->file->flush(file);
    playdate->file->close(file);
    return written == (int)cursor;
}
