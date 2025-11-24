#pragma once

#include <windows.h>

// Append an arbitrary error message to the development log.
#ifdef __cplusplus
extern "C" {
#endif

void log_error(const char *message);

// Append a Win32 error entry with context and the numeric code to the log.
void log_win_error(const char *context, DWORD errorCode);

#ifdef __cplusplus
}
#endif
