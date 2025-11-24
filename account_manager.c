#include "account_manager.h"
#include "log.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <windows.h>

static RbxAccount *g_accounts = NULL;
static size_t g_accountCapacity = 0;
static size_t g_accountCount = 0;

static void CopyField(char *dest, const char *src, size_t destSize)
{
    if (!dest || destSize == 0) {
        return;
    }

    if (!src) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

static void GenerateAccountId(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return;
    }

    static LONG s_counter = 0;
    DWORD64 ticks = GetTickCount64();
    LONG count = InterlockedIncrement(&s_counter);
    _snprintf(buffer, size, "RBX-%016llX-%08lX", (unsigned long long)ticks, (unsigned long)count);
    buffer[size - 1] = '\0';
}

static BOOL EnsureCapacity(size_t required)
{
    if (required <= g_accountCapacity) {
        return TRUE;
    }

    size_t grow = g_accountCapacity ? g_accountCapacity * 2 : 8;
    while (grow < required) {
        grow *= 2;
    }

    RbxAccount *newBuffer = (RbxAccount *)realloc(g_accounts, grow * sizeof(RbxAccount));
    if (!newBuffer) {
        log_error("Failed to allocate account storage.");
        return FALSE;
    }

    g_accounts = newBuffer;
    g_accountCapacity = grow;
    return TRUE;
}

BOOL AM_Init(void)
{
    g_accounts = NULL;
    g_accountCapacity = 0;
    g_accountCount = 0;
    return TRUE;
}

void AM_Shutdown(void)
{
    free(g_accounts);
    g_accounts = NULL;
    g_accountCapacity = 0;
    g_accountCount = 0;
}

size_t AM_GetAccountCount(void)
{
    return g_accountCount;
}

const RbxAccount *AM_GetAccountAt(size_t index)
{
    if (index >= g_accountCount) {
        return NULL;
    }
    return &g_accounts[index];
}

RbxAccount *AM_GetMutableAccountAt(size_t index)
{
    if (index >= g_accountCount) {
        return NULL;
    }
    return &g_accounts[index];
}

RbxAccount *AM_FindAccountById(const char *id)
{
    if (!id) {
        return NULL;
    }

    for (size_t i = 0; i < g_accountCount; ++i) {
        if (_stricmp(g_accounts[i].id, id) == 0) {
            return &g_accounts[i];
        }
    }

    return NULL;
}

RbxAccount *AM_FindAccountByUsername(const char *username)
{
    if (!username) {
        return NULL;
    }

    for (size_t i = 0; i < g_accountCount; ++i) {
        if (_stricmp(g_accounts[i].username, username) == 0) {
            return &g_accounts[i];
        }
    }

    return NULL;
}

RbxAccount *AM_CreateAccount(const char *username, const char *roblosecurity, const char *alias, const char *group, const char *description)
{
    if (!EnsureCapacity(g_accountCount + 1)) {
        return NULL;
    }

    RbxAccount *entry = &g_accounts[g_accountCount];
    memset(entry, 0, sizeof(*entry));
    GenerateAccountId(entry->id, sizeof(entry->id));
    CopyField(entry->username, username, sizeof(entry->username));
    CopyField(entry->alias, alias, sizeof(entry->alias));
    CopyField(entry->group, group, sizeof(entry->group));
    CopyField(entry->description, description, sizeof(entry->description));
    CopyField(entry->roblosecurity, roblosecurity, sizeof(entry->roblosecurity));
    entry->last_used = 0;
    entry->is_favorite = FALSE;
    entry->disabled = FALSE;
    entry->launch_count = 0;
    entry->sort_order = (uint32_t)g_accountCount;

    ++g_accountCount;
    return entry;
}

BOOL AM_DeleteAccountById(const char *id)
{
    if (!id) {
        return FALSE;
    }

    for (size_t i = 0; i < g_accountCount; ++i) {
        if (_stricmp(g_accounts[i].id, id) == 0) {
            if (i + 1 < g_accountCount) {
                memmove(&g_accounts[i], &g_accounts[i + 1], (g_accountCount - i - 1) * sizeof(RbxAccount));
            }
            --g_accountCount;
            return TRUE;
        }
    }

    return FALSE;
}

BOOL AM_UpdateAccountFields(RbxAccount *account, const char *username, const char *roblosecurity, const char *alias, const char *group, const char *description)
{
    if (!account) {
        return FALSE;
    }

    if (username) {
        CopyField(account->username, username, sizeof(account->username));
    }

    if (roblosecurity) {
        CopyField(account->roblosecurity, roblosecurity, sizeof(account->roblosecurity));
    }

    if (alias) {
        CopyField(account->alias, alias, sizeof(account->alias));
    }

    if (group) {
        CopyField(account->group, group, sizeof(account->group));
    }

    if (description) {
        CopyField(account->description, description, sizeof(account->description));
    }

    return TRUE;
}

void AM_MarkAccountUsed(RbxAccount *account)
{
    if (!account) {
        return;
    }

    account->last_used = time(NULL);
    ++account->launch_count;
}

void AM_SetFavorite(RbxAccount *account, BOOL favorite)
{
    if (!account) {
        return;
    }

    account->is_favorite = favorite;
}

void AM_SetDisabled(RbxAccount *account, BOOL disabled)
{
    if (!account) {
        return;
    }

    account->disabled = disabled;
}

static BOOL ContainsTermInsensitive(const char *text, const char *term)
{
    if (!text || !term || term[0] == '\0') {
        return FALSE;
    }

    char lowerText[AM_ACCOUNT_FIELD_LEN * 2] = { 0 };
    char lowerTerm[AM_ACCOUNT_FIELD_LEN] = { 0 };

    size_t i = 0;
    for (; i < sizeof(lowerText) - 1 && text[i]; ++i) {
        lowerText[i] = (char)tolower((unsigned char)text[i]);
    }
    lowerText[i] = '\0';

    i = 0;
    for (; i < sizeof(lowerTerm) - 1 && term[i]; ++i) {
        lowerTerm[i] = (char)tolower((unsigned char)term[i]);
    }
    lowerTerm[i] = '\0';

    return strstr(lowerText, lowerTerm) != NULL;
}

size_t AM_FindAccountsByGroup(const char *group, size_t *outIndices, size_t maxIndices)
{
    size_t found = 0;
    if (!group || group[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; i < g_accountCount; ++i) {
        if (_stricmp(g_accounts[i].group, group) == 0) {
            if (outIndices && found < maxIndices) {
                outIndices[found] = i;
            }
            ++found;
        }
    }

    return found;
}

size_t AM_FindAccountsBySearch(const char *term, size_t *outIndices, size_t maxIndices)
{
    size_t found = 0;
    if (!term || term[0] == '\0') {
        return 0;
    }

    for (size_t i = 0; i < g_accountCount; ++i) {
        if (ContainsTermInsensitive(g_accounts[i].username, term) ||
            ContainsTermInsensitive(g_accounts[i].alias, term) ||
            ContainsTermInsensitive(g_accounts[i].description, term)) {
            if (outIndices && found < maxIndices) {
                outIndices[found] = i;
            }
            ++found;
        }
    }

    return found;
}

static BOOL s_lastUsedDescending = FALSE;
static BOOL s_usernameAscending = TRUE;

static int CompareLastUsedComparator(const void *a, const void *b)
{
    const RbxAccount *left = (const RbxAccount *)a;
    const RbxAccount *right = (const RbxAccount *)b;

    if (left->last_used < right->last_used) {
        return s_lastUsedDescending ? 1 : -1;
    }

    if (left->last_used > right->last_used) {
        return s_lastUsedDescending ? -1 : 1;
    }

    return 0;
}

static int CompareUsernameComparator(const void *a, const void *b)
{
    const RbxAccount *left = (const RbxAccount *)a;
    const RbxAccount *right = (const RbxAccount *)b;
    int result = _stricmp(left->username, right->username);
    return s_usernameAscending ? result : -result;
}

void AM_SortByLastUsed(BOOL descending)
{
    if (g_accountCount < 2) {
        return;
    }

    s_lastUsedDescending = descending;
    qsort(g_accounts, g_accountCount, sizeof(RbxAccount), CompareLastUsedComparator);
}

void AM_SortByUsername(BOOL ascending)
{
    if (g_accountCount < 2) {
        return;
    }

    s_usernameAscending = ascending;
    qsort(g_accounts, g_accountCount, sizeof(RbxAccount), CompareUsernameComparator);
}

void AM_ClearAccounts(void)
{
    g_accountCount = 0;
}
