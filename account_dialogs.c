#include "account_dialogs.h"
#include "resource.h"

#include <string.h>
#include <windows.h>

static const char *s_dialogTitle = NULL;

static void PopulateDialogControls(HWND hwnd, const AccountDialogData *data)
{
    if (!data) {
        return;
    }

    SetDlgItemTextA(hwnd, IDC_EDIT_USERNAME, data->username);
    SetDlgItemTextA(hwnd, IDC_EDIT_ALIAS, data->alias);
    SetDlgItemTextA(hwnd, IDC_EDIT_GROUP, data->group);
    SetDlgItemTextA(hwnd, IDC_EDIT_DESCRIPTION, data->description);
    SetDlgItemTextA(hwnd, IDC_EDIT_ROBLOSECURITY, data->roblosecurity);
    CheckDlgButton(hwnd, IDC_CHECK_FAVORITE, data->isFavorite ? BST_CHECKED : BST_UNCHECKED);
}

static void RetrieveDialogValues(HWND hwnd, AccountDialogData *data)
{
    if (!data) {
        return;
    }

    GetDlgItemTextA(hwnd, IDC_EDIT_USERNAME, data->username, sizeof(data->username));
    GetDlgItemTextA(hwnd, IDC_EDIT_ALIAS, data->alias, sizeof(data->alias));
    GetDlgItemTextA(hwnd, IDC_EDIT_GROUP, data->group, sizeof(data->group));
    GetDlgItemTextA(hwnd, IDC_EDIT_DESCRIPTION, data->description, sizeof(data->description));
    GetDlgItemTextA(hwnd, IDC_EDIT_ROBLOSECURITY, data->roblosecurity, sizeof(data->roblosecurity));
    data->isFavorite = (IsDlgButtonChecked(hwnd, IDC_CHECK_FAVORITE) == BST_CHECKED);
}

static INT_PTR CALLBACK AccountDialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_INITDIALOG:
        {
            AccountDialogData *data = (AccountDialogData *)lParam;
            if (s_dialogTitle) {
                SetWindowTextA(hwnd, s_dialogTitle);
            }
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)data);
            PopulateDialogControls(hwnd, data);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                {
                    AccountDialogData *data = (AccountDialogData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
                    if (data) {
                        RetrieveDialogValues(hwnd, data);
                    }
                    EndDialog(hwnd, IDOK);
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }

    return FALSE;
}

BOOL AD_ShowAccountDialog(HWND owner, const char *title, const AccountDialogData *initial, AccountDialogData *output)
{
    AccountDialogData data = {0};
    if (initial) {
        memcpy(&data, initial, sizeof(data));
    }

    s_dialogTitle = title ? title : "Account Details";

    HINSTANCE instance = GetModuleHandleA(NULL);
    INT_PTR result = DialogBoxParamA(instance, MAKEINTRESOURCEA(IDD_ACCOUNT_DIALOG), owner, AccountDialogProc, (LPARAM)&data);

    s_dialogTitle = NULL;

    if (result == IDOK) {
        if (output) {
            memcpy(output, &data, sizeof(data));
        }
        return TRUE;
    }

    return FALSE;
}
