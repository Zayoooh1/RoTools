#pragma once

#include <windows.h>

#define MULTIROBLOX_WINDOW_CLASS "MULTIROBLOX_WINDOW_CLASS"
#define WM_APP_ACCOUNT_REFRESH (WM_APP + 200)

#ifdef __cplusplus
extern "C" {
#endif

extern HWND g_hwndMain;

int RunGui(HINSTANCE hInstance, int nCmdShow);

#ifdef __cplusplus
}
#endif
