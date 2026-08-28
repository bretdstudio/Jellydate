#pragma once

/* Local hardware builds generate this git-ignored header from bridge/.env. */
#if defined(__has_include)
#if __has_include("config_private.h")
#include "config_private.h"
#endif
#endif

/* Safe simulator/development fallbacks. Never commit a real token here. */
#ifndef JELLYDATE_BRIDGE_HOST
#define JELLYDATE_BRIDGE_HOST "127.0.0.1"
#endif
#define JELLYDATE_STREAM_PORT 7790
#ifndef JELLYDATE_TOKEN
#define JELLYDATE_TOKEN "replace-with-a-long-random-token"
#endif

/* Use "__test__" with TEST_MEDIA_PATH on the bridge, or paste a Jellyfin id. */
#define JELLYDATE_ITEM_ID "3b8c72367f186fe65849def22fc1b6e3"
