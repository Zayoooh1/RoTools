#include "log.h"

#include <stdio.h>
#include <windows.h>

static const char LOG_FILE_NAME[] = "DvlErrLog.txt";

static void WriteTimestamp(FILE *file)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    fprintf(file, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
            now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
}

static void FlushToDebugConsole(const char *line)
{
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
}

static void AppendLogLine(const char *line)
{
    FILE *file = fopen(LOG_FILE_NAME, "a");
    if (file) {
        WriteTimestamp(file);
        fprintf(file, "%s\n", line);
        fclose(file);
    }
    FlushToDebugConsole(line);
}

void log_error(const char *message)
{
    char entry[1024];
    if (!message) {
        message = "(null)";
    }
    snprintf(entry, sizeof(entry), "ERROR: %s", message);
    AppendLogLine(entry);
}

void log_win_error(const char *context, DWORD errorCode)
{
    char resolvedMessage[512] = { 0 };
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        0,
        resolvedMessage,
        (DWORD)sizeof(resolvedMessage),
        NULL);

    if (length == 0) {
        snprintf(resolvedMessage, sizeof(resolvedMessage), "Win32 error 0x%08X", (unsigned int)errorCode);
    }

    char entry[1024];
    snprintf(entry, sizeof(entry), "WIN32 ERROR (%s): 0x%08X (%u) - %s",
             context ? context : "unknown", (unsigned int)errorCode,
             (unsigned int)errorCode, resolvedMessage);
    AppendLogLine(entry);
}
