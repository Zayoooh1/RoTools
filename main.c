#include <windows.h>

#include "account_manager.h"
#include "account_storage.h"
#include "gui.h"
#include "log.h"
#include "version.h"

static const char MULTIROBLOX_MUTEX_NAME[] = "MultiRoblox_singletonMutex";
static const char ROBLOX_MUTEX_NAME[] = "ROBLOX_singletonMutex";

static HANDLE g_hMultiMutex = NULL;
static HANDLE g_hRobloxMutex = NULL;
static BOOL g_multiMutexOwned = FALSE;
static BOOL g_robloxMutexOwned = FALSE;

static void CloseMutexHandle(HANDLE *handle, BOOL *owned)
{
    if (*handle) {
        if (*owned) {
            ReleaseMutex(*handle);
            *owned = FALSE;
        }
        CloseHandle(*handle);
        *handle = NULL;
    }
}

static void CleanupMutexes(void)
{
    CloseMutexHandle(&g_hRobloxMutex, &g_robloxMutexOwned);
    CloseMutexHandle(&g_hMultiMutex, &g_multiMutexOwned);
}

static void ShowMutexMessage(const char *title, const char *message, UINT icon)
{
    MessageBoxA(NULL, message, title, MB_OK | icon);
}

static BOOL s_accountSystemsReady = FALSE;

static BOOL InitializeAccountSystems(void);
static void CleanupAccountSystems(void);

static BOOL CreateSingleInstanceMutex(void)
{
    g_hMultiMutex = CreateMutexA(NULL, TRUE, MULTIROBLOX_MUTEX_NAME);
    if (!g_hMultiMutex) {
        DWORD err = GetLastError();
        log_win_error("CreateMutexA MultiRoblox", err);
        ShowMutexMessage(APP_NAME, "Unable to create internal mutex. Please retry or restart Windows.", MB_ICONERROR);
        return FALSE;
    }

    DWORD lastError = GetLastError();
    if (lastError == ERROR_ALREADY_EXISTS) {
        log_error("Another MultiRoblox instance is already running.");
        ShowMutexMessage(APP_NAME, APP_NAME " is already running on this system. Close the other instance before starting a new one.", MB_ICONINFORMATION);
        CloseMutexHandle(&g_hMultiMutex, &g_multiMutexOwned);
        return FALSE;
    }

    g_multiMutexOwned = TRUE;
    return TRUE;
}

static BOOL CreateRobloxMutex(void)
{
    g_hRobloxMutex = CreateMutexA(NULL, TRUE, ROBLOX_MUTEX_NAME);
    if (!g_hRobloxMutex) {
        DWORD err = GetLastError();
        log_win_error("CreateMutexA Roblox", err);
        ShowMutexMessage(APP_NAME, "Unable to lock the Roblox mutex. MultiRoblox will exit to avoid conflicting with an existing Roblox process.", MB_ICONERROR);
        return FALSE;
    }

    DWORD lastError = GetLastError();
    if (lastError == ERROR_ALREADY_EXISTS) {
        log_error("Roblox mutex already exists; Roblox is running.");
        ShowMutexMessage(APP_NAME, "Roblox is already running. Start MultiRoblox before launching Roblox to enable multiple clients.", MB_ICONWARNING);
        CloseMutexHandle(&g_hRobloxMutex, &g_robloxMutexOwned);
        return FALSE;
    }

    g_robloxMutexOwned = TRUE;
    return TRUE;
}

static BOOL InitializeAccountSystems(void)
{
    if (!AM_Init()) {
        log_error("Failed to initialize account manager.");
        return FALSE;
    }

    if (!AS_Initialize()) {
        log_error("Failed to initialize account storage.");
        return FALSE;
    }

    if (!AS_LoadAccounts()) {
        log_error("Failed to load account data. Starting with empty database.");
        // Continue execution even if loading fails (e.g. version mismatch or corruption)
        return TRUE;
    }

    s_accountSystemsReady = TRUE;
    return TRUE;
}

static void CleanupAccountSystems(void)
{
    if (s_accountSystemsReady && !AS_SaveAccounts()) {
        log_error("Failed to save account data before exit.");
    }
    AS_Shutdown();
    AM_Shutdown();
    s_accountSystemsReady = FALSE;
}

int RunApplication(HINSTANCE hInstance, int nCmdShow)
{
    if (!CreateSingleInstanceMutex()) {
        return -1;
    }

    if (!CreateRobloxMutex()) {
        CleanupMutexes();
        return -1;
    }

    if (!InitializeAccountSystems()) {
        MessageBoxA(NULL, "Failed to initialize account systems. Check logs.", APP_NAME " Error", MB_ICONERROR);
        CleanupAccountSystems();
        CleanupMutexes();
        return -1;
    }

    int result = RunGui(hInstance, nCmdShow);
    CleanupAccountSystems();
    CleanupMutexes();
    return result;
}
