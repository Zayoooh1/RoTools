#include "account_webview.h"

#include "account_manager.h"
#include "account_storage.h"
#include "gui.h"
#include "log.h"

#include <windows.h>

#include <WebView2.h>
#include <winhttp.h>
#include <wrl.h>
#include <ShlObj.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

typedef HRESULT(WINAPI *PFN_CreateCoreWebView2EnvironmentWithOptions)(
    LPCWSTR, LPCWSTR, ICoreWebView2EnvironmentOptions *, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

using Microsoft::WRL::ComPtr;

static const wchar_t ACCOUNT_WEBVIEW_WINDOW_CLASS[] = L"MULTIROBLOX_WEBVIEW_CLASS";
static const wchar_t ACCOUNT_WEBVIEW_TITLE[] = L"Login to your Roblox Account";
static const int ACCOUNT_COOKIE_TIMER_ID = 1;
static const int ACCOUNT_WEBVIEW_DEFAULT_WIDTH = 640;
static const int ACCOUNT_WEBVIEW_DEFAULT_HEIGHT = 520;
static const wchar_t WEBVIEW_USERDATA_SUBDIR[] = L"\\RoTools\\WebView2";
static const wchar_t WEBVIEW_RUNTIME_SUBDIR[] = L"WebView2Runtime";

static HMODULE s_webviewLoader = nullptr;
static PFN_CreateCoreWebView2EnvironmentWithOptions s_createEnvironment = nullptr;
static ComPtr<ICoreWebView2Environment> s_environment;
static BOOL s_comInitialized = FALSE;

struct AccountWebViewContext {
    HWND hwnd;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    BOOL cookieCaptured;
    BOOL accountAdded;
    BOOL done;
    char roblosecurity[AM_ACCOUNT_COOKIE_LEN];
    char username[AM_ACCOUNT_FIELD_LEN];
};

inline BOOL StringHasValue(const char *value)
{
    return value && value[0] != '\0';
}

static BOOL WideToUtf8(PWSTR wide, char *output, size_t outputSize)
{
    if (!wide || !output || outputSize == 0) {
        return FALSE;
    }

    int required = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 0 || (size_t)required > outputSize) {
        return FALSE;
    }

    if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, output, (int)outputSize, nullptr, nullptr)) {
        return FALSE;
    }

    return TRUE;
}

static void AttemptAccountCreation(AccountWebViewContext *context);
static void ClearBrowserCookies(ICoreWebView2 *webview);
static BOOL GetModuleDirectory(std::wstring &pathOut)
{
    wchar_t modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return FALSE;
    }

    wchar_t *lastSlash = wcsrchr(modulePath, L'\\');
    if (!lastSlash) {
        return FALSE;
    }
    *(lastSlash + 1) = L'\0';
    pathOut.assign(modulePath);
    return TRUE;
}

static BOOL GetWebViewRuntimeFolder(std::wstring &pathOut)
{
    std::wstring baseDir;
    if (!GetModuleDirectory(baseDir)) {
        log_error("Failed to get module directory for WebView2 runtime.");
        return FALSE;
    }

    // Try multiple possible locations for WebView2Runtime
    std::wstring runtimeLocations[] = {
        baseDir + WEBVIEW_RUNTIME_SUBDIR + L"\\",  // Production: same folder as exe
        baseDir + L"..\\..\\external\\WebView2Runtime\\",  // Dev: from build/Release to external
        baseDir + L"..\\external\\WebView2Runtime\\",  // Alternative dev path
    };

    for (const auto& runtimeDir : runtimeLocations) {
        DWORD attr = GetFileAttributesW(runtimeDir.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;  // Try next location
        }

        std::wstring runtimeExe = runtimeDir + L"msedgewebview2.exe";
        if (GetFileAttributesW(runtimeExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // Found valid runtime folder
            pathOut = runtimeDir;
            return TRUE;
        }
    }

    // If we get here, none of the locations worked
    log_error("WebView2 runtime folder is missing. Reinstall the application.");
    return FALSE;
}
static BOOL IsDevEnvironment()
{
    wchar_t modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return FALSE;
    }

    // Convert to lowercase for case-insensitive comparison
    for (DWORD i = 0; i < len; ++i) {
        modulePath[i] = towlower(modulePath[i]);
    }

    // Check if path contains "build" or other dev indicators
    return (wcsstr(modulePath, L"\\build\\") != NULL || 
            wcsstr(modulePath, L"\\debug\\") != NULL ||
            wcsstr(modulePath, L"\\release\\") != NULL);
}

static BOOL GetWebViewUserDataFolder(std::wstring &pathOut)
{
    PWSTR localAppData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL, &localAppData);
    if (FAILED(hr) || !localAppData) {
        log_win_error("SHGetKnownFolderPath(LocalAppData)", hr);
        return FALSE;
    }

    std::wstring path(localAppData);
    CoTaskMemFree(localAppData);
    
    // Use different folder for dev environment to avoid mixing dev and production data
    path += IsDevEnvironment() ? L"\\RoTools_Dev\\WebView2" : WEBVIEW_USERDATA_SUBDIR;

    if (SHCreateDirectoryExW(NULL, path.c_str(), NULL) != ERROR_SUCCESS) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            log_win_error("SHCreateDirectoryExW(WebView2 user data)", err);
            return FALSE;
        }
    }

    pathOut = path;
    return TRUE;
}

class CookieListHandler : public ICoreWebView2GetCookiesCompletedHandler {
public:
    CookieListHandler(AccountWebViewContext *context)
        : m_ref(1), m_context(context)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject) {
            return E_POINTER;
        }

        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ICoreWebView2GetCookiesCompletedHandler)) {
            *ppvObject = static_cast<ICoreWebView2GetCookiesCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP Invoke(HRESULT result, ICoreWebView2CookieList *cookieList) override
    {
        if (SUCCEEDED(result) && cookieList && m_context && !m_context->cookieCaptured) {
            UINT32 count = 0;
            cookieList->get_Count(&count);
            for (UINT32 i = 0; i < count && !m_context->cookieCaptured; ++i) {
                ComPtr<ICoreWebView2Cookie> cookie;
                cookieList->GetValueAtIndex(i, &cookie);
                if (!cookie) {
                    continue;
                }

                PWSTR name = nullptr;
                cookie->get_Name(&name);
                if (!name) {
                    continue;
                }

                if (wcscmp(name, L".ROBLOSECURITY") == 0) {
                    PWSTR value = nullptr;
                    cookie->get_Value(&value);
                    if (value) {
                        if (WideToUtf8(value, m_context->roblosecurity, sizeof(m_context->roblosecurity))) {
                            m_context->cookieCaptured = TRUE;
                            KillTimer(m_context->hwnd, ACCOUNT_COOKIE_TIMER_ID);
                            AttemptAccountCreation(m_context);
                        }
                        CoTaskMemFree(value);
                    }
                }

                CoTaskMemFree(name);
            }
        }

        return result;
    }

private:
    ULONG m_ref;
    AccountWebViewContext *m_context;
};

static BOOL EnsureComInitialized(void)
{
    if (s_comInitialized) {
        return TRUE;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        log_win_error("CoInitializeEx", hr);
        return FALSE;
    }

    s_comInitialized = SUCCEEDED(hr);
    return TRUE;
}

static BOOL LoadWebView2Loader(void)
{
    if (s_webviewLoader && s_createEnvironment) {
        return TRUE;
    }

    s_webviewLoader = GetModuleHandleW(L"WebView2Loader.dll");
    if (!s_webviewLoader) {
        s_webviewLoader = LoadLibraryW(L"WebView2Loader.dll");
    }

    if (!s_webviewLoader) {
        log_error("Unable to locate WebView2Loader.dll; install WebView2 runtime.");
        return FALSE;
    }

    s_createEnvironment = reinterpret_cast<PFN_CreateCoreWebView2EnvironmentWithOptions>(
        GetProcAddress(s_webviewLoader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!s_createEnvironment) {
        log_error("WebView2 loader is missing the CreateCoreWebView2EnvironmentWithOptions export.");
        return FALSE;
    }

    return TRUE;
}

class EnvironmentCreatedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    EnvironmentCreatedHandler(HANDLE completionEvent)
        : m_ref(1), m_completionEvent(completionEvent)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject) {
            return E_POINTER;
        }

        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
            *ppvObject = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP Invoke(HRESULT result, ICoreWebView2Environment *environment) override
    {
        if (SUCCEEDED(result) && environment) {
            s_environment = environment;
        }

        if (m_completionEvent) {
            SetEvent(m_completionEvent);
        }

        return result;
    }

private:
    ULONG m_ref;
    HANDLE m_completionEvent;
};

class ControllerCreatedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    ControllerCreatedHandler(AccountWebViewContext *context)
        : m_ref(1), m_context(context)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppvObject) override
    {
        if (!ppvObject) {
            return E_POINTER;
        }

        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
            *ppvObject = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    IFACEMETHODIMP_(ULONG) Release() override
    {
        ULONG count = InterlockedDecrement(&m_ref);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP Invoke(HRESULT result, ICoreWebView2Controller *controller) override
    {
        if (SUCCEEDED(result) && controller && m_context) {
            m_context->controller = controller;
            controller->AddRef();
            controller->get_CoreWebView2(&m_context->webview);
            RECT bounds;
            GetClientRect(m_context->hwnd, &bounds);
            controller->put_Bounds(bounds);
            controller->put_IsVisible(TRUE);

    ComPtr<ICoreWebView2Settings> settings;
            if (SUCCEEDED(m_context->webview.As(&settings))) {
                settings->put_IsScriptEnabled(TRUE);
                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                settings->put_IsStatusBarEnabled(FALSE);
            }

            ClearBrowserCookies(m_context->webview.Get());
            m_context->webview->Navigate(L"https://www.roblox.com/login");
            SetTimer(m_context->hwnd, ACCOUNT_COOKIE_TIMER_ID, 1000, nullptr);
        }

        return result;
    }

private:
    ULONG m_ref;
    AccountWebViewContext *m_context;
};

static bool AddOrUpdateAccountFromBrowser(const char *username, const char *cookie)
{
    if (!username || !cookie) {
        return false;
    }

    RbxAccount *existing = AM_FindAccountByUsername(username);
    if (existing) {
        char prompt[512];
        snprintf(prompt, sizeof(prompt),
                 "An account with the username \"%s\" already exists.\nUpdate its cookie and timestamp?",
                 username);
        if (MessageBoxA(g_hwndMain, prompt, "MultiRoblox", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            AM_UpdateAccountFields(existing, username, cookie, existing->alias, existing->group, existing->description);
            AM_MarkAccountUsed(existing);
            if (!AS_SaveAccounts()) {
                MessageBoxA(g_hwndMain, "Failed to save the updated account.", "MultiRoblox", MB_ICONERROR);
            }
            PostMessage(g_hwndMain, WM_APP_ACCOUNT_REFRESH, 0, 0);
            SendMessage(g_hwndMain, WM_APP_ACCOUNT_REFRESH, 0, 0);
            return true;
        }

        char aliasWithSuffix[AM_ACCOUNT_FIELD_LEN] = {0};
        snprintf(aliasWithSuffix, sizeof(aliasWithSuffix), "%s (2)", username);
        char safeAlias[AM_ACCOUNT_FIELD_LEN];
        strncpy(safeAlias, aliasWithSuffix, sizeof(safeAlias) - 1);
        strncpy(safeAlias, aliasWithSuffix, sizeof(safeAlias) - 1);
        safeAlias[sizeof(safeAlias) - 1] = '\0';

        RbxAccount *account = AM_CreateAccount(username, cookie, safeAlias, "Default", "");
        if (!account) {
            MessageBoxA(g_hwndMain, "Unable to create a new account entry.", "MultiRoblox", MB_ICONERROR);
            return false;
        }

        AM_MarkAccountUsed(account);
        AS_SaveAccounts();
        PostMessage(g_hwndMain, WM_APP_ACCOUNT_REFRESH, 0, 0);
        SendMessage(g_hwndMain, WM_APP_ACCOUNT_REFRESH, 0, 0);
        return true;
    }

    RbxAccount *account = AM_CreateAccount(username, cookie, username, "Default", "");
    if (!account) {
        MessageBoxA(g_hwndMain, "Unable to create a new account entry.", "MultiRoblox", MB_ICONERROR);
        return false;
    }

        AM_MarkAccountUsed(account);
        if (!AS_SaveAccounts()) {
            log_error("Failed to persist the new account (AS_SaveAccounts).");
        }

        PostMessage(g_hwndMain, WM_APP_ACCOUNT_REFRESH, 0, 0);
        return true;
}

static BOOL ParseUsernameFromResponse(const std::string &response, char *username, size_t bufferSize)
{
    // JSON response from users.roblox.com/v1/users/authenticated:
    // {"id":12345,"name":"TargetUser","displayName":"Target Display Name"}
    const char key[] = "\"name\":\"";
    const char *start = strstr(response.c_str(), key);
    if (!start) {
        return FALSE;
    }

    start += strlen(key);
    const char *end = strchr(start, '"');
    if (!end) {
        return FALSE;
    }

    size_t length = (size_t)(end - start);
    if (length >= bufferSize) {
        length = bufferSize - 1;
    }

    strncpy(username, start, length);
    username[length] = '\0';
    return TRUE;
}

static std::wstring StringToWide(const char *src)
{
    if (!src) {
        return L"";
    }

    int required = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
    if (required == 0) {
        return L"";
    }

    std::wstring result(required, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, 0, src, -1, &result[0], required)) {
        return L"";
    }

    result.resize(required - 1);
    return result;
}

static BOOL FetchRobloxUsername(const char *cookie, char *username, size_t usernameSize)
{
    if (!cookie || !username) {
        return FALSE;
    }

    BOOL result = FALSE;
    HINTERNET session = WinHttpOpen(L"MultiRoblox/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        log_win_error("WinHttpOpen", GetLastError());
        return FALSE;
    }

    // Use users.roblox.com for modern API
    HINTERNET connect = WinHttpConnect(session, L"users.roblox.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        log_win_error("WinHttpConnect", GetLastError());
        WinHttpCloseHandle(session);
        return FALSE;
    }

    // Endpoint: /v1/users/authenticated
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/v1/users/authenticated", NULL,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        log_win_error("WinHttpOpenRequest", GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return FALSE;
    }

    char cookieHeader[4096];
    snprintf(cookieHeader, sizeof(cookieHeader), ".ROBLOSECURITY=%s", cookie);
    std::wstring header = L"Cookie: " + StringToWide(cookieHeader);
    WinHttpAddRequestHeaders(request, header.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD);

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        log_win_error("WinHttpSendRequest/ReceiveResponse", GetLastError());
        goto cleanup;
    }

    while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    if (response.empty()) {
        log_error("Empty response from Roblox API.");
        goto cleanup;
    }

    if (ParseUsernameFromResponse(response, username, usernameSize)) {
        result = TRUE;
    } else {
        log_error("Failed to parse username from Roblox API response.");
        if (response.length() > 200) response.resize(200);
        log_error(response.c_str());
    }

cleanup:
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

static void AttemptAccountCreation(AccountWebViewContext *context)
{
    if (!context) {
        return;
    }

    if (!context->roblosecurity[0]) {
        return;
    }

    char username[AM_ACCOUNT_FIELD_LEN] = {0};
    if (!FetchRobloxUsername(context->roblosecurity, username, sizeof(username))) {
        MessageBoxA(context->hwnd, "Unable to fetch Roblox profile information.", "MultiRoblox", MB_ICONERROR);
        // Do not set done=TRUE here; let WM_CLOSE handler do it to ensure proper cleanup sequence
        PostMessage(context->hwnd, WM_CLOSE, 0, 0);
        return;
    }

    context->accountAdded = AddOrUpdateAccountFromBrowser(username, context->roblosecurity);
    // Do not set done=TRUE here; let WM_CLOSE handler do it to ensure proper cleanup sequence
    PostMessage(context->hwnd, WM_CLOSE, 0, 0);
}

static void CheckForRoblosecurity(AccountWebViewContext *context)
{
    if (!context || !context->webview || context->cookieCaptured) {
        return;
    }

    ComPtr<ICoreWebView2_2> webview2;
    if (FAILED(context->webview.As(&webview2)) || !webview2) {
        return;
    }

    ComPtr<ICoreWebView2CookieManager> manager;
    if (FAILED(webview2->get_CookieManager(&manager)) || !manager) {
        return;
    }

    CookieListHandler *handler = new CookieListHandler(context);
    handler->AddRef();
    manager->GetCookies(nullptr, handler);
    handler->Release();
}

static void ClearBrowserCookies(ICoreWebView2 *webview)
{
    if (!webview) {
        return;
    }

    ComPtr<ICoreWebView2_2> webview2;
    if (FAILED(webview->QueryInterface(IID_PPV_ARGS(&webview2))) || !webview2) {
        return;
    }

    ComPtr<ICoreWebView2CookieManager> manager;
    if (FAILED(webview2->get_CookieManager(&manager)) || !manager) {
        return;
    }

    manager->DeleteAllCookies();
    manager->DeleteCookies(nullptr, L"https://www.roblox.com");
}

static LRESULT CALLBACK AccountWebViewProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    AccountWebViewContext *context = reinterpret_cast<AccountWebViewContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE:
        {
            context = reinterpret_cast<AccountWebViewContext *>(((LPCREATESTRUCT)lParam)->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)context);
            if (!context) {
                return -1;
            }

            context->hwnd = hwnd;
            context->done = FALSE;
            context->cookieCaptured = FALSE;
            context->accountAdded = FALSE;
            context->roblosecurity[0] = '\0';
            context->username[0] = '\0';

            if (s_environment) {
            ControllerCreatedHandler *handler = new ControllerCreatedHandler(context);
            handler->AddRef();
            s_environment->CreateCoreWebView2Controller(hwnd, handler);
            handler->Release();
            }

            SetWindowTextW(hwnd, ACCOUNT_WEBVIEW_TITLE);
            return 0;
        }
        case WM_SETTEXT:
        {
            return DefWindowProcW(hwnd, WM_SETTEXT, wParam, (LPARAM)ACCOUNT_WEBVIEW_TITLE);
        }
        case WM_SIZE:
        {
            if (context && context->controller) {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                context->controller->put_Bounds(bounds);
            }
            return 0;
        }
        case WM_TIMER:
        {
            if (wParam == ACCOUNT_COOKIE_TIMER_ID && context) {
                CheckForRoblosecurity(context);
            }
            return 0;
        }
        case WM_CLOSE:
        {
            if (context) {
                context->done = TRUE;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
        {
            if (context) {
                KillTimer(hwnd, ACCOUNT_COOKIE_TIMER_ID);
                context->controller.Reset();
                context->webview.Reset();
            }
            return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

#include "resource.h"

static BOOL RegisterWebViewWindowClass(void)
{
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = AccountWebViewProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = ACCOUNT_WEBVIEW_WINDOW_CLASS;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    
    HICON hIcon = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;

    ATOM atom = RegisterClassExW(&wc);
    if (atom == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_CLASS_ALREADY_EXISTS) {
            return TRUE;
        }
        return FALSE;
    }
    return TRUE;
}

static BOOL EnsureEnvironmentReady(void)
{
    if (s_environment) {
        return TRUE;
    }

    if (!LoadWebView2Loader()) {
        return FALSE;
    }

    HANDLE event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event) {
        log_win_error("CreateEvent", GetLastError());
        return FALSE;
    }

    std::wstring userDataFolder;
    if (!GetWebViewUserDataFolder(userDataFolder)) {
        CloseHandle(event);
        return FALSE;
    }

    std::wstring runtimeFolder;
    if (!GetWebViewRuntimeFolder(runtimeFolder)) {
        CloseHandle(event);
        return FALSE;
    }

    EnvironmentCreatedHandler *handler = new EnvironmentCreatedHandler(event);
    HRESULT hr = s_createEnvironment(runtimeFolder.c_str(), userDataFolder.c_str(), nullptr, handler);
    if (FAILED(hr)) {
        log_win_error("CreateCoreWebView2EnvironmentWithOptions", hr);
        handler->Release();
        CloseHandle(event);
        return FALSE;
    }

    WaitForSingleObject(event, 30000);
    handler->Release();
    CloseHandle(event);

    if (!s_environment) {
        log_error("WebView2 environment creation timed out or failed.");
        return FALSE;
    }

    return TRUE;
}

extern "C" BOOL AM_OpenBrowserLoginAndAddAccount(HWND parent)
{
    if (!EnsureComInitialized() || !EnsureEnvironmentReady()) {
        return FALSE;
    }

    if (!RegisterWebViewWindowClass()) {
        log_error("Failed to register WebView window class.");
        return FALSE;
    }

    AccountWebViewContext *context = new AccountWebViewContext();

    int windowWidth = ACCOUNT_WEBVIEW_DEFAULT_WIDTH;
    int windowHeight = ACCOUNT_WEBVIEW_DEFAULT_HEIGHT;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;

    if (parent) {
        RECT parentRect;
        if (GetWindowRect(parent, &parentRect)) {
            int parentWidth = parentRect.right - parentRect.left;
            int parentHeight = parentRect.bottom - parentRect.top;
            windowX = parentRect.left + (parentWidth - windowWidth) / 2;
            windowY = parentRect.top + (parentHeight - windowHeight) / 2;
        }
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
        ACCOUNT_WEBVIEW_WINDOW_CLASS,
        ACCOUNT_WEBVIEW_TITLE,
        WS_OVERLAPPEDWINDOW,
        windowX,
        windowY,
        windowWidth,
        windowHeight,
        parent,
        NULL,
        GetModuleHandle(NULL),
        context);

    if (!hwnd) {
        delete context;
        log_win_error("CreateWindowExW", GetLastError());
        return FALSE;
    }

    if (!SetWindowTextW(hwnd, ACCOUNT_WEBVIEW_TITLE)) {
        log_win_error("SetWindowTextW (webview)", GetLastError());
    } else {
        SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)ACCOUNT_WEBVIEW_TITLE);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // We are not disabling the parent window because it causes issues with restoring focus/input state
    // after the WebView window closes. Since we have a modal message loop here, the user cannot
    // interact with the main window anyway until this loop finishes (mostly).
    
    MSG msg;
    while (!context->done && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Ensure main window is active and ready for input
    if (parent) {
        if (IsIconic(parent)) {
            ShowWindow(parent, SW_RESTORE);
        }
        SetForegroundWindow(parent);
        SetFocus(parent);
    }

    BOOL added = context->accountAdded;
    delete context;
    return added;
}
