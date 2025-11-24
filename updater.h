#pragma once

#include <windows.h>

typedef enum UpdateCheckFrequency {
    UPDATE_FREQUENCY_EVERYDAY = 0,
    UPDATE_FREQUENCY_WEEKLY = 1,
    UPDATE_FREQUENCY_MONTHLY = 2,
    UPDATE_FREQUENCY_NEVER = 3
} UpdateCheckFrequency;

typedef struct UpdateAvailableInfo {
    char version[64];
    char downloadUrl[1024];
} UpdateAvailableInfo;

#define WM_APP_UPDATE_AVAILABLE (WM_APP + 201)

#ifdef __cplusplus
extern "C" {
#endif

void Updater_BeginStartupCheck(HWND hwndOwner);
BOOL Updater_PerformSelfUpdate(HWND hwndOwner, const UpdateAvailableInfo *info);
void Updater_FreeInfo(UpdateAvailableInfo *info);
UpdateCheckFrequency Updater_GetFrequency(void);
BOOL Updater_SetFrequency(UpdateCheckFrequency frequency);

#ifdef __cplusplus
}
#endif
