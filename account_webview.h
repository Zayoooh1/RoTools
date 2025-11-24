#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opens the Chromium-based login window; returns TRUE if login succeeded and an account was created.
BOOL AM_OpenBrowserLoginAndAddAccount(HWND parent);

#ifdef __cplusplus
}
#endif
