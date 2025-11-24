#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>
#include <wincodec.h>
#include <shlwapi.h>
#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#include "account_dialogs.h"
#include "account_manager.h"
#include "account_storage.h"
#include "account_webview.h"
#include "roblox_launch.h"
#include "gui.h"
#include "log.h"
#include "resource.h"
#include "updater.h"
#include "version.h"

#ifndef MULTIROBLOX_USE_CUSTOM_MANIFEST
#pragma comment(linker, \
    "/manifestdependency:\"type='win32' " \
    "name='Microsoft.Windows.Common-Controls' version='6.0.0.0' " \
    "processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define IDT_AUTO_REJOIN_TIMER 9001
#define AUTO_REJOIN_INTERVAL_MS 900000 // 15 minutes
#define UPDATE_PROMPT_INSTALL_ID 9101
#define UPDATE_PROMPT_SNOOZE_ID 9102

HWND g_hwndMain = NULL;
static HWND g_hwndTitleLabel = NULL;
static HWND g_hwndInfoEdit = NULL;

static const char g_windowTitle[] = APP_NAME " " APP_VERSION_DISPLAY;
static const char g_titleText[] = APP_NAME " - multiple Roblox instances";
static const char g_instructionText[] =
"Purpose:\r\n"
"MultiRoblox allows you to run multiple Roblox instances simultaneously on one computer.\r\n\r\n"
"Usage:\r\n"
"1) Add your Roblox accounts to the list below.\r\n"
"2) Select the accounts you want to use.\r\n"
"3) Select the game you want to play.\r\n"
"4) Click 'Join' to launch all selected accounts at once.";

#define IDC_ACCOUNTS_LIST 3001
#define ID_BTN_ADD_ACCOUNT 3002
#define ID_BTN_DELETE_ACCOUNT 3004
#define ID_BTN_REFRESH_LIST 3008
#define ID_EDIT_JOIN_PLACE 3009
#define ID_EDIT_JOIN_LINK 3010
#define ID_BTN_JOIN_GAMES 3011
#define ID_CHECK_AUTO_REJOIN 3012
#define ID_EDIT_REJOIN_TIME 3013
#define ID_CHECK_UNIT_S 3014
#define ID_CHECK_UNIT_M 3015
#define ID_CHECK_UNIT_H 3016
#define ID_BTN_UPDATE_SETTINGS 3017

#define PLACE_ID_ICON_DEFAULT 24
#define PLACE_ID_ICON_MARGIN 4

#define IDM_CONTEXT_FAVORITE 4001
#define IDM_CONTEXT_MARK_USED 4002
#define IDM_CONTEXT_COPY_COOKIE 4003

enum AccountColumns {
    COL_USERNAME = 0,
    COL_LAST_USED,
    COL_COUNT
};

static HWND g_hwndAccountsLabel = NULL;
static HWND g_hwndListView = NULL;
static HWND g_hwndButtonAdd = NULL;
static HWND g_hwndButtonDelete = NULL;
static HWND g_hwndButtonRefresh = NULL;
static HWND g_hwndUpdateSettingsButton = NULL;
static HWND g_hwndJoinLinkLabel = NULL;
static HWND g_hwndJoinPlaceLabel = NULL;
static HWND g_hwndJoinLinkEdit = NULL;
static HWND g_hwndJoinPlaceEdit = NULL;
static HWND g_hwndJoinButton = NULL;
static HWND g_hwndAutoRejoinCheck = NULL;
static HWND g_hwndRejoinTimeEdit = NULL;
static HWND g_hwndCheckS = NULL;
static HWND g_hwndCheckM = NULL;
static HWND g_hwndCheckH = NULL;
static HMENU g_hAccountMenu = NULL;
static char g_joinLinkSaved[1024] = {0};
static char g_joinPlaceSaved[128] = {0};
static char g_rejoinTimeSaved[32] = "15";
static int g_rejoinUnitSaved = 1; // 0=s, 1=m, 2=h
static HWND g_hwndPlaceIdInfoIcon = NULL;
static HWND g_hwndPlaceIdInfoTooltip = NULL;
static HBITMAP g_hbmPlaceIdIcon = NULL;
static HBITMAP g_hbmPlaceIdTooltip = NULL;
static BOOL g_placeTooltipVisible = FALSE;
static BOOL g_placeIconTracking = FALSE;
static int g_placeTooltipWidth = 0;
static int g_placeTooltipHeight = 0;
static BOOL g_placeImagesLoaded = FALSE;
static int g_placeIconWidth = PLACE_ID_ICON_DEFAULT;
static int g_placeIconHeight = PLACE_ID_ICON_DEFAULT;

static const char g_accountsLabelText[] = "Account Manager";

typedef struct {
    const char* name;
    const char* placeId;
} GameInfo;

static const GameInfo g_games[] = {
    {"RCU", "74260430392611"},
    {"PS99", "8737899170"},
    {"SAB", "109983668079237"},
    {"GAG", "126884695634066"},
    {"PvB", "127742093697776"}
};

static int GetSelectedAccountIndex(void);
static const RbxAccount *GetSelectedAccount(void);
static void PopulateAccountList(void);
static void UpdateActionButtons(void);
static void TrimInPlace(char *str);
static void FormatLastUsed(char *buffer, size_t bufferSize, time_t timestamp);
static BOOL CopyCookieToClipboard(HWND hwnd, const RbxAccount *account);
static BOOL InitializeAccountControls(HWND hwnd, HFONT hFont);
static void CreateAccountContextMenu(void);
static void HandleAccountCommand(HWND hwnd, UINT command);
static void OnAddAccount(HWND hwnd);
static void OnEditAccount(HWND hwnd);
static void OnDeleteAccount(HWND hwnd);

static int RegisterMainWindow(HINSTANCE hInstance);
static BOOL CreateGuiControls(HWND hwnd);
static void ResizeGuiControls(HWND hwnd);
static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static void LoadJoinSettings(void);
static void SaveJoinSettings(HWND hwnd);
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static HBITMAP LoadBitmapFromFile(const char *filename);
static BOOL ResolveAssetPath(const char *relativePath, char *buffer, size_t bufferSize);
static HBITMAP ScaleBitmapToFit(HBITMAP source, int maxWidth, int maxHeight, int *outWidth, int *outHeight);
static HBITMAP LoadPngIcon(const char *relativePath, int targetSize);
static void LoadPlaceIdImages(void);
static BOOL CreatePlaceIdIconControl(HWND hwnd);
static BOOL CreatePlaceIdTooltipWindow(void);
static void UpdatePlaceIdTooltipPosition(void);
static void ShowPlaceIdTooltip(void);
static void HidePlaceIdTooltip(void);
static void CleanupPlaceIdResources(void);
static LRESULT CALLBACK PlaceIdIconSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static void HandleUpdateAvailable(HWND hwnd, UpdateAvailableInfo *info);
static int ShowUpdatePrompt(HWND hwnd, const UpdateAvailableInfo *info);
static void ShowUpdateSettingsDialog(HWND hwnd);
static INT_PTR CALLBACK UpdateSettingsDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static void SetUpdateSettingsDialogFrequency(HWND hwndDlg, UpdateCheckFrequency frequency);
static UpdateCheckFrequency GetDialogUpdateFrequency(HWND hwndDlg);

int RunApplication(HINSTANCE hInstance, int nCmdShow);

int RunGui(HINSTANCE hInstance, int nCmdShow)
{
    if (RegisterMainWindow(hInstance) == 0) {
        return -1;
    }

    DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

    RECT initialRect = {0, 0, 600, 460};
    AdjustWindowRect(&initialRect, windowStyle, FALSE);

    HWND hwnd = CreateWindowExA(
        0,
        MULTIROBLOX_WINDOW_CLASS,
        g_windowTitle,
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        initialRect.right - initialRect.left,
        initialRect.bottom - initialRect.top,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd) {
        DWORD err = GetLastError();
        log_win_error("CreateWindowExA", err);
        MessageBoxA(NULL, "Failed to create the main window.", g_windowTitle, MB_ICONERROR);
        return -1;
    }
    
    // Set Icon manually for the window instance as well
    HICON hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (hIcon) {
        SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    g_hwndMain = hwnd;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    Updater_BeginStartupCheck(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (!g_hwndMain || !IsDialogMessageA(g_hwndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return (int)msg.wParam;
}

static BOOL RegisterCommonControls(void)
{
    INITCOMMONCONTROLSEX icc = {0};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    BOOL ok = InitCommonControlsEx(&icc);
    if (!ok) {
        InitCommonControls(); // legacy call returns void
        ok = TRUE;
    }
    return ok;
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    switch (msg) {
        case WM_KEYDOWN:
        {
            BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl) {
                switch (wParam) {
                    case 'A':
                        SendMessageA(hwnd, EM_SETSEL, 0, -1);
                        return 0;
                    case 'C':
                        SendMessageA(hwnd, WM_COPY, 0, 0);
                        return 0;
                    case 'V':
                        SendMessageA(hwnd, WM_PASTE, 0, 0);
                        SendMessageA(hwnd, EM_SETSEL, 0, 0);
                        SendMessageA(hwnd, EM_SCROLLCARET, 0, 0);
                        return 0;
                    default:
                        return 0; // block other ctrl+keys
                }
            } else {
                if (wParam == VK_BACK) {
                    return DefSubclassProc(hwnd, msg, wParam, lParam);
                }
                return 0; // block typing other keys
            }
        }
        case WM_CHAR:
        {
            // Allow backspace; block other characters (typing)
            if (wParam == VK_BACK) {
                return DefSubclassProc(hwnd, msg, wParam, lParam);
            }
            return 0;
        }
        case WM_PASTE:
        {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            SendMessageA(hwnd, EM_SETSEL, 0, 0);
            SendMessageA(hwnd, EM_SCROLLCARET, 0, 0);
            return r;
        }
        case WM_SETTEXT:
        {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            SendMessageA(hwnd, EM_SETSEL, 0, 0);
            SendMessageA(hwnd, EM_SCROLLCARET, 0, 0);
            return r;
        }
        case WM_SETFOCUS:
        {
            LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
            SendMessageA(hwnd, EM_SETSEL, 0, 0);
            SendMessageA(hwnd, EM_SCROLLCARET, 0, 0);
            return r;
        }
        default:
            return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

static int RegisterMainWindow(HINSTANCE hInstance)
{
    if (!RegisterCommonControls()) {
        log_error("Failed to initialize common controls.");
        MessageBoxA(NULL, "Common controls failed to initialize.", g_windowTitle, MB_ICONERROR);
        return 0;
    }

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = MainWindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = MULTIROBLOX_WINDOW_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    HICON hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    
    wc.style = CS_HREDRAW | CS_VREDRAW;

    ATOM registered = RegisterClassExA(&wc);
    if (registered == 0) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log_win_error("RegisterClassExA", err);
            MessageBoxA(NULL, "Unable to register the MultiRoblox window class.", g_windowTitle, MB_ICONERROR);
            return 0;
        }
    }

    return 1;
}

static BOOL CreateGuiControls(HWND hwnd)
{
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    g_hwndTitleLabel = CreateWindowExA(
        0,
        "STATIC",
        g_titleText,
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd,
        NULL,
        NULL,
        NULL);

    if (!g_hwndTitleLabel) {
        log_error("Failed to create title label control.");
        return FALSE;
    }

    SendMessageA(g_hwndTitleLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hwndInfoEdit = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        g_instructionText,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0,
        0,
        0,
        0,
        hwnd,
        NULL,
        NULL,
        NULL);

    if (!g_hwndInfoEdit) {
        log_error("Failed to create info edit control.");
        return FALSE;
    }

    SendMessageA(g_hwndInfoEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(g_hwndInfoEdit, EM_SETREADONLY, TRUE, 0);

    g_hwndAccountsLabel = CreateWindowExA(
        0,
        "STATIC",
        g_accountsLabelText,
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0,
        0,
        0,
        0,
        hwnd,
        NULL,
        NULL,
        NULL);

    if (!g_hwndAccountsLabel) {
        log_error("Failed to create account manager label.");
        return FALSE;
    }

    SendMessageA(g_hwndAccountsLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (!InitializeAccountControls(hwnd, hFont)) {
        return FALSE;
    }

    PopulateAccountList();
    return TRUE;
}

static BOOL InitializeAccountControls(HWND hwnd, HFONT hFont)
{
    LoadPlaceIdImages();

    g_hwndListView = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0,
        0,
        0,
        0,
        hwnd,
        (HMENU)(INT_PTR)IDC_ACCOUNTS_LIST,
        NULL,
        NULL);

    if (!g_hwndListView) {
        log_error("Failed to create accounts list.");
        return FALSE;
    }

    HWND hHeader = ListView_GetHeader(g_hwndListView);
    if (hHeader) {
        LONG_PTR style = GetWindowLongPtr(hHeader, GWL_STYLE);
        style &= ~(HDS_BUTTONS | HDS_HOTTRACK);
        style |= HDS_NOSIZING;
        SetWindowLongPtr(hHeader, GWL_STYLE, style);
    }

    ListView_SetExtendedListViewStyle(g_hwndListView,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_CHECKBOXES);

    LVCOLUMN col = {0};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_CENTER;
    col.cx = 200;
    col.pszText = (LPSTR)"Username";
    ListView_InsertColumn(g_hwndListView, COL_USERNAME, &col);
    col.cx = 160;
    col.pszText = (LPSTR)"Last used";
    ListView_InsertColumn(g_hwndListView, COL_LAST_USED, &col);

    // Create ComboBox for Place ID (Game Selection)
    g_hwndJoinLinkEdit = CreateWindowExA(0, "COMBOBOX", "", 
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_EDIT_JOIN_LINK, NULL, NULL);
    
    // Populate ComboBox with games
    for (size_t i = 0; i < sizeof(g_games) / sizeof(g_games[0]); ++i) {
        SendMessageA(g_hwndJoinLinkEdit, CB_ADDSTRING, 0, (LPARAM)g_games[i].name);
    }
    
    // Select the saved game if it matches, otherwise select the first one
    int selectedIndex = 0;
    if (g_joinLinkSaved[0] != '\0') {
        LRESULT idx = SendMessageA(g_hwndJoinLinkEdit, CB_FINDSTRINGEXACT, -1, (LPARAM)g_joinLinkSaved);
        if (idx != CB_ERR) {
            selectedIndex = (int)idx;
        }
    }
    SendMessageA(g_hwndJoinLinkEdit, CB_SETCURSEL, (WPARAM)selectedIndex, 0);

    g_hwndJoinPlaceEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_EDIT_JOIN_PLACE, NULL, NULL);
    SendMessageA(g_hwndJoinPlaceEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"PV Server Link");

    g_hwndJoinLinkLabel = CreateWindowExA(0, "STATIC", "Game",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd, NULL, NULL, NULL);

    CreatePlaceIdIconControl(hwnd);
    CreatePlaceIdTooltipWindow();

    g_hwndJoinPlaceLabel = CreateWindowExA(0, "STATIC", "PV Server Link",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hwnd, NULL, NULL, NULL);

    g_hwndJoinButton = CreateWindowExA(0, "BUTTON", "Join", 
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_JOIN_GAMES, NULL, NULL);

    g_hwndAutoRejoinCheck = CreateWindowExA(0, "BUTTON", "Auto Rejoin",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CHECK_AUTO_REJOIN, NULL, NULL);

    g_hwndRejoinTimeEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "15",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_EDIT_REJOIN_TIME, NULL, NULL);

    g_hwndCheckS = CreateWindowExA(0, "BUTTON", "s",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CHECK_UNIT_S, NULL, NULL);

    g_hwndCheckM = CreateWindowExA(0, "BUTTON", "m",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CHECK_UNIT_M, NULL, NULL);

    g_hwndCheckH = CreateWindowExA(0, "BUTTON", "h",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_CHECK_UNIT_H, NULL, NULL);

    g_hwndButtonAdd = CreateWindowExA(0, "BUTTON", "Add", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_ADD_ACCOUNT, NULL, NULL);
    EnableWindow(g_hwndButtonAdd, TRUE);
    g_hwndButtonDelete = CreateWindowExA(0, "BUTTON", "Delete", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_DELETE_ACCOUNT, NULL, NULL);
    g_hwndButtonRefresh = CreateWindowExA(0, "BUTTON", "Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_REFRESH_LIST, NULL, NULL);
    g_hwndUpdateSettingsButton = CreateWindowExA(0, "BUTTON", "Update Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_BTN_UPDATE_SETTINGS, NULL, NULL);

    HWND buttons[] = {
        g_hwndButtonAdd,
        g_hwndButtonDelete,
        g_hwndButtonRefresh,
        g_hwndJoinButton,
        g_hwndAutoRejoinCheck,
        g_hwndCheckS,
        g_hwndCheckM,
        g_hwndCheckH,
        g_hwndUpdateSettingsButton
    };

    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        if (buttons[i]) {
            SendMessageA(buttons[i], WM_SETFONT, (WPARAM)hFont, TRUE);
        }
    }
    SendMessageA(g_hwndJoinLinkEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(g_hwndJoinPlaceEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(g_hwndJoinLinkLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(g_hwndJoinPlaceLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(g_hwndRejoinTimeEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    // Add Ctrl+A/C/V support to edits (only for Edit controls)
    // g_hwndJoinLinkEdit is now a ComboBox, so we don't subclass it with EditSubclassProc
    SetWindowSubclass(g_hwndJoinPlaceEdit, EditSubclassProc, 0, 0);
    SetWindowSubclass(g_hwndRejoinTimeEdit, EditSubclassProc, 0, 0);

    // Load persisted join settings
    LoadJoinSettings();
    if (g_joinLinkSaved[0]) {
        SetWindowTextA(g_hwndJoinLinkEdit, g_joinLinkSaved);
    }
    if (g_joinPlaceSaved[0]) {
        SetWindowTextA(g_hwndJoinPlaceEdit, g_joinPlaceSaved);
    }
    if (g_rejoinTimeSaved[0]) {
        SetWindowTextA(g_hwndRejoinTimeEdit, g_rejoinTimeSaved);
    }
    // Set unit check
    Button_SetCheck(g_hwndCheckS, g_rejoinUnitSaved == 0 ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g_hwndCheckM, g_rejoinUnitSaved == 1 ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(g_hwndCheckH, g_rejoinUnitSaved == 2 ? BST_CHECKED : BST_UNCHECKED);

    CreateAccountContextMenu();
    return TRUE;
}

static HBITMAP LoadBitmapFromFile(const char *filename)
{
    if (!filename) {
        return NULL;
    }

    wchar_t widePath[MAX_PATH] = {0};
    if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, widePath, MAX_PATH) == 0) {
        log_win_error("MultiByteToWideChar", GetLastError());
        return NULL;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL uninit = FALSE;
    if (SUCCEEDED(hr)) {
        uninit = hr != RPC_E_CHANGED_MODE;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        hr = S_OK;
    }
    if (FAILED(hr)) {
        log_win_error("CoInitializeEx", hr);
        return NULL;
    }

    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HBITMAP bitmap = NULL;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory, (void**)&factory);
    if (SUCCEEDED(hr)) {
        hr = factory->lpVtbl->CreateDecoderFromFilename(factory, widePath, NULL,
            GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->lpVtbl->Initialize(converter, (IWICBitmapSource*)frame, &GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    }

    UINT width = 0, height = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->lpVtbl->GetSize(converter, &width, &height);
    }

    if (SUCCEEDED(hr) && width > 0 && height > 0) {
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)width;
        bmi.bmiHeader.biHeight = -((LONG)height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *bits = NULL;
        bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bitmap && bits) {
            UINT stride = width * 4;
            UINT bufferSize = stride * height;
            hr = converter->lpVtbl->CopyPixels(converter, NULL, stride, bufferSize, (BYTE*)bits);
            if (FAILED(hr)) {
                DeleteObject(bitmap);
                bitmap = NULL;
            }
        } else {
            if (bitmap) {
                DeleteObject(bitmap);
                bitmap = NULL;
            }
            hr = E_FAIL;
        }
    }

    if (converter) {
        converter->lpVtbl->Release(converter);
    }
    if (frame) {
        frame->lpVtbl->Release(frame);
    }
    if (decoder) {
        decoder->lpVtbl->Release(decoder);
    }
    if (factory) {
        factory->lpVtbl->Release(factory);
    }
    if (uninit) {
        CoUninitialize();
    }

    return bitmap;
}

static BOOL ResolveAssetPath(const char *relativePath, char *buffer, size_t bufferSize)
{
    if (!relativePath || !buffer || bufferSize == 0) {
        return FALSE;
    }

    char baseDir[MAX_PATH] = {0};
    if (!GetAppDirectory(baseDir, sizeof(baseDir))) {
        return FALSE;
    }

    const char *prefixes[] = {"", "..\\", "..\\..\\", "..\\..\\..\\"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        int written = snprintf(buffer, bufferSize, "%s%s%s", baseDir, prefixes[i], relativePath);
        if (written <= 0 || written >= (int)bufferSize) {
            continue;
        }

        DWORD attributes = GetFileAttributesA(buffer);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            return TRUE;
        }
    }

    return FALSE;
}

static HBITMAP ScaleBitmapToFit(HBITMAP source, int maxWidth, int maxHeight, int *outWidth, int *outHeight)
{
    if (!source) {
        return NULL;
    }

    BITMAP srcInfo = {0};
    if (!GetObjectA(source, sizeof(srcInfo), &srcInfo)) {
        return source;
    }

    int width = srcInfo.bmWidth;
    int height = srcInfo.bmHeight;
    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;

    double scale = 1.0;
    if (maxWidth > 0 && width > maxWidth) {
        scale = fmin(scale, (double)maxWidth / (double)width);
    }
    if (maxHeight > 0 && height > maxHeight) {
        scale = fmin(scale, (double)maxHeight / (double)height);
    }

    if (scale >= 0.999) {
        return source;
    }

    int targetWidth = (int)fmax(1.0, floor(width * scale + 0.5));
    int targetHeight = (int)fmax(1.0, floor(height * scale + 0.5));

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = targetWidth;
    bmi.bmiHeader.biHeight = -targetHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *scaledBits = NULL;
    HBITMAP scaledBitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &scaledBits, NULL, 0);
    if (!scaledBitmap || !scaledBits) {
        if (scaledBitmap) {
            DeleteObject(scaledBitmap);
        }
        return source;
    }

    HDC srcDC = CreateCompatibleDC(NULL);
    HDC dstDC = CreateCompatibleDC(NULL);
    if (!srcDC || !dstDC) {
        if (srcDC) DeleteDC(srcDC);
        if (dstDC) DeleteDC(dstDC);
        DeleteObject(scaledBitmap);
        return source;
    }

    HBITMAP oldSrc = (HBITMAP)SelectObject(srcDC, source);
    HBITMAP oldDst = (HBITMAP)SelectObject(dstDC, scaledBitmap);
    SetStretchBltMode(dstDC, HALFTONE);
    StretchBlt(dstDC, 0, 0, targetWidth, targetHeight, srcDC, 0, 0, width, height, SRCCOPY);
    SelectObject(srcDC, oldSrc);
    SelectObject(dstDC, oldDst);
    DeleteDC(srcDC);
    DeleteDC(dstDC);

    if (outWidth) *outWidth = targetWidth;
    if (outHeight) *outHeight = targetHeight;

    DeleteObject(source);
    return scaledBitmap;
}

static HBITMAP LoadPngIcon(const char *relativePath, int targetSize)
{
    if (!relativePath) {
        return NULL;
    }

    char resolved[MAX_PATH] = {0};
    if (!ResolveAssetPath(relativePath, resolved, sizeof(resolved))) {
        log_error("Failed to resolve icon path.");
        return NULL;
    }

    HBITMAP bitmap = LoadBitmapFromFile(resolved);
    if (!bitmap) {
        return NULL;
    }

    if (targetSize <= 0) {
        return bitmap;
    }

    BITMAP info = {0};
    if (GetObjectA(bitmap, sizeof(info), &info)) {
        if (info.bmWidth == targetSize && info.bmHeight == targetSize) {
            return bitmap;
        }
    }

    int scaledWidth = targetSize;
    int scaledHeight = targetSize;
    return ScaleBitmapToFit(bitmap, targetSize, targetSize, &scaledWidth, &scaledHeight);
}

static void LoadPlaceIdImages(void)
{
    if (g_placeImagesLoaded) {
        return;
    }
    g_placeImagesLoaded = TRUE;

    char resolvedPath[MAX_PATH] = {0};

    g_hbmPlaceIdIcon = LoadPngIcon("assets\\info.jpg", PLACE_ID_ICON_DEFAULT);
    if (!g_hbmPlaceIdIcon) {
        log_error("Unable to load Place ID info icon.");
        g_placeIconWidth = PLACE_ID_ICON_DEFAULT;
        g_placeIconHeight = PLACE_ID_ICON_DEFAULT;
    } else {
        g_placeIconWidth = PLACE_ID_ICON_DEFAULT;
        g_placeIconHeight = PLACE_ID_ICON_DEFAULT;
    }

    if (ResolveAssetPath("assets\\instrukcja.png", resolvedPath, sizeof(resolvedPath))) {
        g_hbmPlaceIdTooltip = LoadBitmapFromFile(resolvedPath);
        if (g_hbmPlaceIdTooltip) {
            const int maxTooltipWidth = 600;
            const int maxTooltipHeight = 500;
            g_hbmPlaceIdTooltip = ScaleBitmapToFit(g_hbmPlaceIdTooltip, maxTooltipWidth, maxTooltipHeight,
                                                   &g_placeTooltipWidth, &g_placeTooltipHeight);
        }
    } else {
        log_error("Unable to resolve path for instrukcja.png");
    }

    if (!g_hbmPlaceIdTooltip) {
        g_placeTooltipWidth = 0;
        g_placeTooltipHeight = 0;
        log_error("Unable to load Place ID instructions image.");
    }
}

static BOOL CreatePlaceIdIconControl(HWND hwnd)
{
    if (!hwnd) {
        return FALSE;
    }

    g_hwndPlaceIdInfoIcon = CreateWindowExA(
        0,
        "STATIC",
        NULL,
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW | SS_NOTIFY,
        0,
        0,
        g_placeIconWidth,
        g_placeIconHeight,
        hwnd,
        NULL,
        GetModuleHandle(NULL),
        NULL);

    if (!g_hwndPlaceIdInfoIcon) {
        log_win_error("CreateWindowExA Place ID icon", GetLastError());
        return FALSE;
    }

    SetWindowSubclass(g_hwndPlaceIdInfoIcon, PlaceIdIconSubclassProc, 0, 0);
    return TRUE;
}

static BOOL CreatePlaceIdTooltipWindow(void)
{
    if (!g_hbmPlaceIdTooltip || g_placeTooltipWidth == 0 || g_placeTooltipHeight == 0) {
        return FALSE;
    }

    g_hwndPlaceIdInfoTooltip = CreateWindowExA(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "STATIC",
        NULL,
        WS_POPUP | WS_BORDER | SS_BITMAP,
        0,
        0,
        g_placeTooltipWidth,
        g_placeTooltipHeight,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL);

    if (!g_hwndPlaceIdInfoTooltip) {
        log_win_error("CreateWindowExA Place ID tooltip", GetLastError());
        return FALSE;
    }

    SendMessageA(g_hwndPlaceIdInfoTooltip, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_hbmPlaceIdTooltip);
    ShowWindow(g_hwndPlaceIdInfoTooltip, SW_HIDE);
    g_placeTooltipVisible = FALSE;
    return TRUE;
}

static void UpdatePlaceIdTooltipPosition(void)
{
    if (!g_hwndPlaceIdInfoTooltip || !g_hwndPlaceIdInfoIcon || g_placeTooltipWidth == 0 || g_placeTooltipHeight == 0) {
        return;
    }

    RECT iconRect = {0};
    GetWindowRect(g_hwndPlaceIdInfoIcon, &iconRect);
    int iconHeight = iconRect.bottom - iconRect.top;
    int offsetY = (iconHeight - g_placeTooltipHeight) / 2;

    int x = iconRect.right + PLACE_ID_ICON_MARGIN;
    int y = iconRect.top + offsetY;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    if (x + g_placeTooltipWidth > screenWidth - PLACE_ID_ICON_MARGIN) {
        x = screenWidth - g_placeTooltipWidth - PLACE_ID_ICON_MARGIN;
        if (x < 0) {
            x = 0;
        }
    }
    if (y + g_placeTooltipHeight > screenHeight - PLACE_ID_ICON_MARGIN) {
        y = screenHeight - g_placeTooltipHeight - PLACE_ID_ICON_MARGIN;
    }
    if (y < PLACE_ID_ICON_MARGIN) {
        y = PLACE_ID_ICON_MARGIN;
    }

    SetWindowPos(g_hwndPlaceIdInfoTooltip, HWND_TOPMOST, x, y, g_placeTooltipWidth, g_placeTooltipHeight, SWP_NOACTIVATE);
}

static void ShowPlaceIdTooltip(void)
{
    if (!g_hwndPlaceIdInfoTooltip || !g_hbmPlaceIdTooltip) {
        return;
    }
    UpdatePlaceIdTooltipPosition();
    if (!g_placeTooltipVisible) {
        ShowWindow(g_hwndPlaceIdInfoTooltip, SW_SHOWNOACTIVATE);
        g_placeTooltipVisible = TRUE;
    }
}

static void HidePlaceIdTooltip(void)
{
    if (!g_hwndPlaceIdInfoTooltip) {
        return;
    }
    if (g_placeTooltipVisible) {
        ShowWindow(g_hwndPlaceIdInfoTooltip, SW_HIDE);
        g_placeTooltipVisible = FALSE;
    }
}

static void CleanupPlaceIdResources(void)
{
    HidePlaceIdTooltip();

    if (g_hwndPlaceIdInfoTooltip) {
        SendMessageA(g_hwndPlaceIdInfoTooltip, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
        DestroyWindow(g_hwndPlaceIdInfoTooltip);
        g_hwndPlaceIdInfoTooltip = NULL;
    }

    if (g_hwndPlaceIdInfoIcon) {
        SendMessageA(g_hwndPlaceIdInfoIcon, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)NULL);
        RemoveWindowSubclass(g_hwndPlaceIdInfoIcon, PlaceIdIconSubclassProc, 0);
        g_hwndPlaceIdInfoIcon = NULL;
    }

    if (g_hbmPlaceIdTooltip) {
        DeleteObject(g_hbmPlaceIdTooltip);
        g_hbmPlaceIdTooltip = NULL;
    }
    if (g_hbmPlaceIdIcon) {
        DeleteObject(g_hbmPlaceIdIcon);
        g_hbmPlaceIdIcon = NULL;
    }

    g_placeImagesLoaded = FALSE;
    g_placeTooltipWidth = 0;
    g_placeTooltipHeight = 0;
    g_placeIconTracking = FALSE;
}

static LRESULT CALLBACK PlaceIdIconSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)uIdSubclass;
    (void)dwRefData;

    switch (msg) {
        case WM_MOUSEMOVE:
        {
            if (g_hbmPlaceIdTooltip && g_placeTooltipWidth > 0 && g_placeTooltipHeight > 0) {
                ShowPlaceIdTooltip();
            }
            if (!g_placeIconTracking) {
                TRACKMOUSEEVENT tme = {0};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_placeIconTracking = TRUE;
            }
            break;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            if (g_hbmPlaceIdIcon) {
                BITMAP bmp = {0};
                if (GetObjectA(g_hbmPlaceIdIcon, sizeof(bmp), &bmp)) {
                    HDC memDC = CreateCompatibleDC(hdc);
                    if (memDC) {
                        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, g_hbmPlaceIdIcon);
                        int dstX = (rect.right - rect.left - bmp.bmWidth) / 2;
                        int dstY = (rect.bottom - rect.top - bmp.bmHeight) / 2;
                        
                        // Simple BitBlt for JPG (no transparency)
                        BitBlt(hdc, dstX, dstY, bmp.bmWidth, bmp.bmHeight, memDC, 0, 0, SRCCOPY);
                        
                        SelectObject(memDC, oldBmp);
                        DeleteDC(memDC);
                    }
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSELEAVE:
        {
            g_placeIconTracking = FALSE;
            HidePlaceIdTooltip();
            break;
        }
        case WM_LBUTTONDOWN:
        {
            if (g_placeTooltipVisible) {
                HidePlaceIdTooltip();
            } else if (g_hbmPlaceIdTooltip && g_placeTooltipWidth > 0) {
                ShowPlaceIdTooltip();
            }
            break;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static int ShowUpdatePrompt(HWND hwnd, const UpdateAvailableInfo *info)
{
    if (!info) {
        return UPDATE_PROMPT_SNOOZE_ID;
    }

    wchar_t versionWide[64] = L"";
    if (info->version[0]) {
        MultiByteToWideChar(CP_UTF8, 0, info->version, -1, versionWide, ARRAYSIZE(versionWide));
    } else {
        wcscpy(versionWide, L"unknown");
    }

    wchar_t description[160] = L"";
    swprintf_s(description, ARRAYSIZE(description), L"New version available: %s", versionWide);

    wchar_t windowTitle[96] = L"";
    swprintf_s(windowTitle, ARRAYSIZE(windowTitle), L"%S Update", APP_NAME);

    TASKDIALOG_BUTTON buttons[] = {
        { UPDATE_PROMPT_INSTALL_ID, L"Update" },
        { UPDATE_PROMPT_SNOOZE_ID, L"Not now" }
    };

    TASKDIALOGCONFIG config = {0};
    config.cbSize = sizeof(config);
    config.hwndParent = hwnd;
    config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = windowTitle;
    config.pszMainInstruction = L"Update available";
    config.pszContent = description;
    config.pButtons = buttons;
    config.cButtons = ARRAYSIZE(buttons);
    config.nDefaultButton = UPDATE_PROMPT_INSTALL_ID;

    int pressed = UPDATE_PROMPT_SNOOZE_ID;
    HRESULT hr = TaskDialogIndirect(&config, &pressed, NULL, NULL);
    if (SUCCEEDED(hr)) {
        return pressed;
    }

    char fallback[160];
    snprintf(fallback, sizeof(fallback), "New version available: %s", info->version[0] ? info->version : "unknown");
    int result = MessageBoxA(hwnd, fallback, APP_NAME " Update", MB_ICONINFORMATION | MB_YESNO);
    return (result == IDYES) ? UPDATE_PROMPT_INSTALL_ID : UPDATE_PROMPT_SNOOZE_ID;
}

static void HandleUpdateAvailable(HWND hwnd, UpdateAvailableInfo *info)
{
    if (!info) {
        return;
    }

    UpdateAvailableInfo payload = *info;
    Updater_FreeInfo(info);

    int choice = ShowUpdatePrompt(hwnd, &payload);
    if (choice == UPDATE_PROMPT_INSTALL_ID) {
        if (Updater_PerformSelfUpdate(hwnd, &payload)) {
            MessageBoxA(hwnd, "Update downloaded successfully. MultiRoblox will restart now.", APP_NAME, MB_ICONINFORMATION | MB_OK);
            ExitProcess(0);
        } else {
            MessageBoxA(hwnd, "Failed to install update. Please try again later.", APP_NAME, MB_ICONERROR | MB_OK);
        }
    }
}

static void ShowUpdateSettingsDialog(HWND hwnd)
{
    if (!hwnd) {
        return;
    }

    HINSTANCE hInstance = GetModuleHandleA(NULL);
    INT_PTR result = DialogBoxParamA(hInstance, MAKEINTRESOURCEA(IDD_UPDATE_SETTINGS), hwnd, UpdateSettingsDialogProc, 0);
    if (result == IDOK) {
        Updater_BeginStartupCheck(hwnd);
    }
}

static void SetUpdateSettingsDialogFrequency(HWND hwndDlg, UpdateCheckFrequency frequency)
{
    int controlId = IDC_RADIO_UPDATE_EVERYDAY;
    switch (frequency) {
        case UPDATE_FREQUENCY_WEEKLY:
            controlId = IDC_RADIO_UPDATE_WEEKLY;
            break;
        case UPDATE_FREQUENCY_MONTHLY:
            controlId = IDC_RADIO_UPDATE_MONTHLY;
            break;
        case UPDATE_FREQUENCY_NEVER:
            controlId = IDC_RADIO_UPDATE_NEVER;
            break;
        case UPDATE_FREQUENCY_EVERYDAY:
        default:
            controlId = IDC_RADIO_UPDATE_EVERYDAY;
            break;
    }

    CheckRadioButton(hwndDlg, IDC_RADIO_UPDATE_EVERYDAY, IDC_RADIO_UPDATE_NEVER, controlId);
}

static UpdateCheckFrequency GetDialogUpdateFrequency(HWND hwndDlg)
{
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_UPDATE_WEEKLY) == BST_CHECKED) {
        return UPDATE_FREQUENCY_WEEKLY;
    }
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_UPDATE_MONTHLY) == BST_CHECKED) {
        return UPDATE_FREQUENCY_MONTHLY;
    }
    if (IsDlgButtonChecked(hwndDlg, IDC_RADIO_UPDATE_NEVER) == BST_CHECKED) {
        return UPDATE_FREQUENCY_NEVER;
    }
    return UPDATE_FREQUENCY_EVERYDAY;
}

static INT_PTR CALLBACK UpdateSettingsDialogProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG:
            SetUpdateSettingsDialogFrequency(hwndDlg, Updater_GetFrequency());
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                {
                    UpdateCheckFrequency frequency = GetDialogUpdateFrequency(hwndDlg);
                    Updater_SetFrequency(frequency);
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
    }

    return FALSE;
}

static void TrimInPlace(char *str)
{
    if (!str) return;
    char *start = str;
    while (*start && isspace((unsigned char)*start)) {
        ++start;
    }
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1))) {
        --end;
    }
    size_t len = (size_t)(end - start);
    if (start != str) {
        memmove(str, start, len);
    }
    str[len] = '\0';
}

static void StripIfContainsNonDigits(char *buffer)
{
    if (!buffer) return;
    for (size_t i = 0; buffer[i]; ++i) {
        if (!isdigit((unsigned char)buffer[i])) {
            buffer[0] = '\0';
            return;
        }
    }
}

static void StripIfContainsDigits(char *buffer)
{
    if (!buffer) return;
    for (size_t i = 0; buffer[i]; ++i) {
        if (isdigit((unsigned char)buffer[i])) {
            buffer[0] = '\0';
            return;
        }
    }
}

static void StripIfAllDigits(char *buffer)
{
    if (!buffer) return;
    size_t len = strlen(buffer);
    if (len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)buffer[i])) {
            return; // contains non-digit, keep
        }
    }
    // all digits -> clear
    buffer[0] = '\0';
}

static BOOL GetAppDirectory(char *output, size_t size)
{
    if (!output || size == 0) {
        return FALSE;
    }

    char modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return FALSE;
    }

    const char *lastSlash = strrchr(modulePath, '\\');
    size_t dirLength = 0;
    if (lastSlash) {
        dirLength = (size_t)(lastSlash - modulePath + 1);
    }

    if (dirLength > 0 && dirLength < size) {
        memcpy(output, modulePath, dirLength);
        output[dirLength] = '\0';
        return TRUE;
    }

    return FALSE;
}

static void LoadJoinSettings(void)
{
    char baseDir[MAX_PATH] = {0};
    if (!GetAppDirectory(baseDir, sizeof(baseDir))) {
        return;
    }

    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s%s", baseDir, "JoinSettings.txt");

    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }

    if (fgets(g_joinLinkSaved, sizeof(g_joinLinkSaved), file)) {
        TrimInPlace(g_joinLinkSaved);
    }
    if (fgets(g_joinPlaceSaved, sizeof(g_joinPlaceSaved), file)) {
        TrimInPlace(g_joinPlaceSaved);
    }
    
    // Read auto-rejoin state (0 or 1)
    char line[32] = {0};
    if (fgets(line, sizeof(line), file)) {
        int checked = atoi(line);
        if (g_hwndAutoRejoinCheck) {
            Button_SetCheck(g_hwndAutoRejoinCheck, checked ? BST_CHECKED : BST_UNCHECKED);
        }
    }
    
    // Read rejoin time
    if (fgets(g_rejoinTimeSaved, sizeof(g_rejoinTimeSaved), file)) {
        TrimInPlace(g_rejoinTimeSaved);
    }
    
    // Read rejoin unit
    if (fgets(line, sizeof(line), file)) {
        g_rejoinUnitSaved = atoi(line);
    }

    fclose(file);
}

static void SaveJoinSettings(HWND hwnd)
{
    if (!hwnd) return;

    GetWindowTextA(g_hwndJoinLinkEdit, g_joinLinkSaved, sizeof(g_joinLinkSaved));
    GetWindowTextA(g_hwndJoinPlaceEdit, g_joinPlaceSaved, sizeof(g_joinPlaceSaved));
    GetWindowTextA(g_hwndRejoinTimeEdit, g_rejoinTimeSaved, sizeof(g_rejoinTimeSaved));
    TrimInPlace(g_joinLinkSaved);
    TrimInPlace(g_joinPlaceSaved);
    TrimInPlace(g_rejoinTimeSaved);
    
    int checked = (Button_GetCheck(g_hwndAutoRejoinCheck) == BST_CHECKED) ? 1 : 0;
    int unit = 1;
    if (Button_GetCheck(g_hwndCheckS) == BST_CHECKED) unit = 0;
    else if (Button_GetCheck(g_hwndCheckM) == BST_CHECKED) unit = 1;
    else if (Button_GetCheck(g_hwndCheckH) == BST_CHECKED) unit = 2;

    char baseDir[MAX_PATH] = {0};
    if (!GetAppDirectory(baseDir, sizeof(baseDir))) {
        return;
    }

    char path[MAX_PATH] = {0};
    snprintf(path, sizeof(path), "%s%s", baseDir, "JoinSettings.txt");

    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }

    fprintf(file, "%s\n%s\n%d\n%s\n%d\n", g_joinLinkSaved, g_joinPlaceSaved, checked, g_rejoinTimeSaved, unit);
    fclose(file);
}

static void CreateAccountContextMenu(void)
{
    // Context menu removed
    if (g_hAccountMenu) {
        DestroyMenu(g_hAccountMenu);
        g_hAccountMenu = NULL;
    }
}

static void ResizeGuiControls(HWND hwnd)
{
    if (!g_hwndTitleLabel || !g_hwndInfoEdit || !g_hwndAccountsLabel || !g_hwndListView) {
        return;
    }

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    const int margin = 12;
    const int spacing = 8;
    const int titleHeight = 28;
    const int accountsLabelHeight = 20;
    const int buttonHeight = 30;
    const int statusHeight = 20;
    const int joinLabelHeight = 20; // Increased slightly for checkbox alignment
    const int joinEditHeight = 30;
    const int joinRowHeight = joinLabelHeight + joinEditHeight;

    int availableWidth = clientRect.right - clientRect.left - margin * 2;
    if (availableWidth < 100) {
        availableWidth = 100;
    }

    int infoTop = margin + titleHeight + spacing;
    int infoHeight = 150;
    int listTopBase = infoTop + infoHeight + spacing + accountsLabelHeight + spacing;
    
    // Calculate list height to leave room for join row, buttons, and status
    int listHeight = clientRect.bottom - listTopBase - spacing - joinRowHeight - spacing - buttonHeight - margin;
    if (listHeight < 100) {
        listHeight = 100;
    }

    int joinRowTop = listTopBase + listHeight + spacing;
    int buttonTop = joinRowTop + joinRowHeight + spacing;
    int statusTop = buttonTop + buttonHeight + spacing;

    int buttonCount = 4;
    int totalSpacing = spacing * (buttonCount - 1);
    int buttonWidth = (availableWidth - totalSpacing) / buttonCount;
    if (buttonWidth < 70) {
        buttonWidth = 70;
    }

    int requiredWidth = buttonCount * buttonWidth + totalSpacing;
    if (requiredWidth > availableWidth) {
        buttonWidth = (availableWidth - totalSpacing) / buttonCount;
        if (buttonWidth < 60) {
            buttonWidth = 60;
        }
    }

    int listWidth = availableWidth - 60;
    if (listWidth < 200) listWidth = 200;
    int listLeft = margin + (availableWidth - listWidth) / 2;

    MoveWindow(g_hwndTitleLabel, margin, margin, availableWidth, titleHeight, TRUE);
    MoveWindow(g_hwndInfoEdit, margin, infoTop, availableWidth, infoHeight, TRUE);
    MoveWindow(g_hwndAccountsLabel, listLeft, infoTop + infoHeight + spacing, listWidth, accountsLabelHeight, TRUE);
    MoveWindow(g_hwndListView, listLeft, listTopBase, listWidth, listHeight, TRUE);

    // Resize columns to fill width
    int colLastUsedWidth = 140;
    int scrollBarWidth = GetSystemMetrics(SM_CXVSCROLL);
    int colUsernameWidth = listWidth - colLastUsedWidth - scrollBarWidth - 4; 
    if (colUsernameWidth < 100) colUsernameWidth = 100;
    
    SendMessageA(g_hwndListView, LVM_SETCOLUMNWIDTH, (WPARAM)0, (LPARAM)colUsernameWidth);
    SendMessageA(g_hwndListView, LVM_SETCOLUMNWIDTH, (WPARAM)1, (LPARAM)colLastUsedWidth);

    // Join Row Layout
    int rightColumnWidth = 260; // Increased to accommodate Auto Rejoin + Time + Units
    int placeWidth = 160;
    int joinEditWidth = availableWidth - rightColumnWidth - placeWidth - (spacing * 2);
    if (joinEditWidth < 80) {
        joinEditWidth = 80;
    }
    int placeLeft = margin + joinEditWidth + spacing;
    int buttonLeft = placeLeft + placeWidth + spacing;
    int labelTop = joinRowTop;
    int editTop = joinRowTop + joinLabelHeight;

    int iconSpace = g_placeIconWidth + PLACE_ID_ICON_MARGIN;
    int placeIdLabelWidth = joinEditWidth - iconSpace;
    if (placeIdLabelWidth < 0) {
        placeIdLabelWidth = 0;
    }
    MoveWindow(g_hwndJoinLinkLabel, margin, labelTop, placeIdLabelWidth, joinLabelHeight, TRUE);
    if (g_hwndPlaceIdInfoIcon) {
        int iconX = margin + placeIdLabelWidth + PLACE_ID_ICON_MARGIN;
        int iconY = labelTop + (joinLabelHeight - g_placeIconHeight) / 2;
        MoveWindow(g_hwndPlaceIdInfoIcon, iconX, iconY, g_placeIconWidth, g_placeIconHeight, TRUE);
    }
    MoveWindow(g_hwndJoinPlaceLabel, placeLeft, labelTop, placeWidth, joinLabelHeight, TRUE);
    
    // Position Checkbox above Join Button
    // Layout: [Auto Rejoin] [Time Edit] [s] [m] [h]
    int arWidth = 90;
    int timeWidth = 40;
    int unitWidth = 35;
    
    if (g_hwndAutoRejoinCheck) {
        MoveWindow(g_hwndAutoRejoinCheck, buttonLeft, labelTop, arWidth, joinLabelHeight, TRUE);
    }
    if (g_hwndRejoinTimeEdit) {
        MoveWindow(g_hwndRejoinTimeEdit, buttonLeft + arWidth, labelTop, timeWidth, joinLabelHeight, TRUE);
    }
    if (g_hwndCheckS) {
        MoveWindow(g_hwndCheckS, buttonLeft + arWidth + timeWidth + 5, labelTop, unitWidth, joinLabelHeight, TRUE);
    }
    if (g_hwndCheckM) {
        MoveWindow(g_hwndCheckM, buttonLeft + arWidth + timeWidth + 5 + unitWidth, labelTop, unitWidth, joinLabelHeight, TRUE);
    }
    if (g_hwndCheckH) {
        MoveWindow(g_hwndCheckH, buttonLeft + arWidth + timeWidth + 5 + unitWidth * 2, labelTop, unitWidth, joinLabelHeight, TRUE);
    }

    // For ComboBox, the height must include the dropdown list height.
    // 200 should be enough for ~10 items.
    MoveWindow(g_hwndJoinLinkEdit, margin, editTop, joinEditWidth, 200, TRUE);
    MoveWindow(g_hwndJoinPlaceEdit, placeLeft, editTop, placeWidth, joinEditHeight, TRUE);
    MoveWindow(g_hwndJoinButton, buttonLeft, editTop, rightColumnWidth, joinEditHeight, TRUE);

    HWND buttons[] = {
        g_hwndButtonAdd, g_hwndButtonDelete, g_hwndButtonRefresh, g_hwndUpdateSettingsButton
    };

    int x = margin;
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        if (buttons[i]) {
            MoveWindow(buttons[i], x, buttonTop, buttonWidth, buttonHeight, TRUE);
            x += buttonWidth + spacing;
        }
    }

    (void)statusTop;

    if (g_placeTooltipVisible) {
        UpdatePlaceIdTooltipPosition();
    }
}

static int GetSelectedAccountIndex(void)
{
    if (!g_hwndListView) {
        return -1;
    }

    return (int)SendMessageA(g_hwndListView, LVM_GETNEXTITEM, (WPARAM)-1, (LPARAM)LVNI_SELECTED);
}

static const RbxAccount *GetSelectedAccount(void)
{
    int index = GetSelectedAccountIndex();
    if (index < 0) {
        return NULL;
    }

    return AM_GetAccountAt((size_t)index);
}

static void FormatLastUsed(char *buffer, size_t bufferSize, time_t timestamp)
{
    if (!buffer || bufferSize == 0) {
        return;
    }

    if (timestamp == 0) {
        strncpy(buffer, "Never", bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return;
    }

    struct tm tmInfo;
    localtime_s(&tmInfo, &timestamp);
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M", &tmInfo);
}

static void PopulateAccountList(void)
{
    if (!g_hwndListView) {
        return;
    }

    SendMessageA(g_hwndListView, LVM_DELETEALLITEMS, 0, 0);

    size_t count = AM_GetAccountCount();
    LVITEMA item = {0};
    char lastUsed[64];
    LVITEMA subItem = {0};
    subItem.mask = LVIF_TEXT;

    for (size_t i = 0; i < count; ++i) {
        const RbxAccount *account = AM_GetAccountAt(i);
        if (!account) {
            continue;
        }

        item.iItem = (int)i;
        item.mask = LVIF_TEXT;
        const char *usernameDisplay = account->username[0] ? account->username : "(unnamed)";
        item.pszText = (LPSTR)usernameDisplay;
        int inserted = (int)SendMessageA(g_hwndListView, LVM_INSERTITEMA, 0, (LPARAM)&item);
        if (inserted < 0) {
            continue;
        }

        FormatLastUsed(lastUsed, sizeof(lastUsed), account->last_used);
        subItem.iSubItem = COL_LAST_USED;
        subItem.pszText = lastUsed;
        SendMessageA(g_hwndListView, LVM_SETITEMTEXTA, (WPARAM)inserted, (LPARAM)&subItem);
    }

    UpdateActionButtons();
    
    // Reset focus to list view to ensure UI is responsive
    if (g_hwndListView) {
        SetFocus(g_hwndListView);
    }
}

static void UpdateActionButtons(void)
{
    BOOL hasSelection = GetSelectedAccountIndex() >= 0;
    EnableWindow(g_hwndButtonAdd, TRUE); // Add button should always be enabled
    EnableWindow(g_hwndButtonRefresh, TRUE); // Refresh button should always be enabled
    EnableWindow(g_hwndButtonDelete, hasSelection);
}

static BOOL CopyCookieToClipboard(HWND hwnd, const RbxAccount *account)
{
    if (!account || account->roblosecurity[0] == '\0') {
        MessageBoxA(hwnd, "Account does not have a .ROBLOSECURITY value.", g_windowTitle, MB_ICONINFORMATION);
        return FALSE;
    }

    if (!OpenClipboard(hwnd)) {
        log_win_error("OpenClipboard", GetLastError());
        return FALSE;
    }

    EmptyClipboard();
    size_t length = strlen(account->roblosecurity) + 1;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, length);
    if (!memory) {
        CloseClipboard();
        log_error("GlobalAlloc failed while copying cookie.");
        return FALSE;
    }

    void *destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        log_win_error("GlobalLock", GetLastError());
        return FALSE;
    }

    memcpy(destination, account->roblosecurity, length);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_TEXT, memory)) {
        log_win_error("SetClipboardData", GetLastError());
        GlobalFree(memory);
        CloseClipboard();
        return FALSE;
    }

    CloseClipboard();
    return TRUE;
}

typedef struct {
    char link[1024];
    char placeId[128];
    bool hasLink;
    bool hasPlace;
    int *checkedIndices;
    int checkedCount;
    HWND hwnd;
} JoinThreadParams;

static DWORD WINAPI JoinThreadProc(LPVOID lpParam) {
    JoinThreadParams *params = (JoinThreadParams*)lpParam;
    if (!params) return 0;

    int launchCount = 0;
    char effectiveLink[1400];

    for (int i = 0; i < params->checkedCount; ++i) {
        int index = params->checkedIndices[i];
        RbxAccount* account = AM_GetMutableAccountAt(index);
        if (account) {
            // Check if there's a previous process to close
            if (account->process_id > 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, account->process_id);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                    log_error("Closed previous instance for account.");
                }
                account->process_id = 0;
            }

            effectiveLink[0] = '\0';
            if (params->hasLink) {
                snprintf(effectiveLink, sizeof(effectiveLink), "%s", params->link);
                if (params->hasPlace) {
                    size_t curLen = strlen(effectiveLink);
                    snprintf(effectiveLink + curLen, sizeof(effectiveLink) - curLen, "%splaceId=%s",
                        strchr(params->link, '?') ? "&" : "?", params->placeId);
                }
            } else if (params->hasPlace) {
                snprintf(effectiveLink, sizeof(effectiveLink), "placeId=%s", params->placeId);
            }

            DWORD newPid = RL_LaunchAccount(account, effectiveLink);
            if (newPid > 0) {
                account->process_id = newPid;
                launchCount++;
                // Small delay to prevent overwhelming the system or API rate limits
                Sleep(2000); 
            }
        }
    }

    if (params->checkedIndices) {
        free(params->checkedIndices);
    }
    free(params);
    return 0;
}

static void OnAddAccount(HWND hwnd)
{
    AM_OpenBrowserLoginAndAddAccount(hwnd);
}

static void OnEditAccount(HWND hwnd)
{
    int index = GetSelectedAccountIndex();
    if (index < 0) {
        return;
    }

    RbxAccount *account = AM_GetMutableAccountAt((size_t)index);
    if (!account) {
        return;
    }

    AccountDialogData data = {0};
    strncpy(data.username, account->username, sizeof(data.username) - 1);
    strncpy(data.alias, account->alias, sizeof(data.alias) - 1);
    strncpy(data.group, account->group, sizeof(data.group) - 1);
    strncpy(data.description, account->description, sizeof(data.description) - 1);
    strncpy(data.roblosecurity, account->roblosecurity, sizeof(data.roblosecurity) - 1);
    data.isFavorite = account->is_favorite;

    if (!AD_ShowAccountDialog(hwnd, "Edit Roblox Account", &data, &data)) {
        return;
    }

    if (!AM_UpdateAccountFields(account, data.username, data.roblosecurity, data.alias, data.group, data.description)) {
        MessageBoxA(hwnd, "Unable to update account entry.", g_windowTitle, MB_ICONERROR);
        return;
    }

    AM_SetFavorite(account, data.isFavorite);
    if (!AS_SaveAccounts()) {
        MessageBoxA(hwnd, "Failed to save account data.", g_windowTitle, MB_ICONERROR);
    }

    PopulateAccountList();
}

static void OnDeleteAccount(HWND hwnd)
{
    const RbxAccount *account = GetSelectedAccount();
    if (!account) {
        return;
    }

    char prompt[256];
    snprintf(prompt, sizeof(prompt), "Delete account '%s'? This action cannot be undone.", account->username[0] ? account->username : account->alias);
    if (MessageBoxA(hwnd, prompt, g_windowTitle, MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    if (!AM_DeleteAccountById(account->id)) {
        MessageBoxA(hwnd, "Unable to delete the selected account.", g_windowTitle, MB_ICONERROR);
        return;
    }

    if (!AS_SaveAccounts()) {
        MessageBoxA(hwnd, "Failed to save account data.", g_windowTitle, MB_ICONERROR);
    }

    PopulateAccountList();
}

static void ExecuteJoinBatch(HWND hwnd)
{
    char link[1024];
    char placeId[128] = {0};
    
    // Field 1: Game Selection (ComboBox) -> Get Place ID from internal list
    int selectionIndex = (int)SendMessageA(g_hwndJoinLinkEdit, CB_GETCURSEL, 0, 0);
    if (selectionIndex >= 0 && selectionIndex < (int)(sizeof(g_games) / sizeof(g_games[0]))) {
        strncpy(placeId, g_games[selectionIndex].placeId, sizeof(placeId) - 1);
    } else {
        // Fallback or error if nothing selected (should default to 0)
        if (sizeof(g_games) > 0) {
            strncpy(placeId, g_games[0].placeId, sizeof(placeId) - 1);
        }
    }

    // Field 2: PV Server Link (URL)
    GetWindowTextA(g_hwndJoinPlaceEdit, link, sizeof(link));

    TrimInPlace(link);
    // placeId is already trimmed and valid from our internal list

    // Analyze content (Link field only)
    bool linkIsUrl = (strstr(link, "http://") || strstr(link, "https://") || strstr(link, "roblox.com"));
    
    // We no longer need to check for swaps because Place ID is fixed from the dropdown
    
    // If link is empty, that's fine (standard join)
    // If link is present, it should be a URL.

    
    // Swap Logic removed as Place ID is now a fixed selection
    // The Place ID comes from a controlled list, so it will always be valid digits.
    // The Link field is the only free text field.

    // Now apply strict filtering
    // StripIfContainsNonDigits(placeId); // Not needed, placeId is from internal list
    StripIfAllDigits(link); // Link field must not be pure digits (placeId)

    // Reset info text to default instructions unless we need to show an inline prompt
    SetWindowTextA(g_hwndInfoEdit, g_instructionText);

    bool hasLink = strlen(link) > 0;
    bool hasPlace = strlen(placeId) > 0;

    if (!hasPlace) {
        const char *msg = hasLink
            ? "Add Place ID to join with this VIP link."
            : "Add Place ID (and optionally a VIP link).";
        SetWindowTextA(g_hwndInfoEdit, msg);
        return;
    }

    // Iterate through checked items
    int count = ListView_GetItemCount(g_hwndListView);
    int checkedCount = 0;
    
    // First pass: count checked items
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(g_hwndListView, i)) {
            checkedCount++;
        }
    }

    if (checkedCount == 0) {
        MessageBoxA(hwnd, "Please check at least one account to join.", "MultiRoblox", MB_ICONWARNING);
        return;
    }

    // Persist fields
    SaveJoinSettings(hwnd);

    // Prepare params for thread
    JoinThreadParams *params = (JoinThreadParams*)malloc(sizeof(JoinThreadParams));
    if (!params) return;

    strncpy(params->link, link, sizeof(params->link));
    strncpy(params->placeId, placeId, sizeof(params->placeId));
    params->hasLink = hasLink;
    params->hasPlace = hasPlace;
    params->hwnd = hwnd;
    params->checkedCount = checkedCount;
    params->checkedIndices = (int*)malloc(sizeof(int) * checkedCount);
    
    if (!params->checkedIndices) {
        free(params);
        return;
    }

    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (ListView_GetCheckState(g_hwndListView, i)) {
            params->checkedIndices[idx++] = i;
        }
    }

    // Spawn thread
    HANDLE hThread = CreateThread(NULL, 0, JoinThreadProc, params, 0, NULL);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        log_error("Failed to create join thread.");
        free(params->checkedIndices);
        free(params);
    }
}

static void OnJoinGames(HWND hwnd)
{
    // Execute immediate join
    ExecuteJoinBatch(hwnd);

    // Handle Auto Rejoin Timer
    if (Button_GetCheck(g_hwndAutoRejoinCheck) == BST_CHECKED) {
        // Calculate interval
        char timeBuf[32];
        GetWindowTextA(g_hwndRejoinTimeEdit, timeBuf, sizeof(timeBuf));
        int val = atoi(timeBuf);
        if (val <= 0) val = 15; // fallback

        int multiplier = 60000; // default m
        if (Button_GetCheck(g_hwndCheckS) == BST_CHECKED) multiplier = 1000;
        else if (Button_GetCheck(g_hwndCheckH) == BST_CHECKED) multiplier = 3600000;

        int interval = val * multiplier;
        if (interval < 1000) interval = 1000; // min 1 sec

        // Start or reset timer
        SetTimer(hwnd, IDT_AUTO_REJOIN_TIMER, interval, NULL);
        log_error("Auto Rejoin: Timer started.");
    } else {
        // Stop timer
        KillTimer(hwnd, IDT_AUTO_REJOIN_TIMER);
        log_error("Auto Rejoin: Timer stopped.");
    }
}

static void HandleAccountCommand(HWND hwnd, UINT command)
{
    switch (command) {
        case ID_BTN_ADD_ACCOUNT:
            OnAddAccount(hwnd);
            break;
        case ID_BTN_DELETE_ACCOUNT:
            OnDeleteAccount(hwnd);
            break;
        case ID_BTN_REFRESH_LIST:
            PopulateAccountList();
            break;
        case ID_BTN_UPDATE_SETTINGS:
            ShowUpdateSettingsDialog(hwnd);
            break;
        case ID_BTN_JOIN_GAMES:
            OnJoinGames(hwnd);
            break;
        case ID_CHECK_AUTO_REJOIN:
            if (Button_GetCheck(g_hwndAutoRejoinCheck) == BST_UNCHECKED) {
                KillTimer(hwnd, IDT_AUTO_REJOIN_TIMER);
                log_error("Auto Rejoin: Timer stopped manually.");
            }
            break;
        case ID_CHECK_UNIT_S:
            if (Button_GetCheck(g_hwndCheckS) == BST_CHECKED) {
                Button_SetCheck(g_hwndCheckM, BST_UNCHECKED);
                Button_SetCheck(g_hwndCheckH, BST_UNCHECKED);
            } else {
                // Prevent unchecking all - force at least one
                if (Button_GetCheck(g_hwndCheckM) == BST_UNCHECKED && Button_GetCheck(g_hwndCheckH) == BST_UNCHECKED)
                    Button_SetCheck(g_hwndCheckS, BST_CHECKED);
            }
            break;
        case ID_CHECK_UNIT_M:
            if (Button_GetCheck(g_hwndCheckM) == BST_CHECKED) {
                Button_SetCheck(g_hwndCheckS, BST_UNCHECKED);
                Button_SetCheck(g_hwndCheckH, BST_UNCHECKED);
            } else {
                if (Button_GetCheck(g_hwndCheckS) == BST_UNCHECKED && Button_GetCheck(g_hwndCheckH) == BST_UNCHECKED)
                    Button_SetCheck(g_hwndCheckM, BST_CHECKED);
            }
            break;
        case ID_CHECK_UNIT_H:
            if (Button_GetCheck(g_hwndCheckH) == BST_CHECKED) {
                Button_SetCheck(g_hwndCheckS, BST_UNCHECKED);
                Button_SetCheck(g_hwndCheckM, BST_UNCHECKED);
            } else {
                if (Button_GetCheck(g_hwndCheckS) == BST_UNCHECKED && Button_GetCheck(g_hwndCheckM) == BST_UNCHECKED)
                    Button_SetCheck(g_hwndCheckH, BST_CHECKED);
            }
            break;
    }
}

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_CREATE:
            if (!CreateGuiControls(hwnd)) {
                MessageBoxA(hwnd, "Unable to initialize the interface controls.", g_windowTitle, MB_ICONERROR);
                return -1;
            }
            ResizeGuiControls(hwnd);
            return 0;
        case WM_COMMAND:
            HandleAccountCommand(hwnd, (UINT)LOWORD(wParam));
            return 0;
        case WM_TIMER:
            if (wParam == IDT_AUTO_REJOIN_TIMER) {
                log_error("Auto Rejoin: Timer triggered. Re-joining...");
                ExecuteJoinBatch(hwnd);
            }
            return 0;
        case WM_APP_UPDATE_AVAILABLE:
            HandleUpdateAvailable(hwnd, (UpdateAvailableInfo *)lParam);
            return 0;
        case WM_APP_ACCOUNT_REFRESH:
            PopulateAccountList();
            return 0;
        case WM_NOTIFY:
        {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr) {
                HWND hHeader = ListView_GetHeader(g_hwndListView);
                if (hdr->hwndFrom == hHeader) {
                    if (hdr->code == HDN_BEGINTRACKA || hdr->code == HDN_BEGINTRACKW ||
                        hdr->code == HDN_TRACKA || hdr->code == HDN_TRACKW ||
                        hdr->code == HDN_ENDTRACKA || hdr->code == HDN_ENDTRACKW) {
                        return TRUE; // block column resizing
                    }
                    if (hdr->code == HDN_ITEMCHANGINGA || hdr->code == HDN_ITEMCHANGINGW) {
                        LPNMHEADER ph = (LPNMHEADER)lParam;
                        if (ph && (ph->pitem) && (ph->pitem->mask & HDI_WIDTH)) {
                            return TRUE; // block width changes
                        }
                        return TRUE; // block column resizing
                    }
                } else if (hdr->hwndFrom == g_hwndListView && hdr->code == LVN_ITEMCHANGED) {
                    UpdateActionButtons();
                }
            }
            break;
        }
        case WM_SIZE:
            ResizeGuiControls(hwnd);
            return 0;
        case WM_DESTROY:
            if (g_hAccountMenu) {
                DestroyMenu(g_hAccountMenu);
                g_hAccountMenu = NULL;
            }
            CleanupPlaceIdResources();
            SaveJoinSettings(hwnd);
            KillTimer(hwnd, IDT_AUTO_REJOIN_TIMER);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    
    __try {
        return RunApplication(hInstance, nCmdShow);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        MessageBoxA(NULL, "Application crashed (SEH Exception)!", "MultiRoblox Critical Error", MB_ICONERROR);
        return -1;
    }
}
