#pragma once

#include "pd_api.h"

#define JD_SETTINGS_HOST_CAPACITY 128
#define JD_SETTINGS_TOKEN_CAPACITY 192

typedef struct {
    char host[JD_SETTINGS_HOST_CAPACITY];
    char token[JD_SETTINGS_TOKEN_CAPACITY];
    uint16_t port;
} JDSettings;

void jd_settings_defaults(JDSettings* settings);
int jd_settings_load(PlaydateAPI* playdate, JDSettings* settings);
int jd_settings_save(PlaydateAPI* playdate, const JDSettings* settings);
int jd_settings_valid(const JDSettings* settings);
