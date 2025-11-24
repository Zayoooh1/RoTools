#include "updater.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <winhttp.h>

#include "log.h"
#include "version.h"

#define UPDATE_SETTINGS_FILENAME "UpdateSettings.txt"
#define USER_AGENT_TEXT L"MultiRobloxUpdater/1.0"
#define UPDATE_DEFAULT_FREQUENCY UPDATE_FREQUENCY_EVERYDAY

typedef struct UpdateThreadParams {
    HWND hwndOwner;
} UpdateThreadParams;

typedef struct UpdateSettings {
    time_t lastCheck;
    UpdateCheckFrequency frequency;
} UpdateSettings;

static LONG g_updateThreadStarted = 0;

static DWORD WINAPI UpdateThreadProc(LPVOID param);
static BOOL GetUpdateSettingsPath(char *buffer, size_t size);
static void LoadUpdateSettings(UpdateSettings *settings);
static void SaveUpdateSettings(const UpdateSettings *settings);
static DWORD GetFrequencyIntervalSeconds(UpdateCheckFrequency frequency);
static BOOL FetchLatestRelease(UpdateAvailableInfo *info);
static BOOL DownloadLatestReleaseJson(char **jsonBuffer);
static BOOL ParseReleaseJson(const char *json, UpdateAvailableInfo *info);
static BOOL ExtractJsonStringValue(const char *start, char *output, size_t outputSize, const char **nextOut);
static BOOL ExtractTagName(const char *json, char *tag, size_t tagSize);
static BOOL ExtractDownloadUrl(const char *json, char *url, size_t urlSize);
static int CompareSemanticVersions(const char *current, const char *latest);
static BOOL DownloadFileFromUrl(const char *url, const char *destinationPath);
static BOOL SplitUrlComponents(const char *url, BOOL *useHttps, INTERNET_PORT *port, char *hostBuffer, size_t hostSize, char *pathBuffer, size_t pathSize);
static BOOL BuildTempPath(const char *fileName, char *output, size_t size);
static BOOL WriteUpdateScript(const char *scriptPath, const char *tempExePath, const char *targetExePath);
static BOOL LaunchScript(const char *scriptPath);

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

void Updater_BeginStartupCheck(HWND hwndOwner)
{
    if (!hwndOwner) {
        return;
    }

    if (InterlockedCompareExchange(&g_updateThreadStarted, 1, 0) != 0) {
        return;
    }

    UpdateThreadParams *params = (UpdateThreadParams *)malloc(sizeof(UpdateThreadParams));
    if (!params) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return;
    }
    params->hwndOwner = hwndOwner;

    HANDLE hThread = CreateThread(NULL, 0, UpdateThreadProc, params, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        free(params);
        InterlockedExchange(&g_updateThreadStarted, 0);
    }
}

BOOL Updater_PerformSelfUpdate(HWND hwndOwner, const UpdateAvailableInfo *info)
{
    (void)hwndOwner;

    if (!info || info->downloadUrl[0] == '\0') {
        return FALSE;
    }

    char tempExePath[MAX_PATH] = {0};
    char scriptPath[MAX_PATH] = {0};
    char currentExePath[MAX_PATH] = {0};

    if (!BuildTempPath(APP_TEMP_UPDATE_EXE, tempExePath, sizeof(tempExePath))) {
        log_error("Updater: Failed to build temp exe path.");
        return FALSE;
    }

    if (!BuildTempPath(APP_UPDATE_SCRIPT_NAME, scriptPath, sizeof(scriptPath))) {
        log_error("Updater: Failed to build update script path.");
        return FALSE;
    }

    DWORD exeLen = GetModuleFileNameA(NULL, currentExePath, ARRAYSIZE(currentExePath));
    if (exeLen == 0 || exeLen >= ARRAYSIZE(currentExePath)) {
        log_error("Updater: Unable to determine current executable path.");
        return FALSE;
    }

    if (!DownloadFileFromUrl(info->downloadUrl, tempExePath)) {
        log_error("Updater: Failed to download update payload.");
        return FALSE;
    }

    if (!WriteUpdateScript(scriptPath, tempExePath, currentExePath)) {
        log_error("Updater: Failed to write update script.");
        DeleteFileA(tempExePath);
        return FALSE;
    }

    if (!LaunchScript(scriptPath)) {
        log_error("Updater: Failed to launch update script.");
        DeleteFileA(tempExePath);
        DeleteFileA(scriptPath);
        return FALSE;
    }

    return TRUE;
}

void Updater_FreeInfo(UpdateAvailableInfo *info)
{
    if (info) {
        free(info);
    }
}

UpdateCheckFrequency Updater_GetFrequency(void)
{
    UpdateSettings settings = {0};
    LoadUpdateSettings(&settings);
    if (settings.frequency < UPDATE_FREQUENCY_EVERYDAY || settings.frequency > UPDATE_FREQUENCY_NEVER) {
        return UPDATE_DEFAULT_FREQUENCY;
    }
    return settings.frequency;
}

BOOL Updater_SetFrequency(UpdateCheckFrequency frequency)
{
    if (frequency < UPDATE_FREQUENCY_EVERYDAY || frequency > UPDATE_FREQUENCY_NEVER) {
        frequency = UPDATE_DEFAULT_FREQUENCY;
    }

    UpdateSettings settings = {0};
    LoadUpdateSettings(&settings);
    settings.frequency = frequency;
    SaveUpdateSettings(&settings);
    return TRUE;
}

static DWORD WINAPI UpdateThreadProc(LPVOID param)
{
    UpdateThreadParams *params = (UpdateThreadParams *)param;
    HWND hwnd = params ? params->hwndOwner : NULL;
    if (params) {
        free(params);
    }

    if (!hwnd) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    UpdateSettings settings = {0};
    LoadUpdateSettings(&settings);

    UpdateCheckFrequency frequency = settings.frequency;
    if (frequency < UPDATE_FREQUENCY_EVERYDAY || frequency > UPDATE_FREQUENCY_NEVER) {
        frequency = UPDATE_DEFAULT_FREQUENCY;
    }

    if (frequency == UPDATE_FREQUENCY_NEVER) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    DWORD intervalSeconds = GetFrequencyIntervalSeconds(frequency);

    time_t now = time(NULL);
    if (now == (time_t)-1) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    BOOL recentlyChecked = FALSE;
    if (settings.lastCheck != 0 && intervalSeconds > 0) {
        double diff = difftime(now, settings.lastCheck);
        if (diff >= 0 && diff < (double)intervalSeconds) {
            recentlyChecked = TRUE;
        }
    }

    if (recentlyChecked) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    settings.lastCheck = now;
    settings.frequency = frequency;
    SaveUpdateSettings(&settings);

    UpdateAvailableInfo latest = {0};
    if (!FetchLatestRelease(&latest)) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    if (CompareSemanticVersions(APP_VERSION, latest.version) >= 0) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }

    UpdateAvailableInfo *payload = (UpdateAvailableInfo *)malloc(sizeof(UpdateAvailableInfo));
    if (!payload) {
        InterlockedExchange(&g_updateThreadStarted, 0);
        return 0;
    }
    memcpy(payload, &latest, sizeof(UpdateAvailableInfo));

    if (!PostMessage(hwnd, WM_APP_UPDATE_AVAILABLE, 0, (LPARAM)payload)) {
        free(payload);
    }

    InterlockedExchange(&g_updateThreadStarted, 0);
    return 0;
}

static BOOL GetUpdateSettingsPath(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return FALSE;
    }

    char modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, modulePath, ARRAYSIZE(modulePath));
    if (len == 0 || len >= ARRAYSIZE(modulePath)) {
        return FALSE;
    }

    char *lastSlash = strrchr(modulePath, '\\');
    size_t dirLen = 0;
    if (lastSlash) {
        dirLen = (size_t)(lastSlash - modulePath + 1);
    }

    size_t required = dirLen + strlen(UPDATE_SETTINGS_FILENAME) + 1;
    if (required > size) {
        return FALSE;
    }

    memcpy(buffer, modulePath, dirLen);
    buffer[dirLen] = '\0';
    strcpy(buffer + dirLen, UPDATE_SETTINGS_FILENAME);
    return TRUE;
}

static void LoadUpdateSettings(UpdateSettings *settings)
{
    if (!settings) {
        return;
    }

    settings->lastCheck = 0;
    settings->frequency = UPDATE_DEFAULT_FREQUENCY;

    char path[MAX_PATH] = {0};
    if (!GetUpdateSettingsPath(path, sizeof(path))) {
        return;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }

    char line[128] = {0};
    while (fgets(line, sizeof(line), file)) {
        long long value = 0;
        if (sscanf(line, "LastCheckTimestamp=%lld", &value) == 1) {
            settings->lastCheck = (time_t)value;
        } else if (sscanf(line, "Frequency=%lld", &value) == 1) {
            int freqValue = (int)value;
            if (freqValue < UPDATE_FREQUENCY_EVERYDAY || freqValue > UPDATE_FREQUENCY_NEVER) {
                freqValue = UPDATE_DEFAULT_FREQUENCY;
            }
            settings->frequency = (UpdateCheckFrequency)freqValue;
        }
    }

    fclose(file);
}

static void SaveUpdateSettings(const UpdateSettings *settings)
{
    if (!settings) {
        return;
    }

    char path[MAX_PATH] = {0};
    if (!GetUpdateSettingsPath(path, sizeof(path))) {
        return;
    }

    FILE *file = fopen(path, "w");
    if (!file) {
        log_error("Updater: Unable to open UpdateSettings.txt for writing.");
        return;
    }

    fprintf(file, "LastCheckTimestamp=%lld\n", (long long)settings->lastCheck);
    fprintf(file, "Frequency=%d\n", (int)settings->frequency);
    fclose(file);
}

static DWORD GetFrequencyIntervalSeconds(UpdateCheckFrequency frequency)
{
    switch (frequency) {
        case UPDATE_FREQUENCY_WEEKLY:
            return 7 * 24 * 60 * 60;
        case UPDATE_FREQUENCY_MONTHLY:
            return 30 * 24 * 60 * 60;
        case UPDATE_FREQUENCY_NEVER:
            return 0;
        case UPDATE_FREQUENCY_EVERYDAY:
        default:
            return 24 * 60 * 60;
    }
}

static BOOL FetchLatestRelease(UpdateAvailableInfo *info)
{
    if (!info) {
        return FALSE;
    }

    char *jsonBuffer = NULL;
    BOOL result = DownloadLatestReleaseJson(&jsonBuffer);
    if (!result || !jsonBuffer) {
        if (jsonBuffer) {
            free(jsonBuffer);
        }
        return FALSE;
    }

    BOOL parsed = ParseReleaseJson(jsonBuffer, info);
    free(jsonBuffer);
    return parsed;
}

static BOOL DownloadLatestReleaseJson(char **jsonBuffer)
{
    if (!jsonBuffer) {
        return FALSE;
    }

    *jsonBuffer = NULL;

    HINTERNET hSession = WinHttpOpen(USER_AGENT_TEXT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log_win_error("WinHttpOpen", GetLastError());
        return FALSE;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        log_win_error("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        L"/repos/Zayo/RoTools/releases/latest",
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        log_win_error("WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    WinHttpAddRequestHeaders(hRequest, L"User-Agent: MultiRobloxUpdater/1.0\r\n", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL sent = WinHttpSendRequest(hRequest,
                                   WINHTTP_NO_ADDITIONAL_HEADERS,
                                   0,
                                   WINHTTP_NO_REQUEST_DATA,
                                   0,
                                   0,
                                   0);
    if (!sent) {
        log_win_error("WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        log_win_error("WinHttpReceiveResponse", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status,
                            &statusSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        if (status != 200) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Updater: GitHub API returned status %lu", status);
            log_error(msg);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return FALSE;
        }
    }

    BYTE chunk[4096];
    DWORD bytesRead = 0;
    DWORD total = 0;
    char *buffer = NULL;

    do {
        if (!WinHttpReadData(hRequest, chunk, sizeof(chunk), &bytesRead)) {
            log_win_error("WinHttpReadData", GetLastError());
            free(buffer);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return FALSE;
        }

        if (bytesRead == 0) {
            break;
        }

        char *newBuffer = (char *)realloc(buffer, total + bytesRead + 1);
        if (!newBuffer) {
            free(buffer);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return FALSE;
        }
        buffer = newBuffer;
        memcpy(buffer + total, chunk, bytesRead);
        total += bytesRead;
        buffer[total] = '\0';
    } while (bytesRead > 0);

    if (!buffer) {
        buffer = (char *)malloc(1);
        if (!buffer) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return FALSE;
        }
        buffer[0] = '\0';
    }

    *jsonBuffer = buffer;

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return TRUE;
}

static BOOL ParseReleaseJson(const char *json, UpdateAvailableInfo *info)
{
    if (!json || !info) {
        return FALSE;
    }

    if (!ExtractTagName(json, info->version, ARRAYSIZE(info->version))) {
        log_error("Updater: Failed to parse tag_name from GitHub response.");
        return FALSE;
    }

    if (!ExtractDownloadUrl(json, info->downloadUrl, ARRAYSIZE(info->downloadUrl))) {
        log_error("Updater: Failed to parse browser_download_url from GitHub response.");
        return FALSE;
    }

    return TRUE;
}

static BOOL ExtractJsonStringValue(const char *start, char *output, size_t outputSize, const char **nextOut)
{
    if (!start || !output || outputSize == 0) {
        return FALSE;
    }

    const char *colon = strchr(start, ':');
    if (!colon) {
        return FALSE;
    }

    const char *cursor = colon + 1;
    while (*cursor && isspace((unsigned char)*cursor)) {
        ++cursor;
    }

    if (*cursor != '"') {
        return FALSE;
    }
    ++cursor;

    size_t index = 0;
    while (*cursor) {
        if (*cursor == '\\' && cursor[1]) {
            cursor++;
        } else if (*cursor == '"') {
            break;
        }

        if (index + 1 < outputSize) {
            output[index++] = *cursor;
        }
        cursor++;
    }

    if (*cursor != '"') {
        return FALSE;
    }
    output[index] = '\0';
    cursor++;

    if (nextOut) {
        *nextOut = cursor;
    }

    return index > 0;
}

static BOOL ExtractTagName(const char *json, char *tag, size_t tagSize)
{
    const char *pattern = "\"tag_name\"";
    const char *location = strstr(json, pattern);
    if (!location) {
        return FALSE;
    }

    return ExtractJsonStringValue(location + strlen(pattern), tag, tagSize, NULL);
}

static BOOL ExtractDownloadUrl(const char *json, char *url, size_t urlSize)
{
    const char *pattern = "\"browser_download_url\"";
    const char *cursor = json;
    while ((cursor = strstr(cursor, pattern)) != NULL) {
        const char *next = NULL;
        if (ExtractJsonStringValue(cursor + strlen(pattern), url, urlSize, &next)) {
            if (strstr(url, ".exe") != NULL) {
                return TRUE;
            }
        }
        if (!next) {
            next = cursor + strlen(pattern);
        }
        cursor = next;
    }

    return FALSE;
}

static int CompareSemanticVersions(const char *current, const char *latest)
{
    int currentParts[3] = {0, 0, 0};
    int latestParts[3] = {0, 0, 0};

    const char *ptr = current;
    if (ptr && (*ptr == 'v' || *ptr == 'V')) {
        ++ptr;
    }
    for (int i = 0; ptr && i < 3; ++i) {
        currentParts[i] = atoi(ptr);
        const char *dot = strchr(ptr, '.');
        if (!dot) {
            break;
        }
        ptr = dot + 1;
    }

    ptr = latest;
    if (ptr && (*ptr == 'v' || *ptr == 'V')) {
        ++ptr;
    }
    for (int i = 0; ptr && i < 3; ++i) {
        latestParts[i] = atoi(ptr);
        const char *dot = strchr(ptr, '.');
        if (!dot) {
            break;
        }
        ptr = dot + 1;
    }

    for (int i = 0; i < 3; ++i) {
        if (currentParts[i] < latestParts[i]) {
            return -1;
        }
        if (currentParts[i] > latestParts[i]) {
            return 1;
        }
    }
    return 0;
}

static BOOL SplitUrlComponents(const char *url, BOOL *useHttps, INTERNET_PORT *port, char *hostBuffer, size_t hostSize, char *pathBuffer, size_t pathSize)
{
    if (!url || !hostBuffer || !pathBuffer || hostSize == 0 || pathSize == 0) {
        return FALSE;
    }

    const char *cursor = url;
    BOOL https = FALSE;

    if (_strnicmp(url, "https://", 8) == 0) {
        https = TRUE;
        cursor = url + 8;
    } else if (_strnicmp(url, "http://", 7) == 0) {
        https = FALSE;
        cursor = url + 7;
    } else {
        return FALSE;
    }

    const char *slash = strchr(cursor, '/');
    size_t hostLen = 0;
    if (slash) {
        hostLen = (size_t)(slash - cursor);
    } else {
        hostLen = strlen(cursor);
    }

    if (hostLen == 0 || hostLen >= hostSize) {
        return FALSE;
    }

    memcpy(hostBuffer, cursor, hostLen);
    hostBuffer[hostLen] = '\0';

    if (slash) {
        size_t pathLen = strlen(slash);
        if (pathLen >= pathSize) {
            return FALSE;
        }
        memcpy(pathBuffer, slash, pathLen + 1);
    } else {
        if (pathSize < 2) {
            return FALSE;
        }
        pathBuffer[0] = '/';
        pathBuffer[1] = '\0';
    }

    if (useHttps) {
        *useHttps = https;
    }
    if (port) {
        *port = https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    }

    return TRUE;
}

static BOOL DownloadFileFromUrl(const char *url, const char *destinationPath)
{
    char host[256] = {0};
    char path[1024] = {0};
    BOOL useHttps = TRUE;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;

    if (!SplitUrlComponents(url, &useHttps, &port, host, sizeof(host), path, sizeof(path))) {
        log_error("Updater: Invalid download URL.");
        return FALSE;
    }

    wchar_t whost[256];
    wchar_t wpath[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, ARRAYSIZE(whost))) {
        return FALSE;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, ARRAYSIZE(wpath))) {
        return FALSE;
    }

    HINTERNET hSession = WinHttpOpen(USER_AGENT_TEXT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        log_win_error("WinHttpOpen", GetLastError());
        return FALSE;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, whost, port, 0);
    if (!hConnect) {
        log_win_error("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        wpath,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest) {
        log_win_error("WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    WinHttpAddRequestHeaders(hRequest, L"User-Agent: MultiRobloxUpdater/1.0\r\n", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL sent = WinHttpSendRequest(hRequest,
                                   WINHTTP_NO_ADDITIONAL_HEADERS,
                                   0,
                                   WINHTTP_NO_REQUEST_DATA,
                                   0,
                                   0,
                                   0);
    if (!sent) {
        log_win_error("WinHttpSendRequest", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        log_win_error("WinHttpReceiveResponse", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    HANDLE hFile = CreateFileA(destinationPath,
                               GENERIC_WRITE,
                               0,
                               NULL,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        log_win_error("CreateFileA", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }

    BYTE buffer[8192];
    DWORD bytesRead = 0;
    BOOL success = TRUE;

    do {
        if (!WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead)) {
            log_win_error("WinHttpReadData", GetLastError());
            success = FALSE;
            break;
        }

        if (bytesRead == 0) {
            break;
        }

        DWORD written = 0;
        if (!WriteFile(hFile, buffer, bytesRead, &written, NULL) || written != bytesRead) {
            log_win_error("WriteFile", GetLastError());
            success = FALSE;
            break;
        }
    } while (bytesRead > 0);

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!success) {
        DeleteFileA(destinationPath);
    }

    return success;
}

static BOOL BuildTempPath(const char *fileName, char *output, size_t size)
{
    if (!fileName || !output || size == 0) {
        return FALSE;
    }

    DWORD len = GetTempPathA((DWORD)size, output);
    if (len == 0 || len >= size) {
        return FALSE;
    }

    size_t remaining = size - len;
    int written = snprintf(output + len, remaining, "%s", fileName);
    if (written < 0 || (size_t)written >= remaining) {
        output[0] = '\0';
        return FALSE;
    }
    return TRUE;
}

static BOOL WriteUpdateScript(const char *scriptPath, const char *tempExePath, const char *targetExePath)
{
    FILE *file = fopen(scriptPath, "w");
    if (!file) {
        log_error("Updater: Unable to create update script.");
        return FALSE;
    }

    fprintf(file, "@echo off\n");
    fprintf(file, "setlocal\n");
    fprintf(file, "timeout /t 2 /nobreak >nul\n");
    fprintf(file, "copy /y \"%s\" \"%s\" >nul\n", tempExePath, targetExePath);
    fprintf(file, "if errorlevel 1 goto end\n");
    fprintf(file, "start \"\" \"%s\"\n", targetExePath);
    fprintf(file, "del /f /q \"%s\"\n", tempExePath);
    fprintf(file, ":end\n");
    fprintf(file, "del /f /q \"%%~f0\"\n");

    fclose(file);
    return TRUE;
}

static BOOL LaunchScript(const char *scriptPath)
{
    char systemDir[MAX_PATH] = {0};
    UINT len = GetSystemDirectoryA(systemDir, ARRAYSIZE(systemDir));
    if (len == 0 || len >= ARRAYSIZE(systemDir)) {
        return FALSE;
    }

    char cmdPath[MAX_PATH] = {0};
    snprintf(cmdPath, sizeof(cmdPath), "%s\\cmd.exe", systemDir);

    char commandLine[MAX_PATH * 2] = {0};
    snprintf(commandLine, sizeof(commandLine), "/c \"%s\"", scriptPath);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    BOOL created = CreateProcessA(cmdPath,
                                  commandLine,
                                  NULL,
                                  NULL,
                                  FALSE,
                                  CREATE_NO_WINDOW,
                                  NULL,
                                  NULL,
                                  &si,
                                  &pi);
    if (!created) {
        log_win_error("CreateProcessA", GetLastError());
        return FALSE;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}
