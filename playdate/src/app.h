#pragma once

#include "pd_api.h"

void jd_app_init(PlaydateAPI* playdate);
int jd_app_update(void* userdata);
void jd_app_system_pause(void);
void jd_app_system_resume(void);
void jd_app_shutdown(void);
