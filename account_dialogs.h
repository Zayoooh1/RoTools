#pragma once

#include <windows.h>

#include "account_manager.h"

typedef struct AccountDialogData {
    char username[AM_ACCOUNT_FIELD_LEN];
    char alias[AM_ACCOUNT_FIELD_LEN];
    char group[AM_ACCOUNT_FIELD_LEN];
    char description[AM_ACCOUNT_FIELD_LEN];
    char roblosecurity[AM_ACCOUNT_COOKIE_LEN];
    BOOL isFavorite;
} AccountDialogData;

BOOL AD_ShowAccountDialog(HWND owner, const char *title, const AccountDialogData *initial, AccountDialogData *output);
