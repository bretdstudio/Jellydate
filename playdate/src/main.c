#include "pd_api.h"
#include "app.h"

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* playdate, PDSystemEvent event, uint32_t arg) {
    (void)arg;
    switch (event) {
        case kEventInit:
            jd_app_init(playdate);
            playdate->system->setUpdateCallback(jd_app_update, NULL);
            break;
        case kEventPause:
            jd_app_system_pause();
            break;
        case kEventResume:
            jd_app_system_resume();
            break;
        case kEventTerminate:
            jd_app_shutdown();
            break;
        default:
            break;
    }
    return 0;
}
