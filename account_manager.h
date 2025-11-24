#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <windows.h>

#define AM_ACCOUNT_ID_LEN 48
#define AM_ACCOUNT_FIELD_LEN 128
#define AM_ACCOUNT_COOKIE_LEN 4096

typedef struct RbxAccount {
    char id[AM_ACCOUNT_ID_LEN];
    char username[AM_ACCOUNT_FIELD_LEN];
    char alias[AM_ACCOUNT_FIELD_LEN];
    char description[AM_ACCOUNT_FIELD_LEN];
    char group[AM_ACCOUNT_FIELD_LEN];
    char roblosecurity[AM_ACCOUNT_COOKIE_LEN];
    time_t last_used;
    BOOL is_favorite;
    BOOL disabled;
    uint32_t launch_count;
    uint32_t sort_order;
    DWORD process_id;
} RbxAccount;

#ifdef __cplusplus
extern "C" {
#endif

BOOL AM_Init(void);
void AM_Shutdown(void);
size_t AM_GetAccountCount(void);
const RbxAccount *AM_GetAccountAt(size_t index);
RbxAccount *AM_GetMutableAccountAt(size_t index);
RbxAccount *AM_FindAccountById(const char *id);
RbxAccount *AM_FindAccountByUsername(const char *username);
RbxAccount *AM_CreateAccount(const char *username, const char *roblosecurity, const char *alias, const char *group, const char *description);
BOOL AM_DeleteAccountById(const char *id);
BOOL AM_UpdateAccountFields(RbxAccount *account, const char *username, const char *roblosecurity, const char *alias, const char *group, const char *description);
void AM_MarkAccountUsed(RbxAccount *account);
void AM_SetFavorite(RbxAccount *account, BOOL favorite);
void AM_SetDisabled(RbxAccount *account, BOOL disabled);
size_t AM_FindAccountsByGroup(const char *group, size_t *outIndices, size_t maxIndices);
size_t AM_FindAccountsBySearch(const char *term, size_t *outIndices, size_t maxIndices);
void AM_SortByLastUsed(BOOL descending);
void AM_SortByUsername(BOOL ascending);
void AM_ClearAccounts(void);

#ifdef __cplusplus
}
#endif
