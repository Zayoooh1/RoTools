#include "account_storage.h"
#include "account_manager.h"
#include "log.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>

#define ACCOUNT_DATA_FILE "AccountData.dat"
#define ACCOUNT_NO_ENCRYPTION_FILE "NoEncryption.IUnderstandTheRisks.iautamor"
#define ACCOUNT_STORAGE_SIGNATURE 0x5242584D // 'RBXM'
#define ACCOUNT_STORAGE_VERSION 2
#define AS_FLAG_MASTER_PASSWORD 0x1

#pragma pack(push, 1)
typedef struct AccountStorageHeader {
    uint32_t signature;
    uint32_t version;
    uint32_t accountCount;
    uint32_t flags;
} AccountStorageHeader;

typedef struct AccountStorageRecord {
    char id[AM_ACCOUNT_ID_LEN];
    char username[AM_ACCOUNT_FIELD_LEN];
    char alias[AM_ACCOUNT_FIELD_LEN];
    char description[AM_ACCOUNT_FIELD_LEN];
    char group[AM_ACCOUNT_FIELD_LEN];
    char roblosecurity[AM_ACCOUNT_COOKIE_LEN];
    uint64_t last_used;
    uint32_t launch_count;
    uint32_t sort_order;
    uint8_t is_favorite;
    uint8_t disabled;
} AccountStorageRecord;
#pragma pack(pop)

static char s_dataPath[MAX_PATH] = {0};
static char s_noEncryptionPath[MAX_PATH] = {0};
static BOOL s_pathsPrepared = FALSE;
static BOOL s_initialized = FALSE;
static ASEncryptionMode s_encryptionMode = ASE_MODE_DPAPI;
static BOOL s_masterPasswordSet = FALSE;
static BYTE s_masterPasswordEntropy[256] = {0};
static DWORD s_masterPasswordEntropySize = 0;
static BOOL s_masterPasswordRequired = FALSE;

static BOOL IsDevEnvironment(void)
{
    char modulePath[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return FALSE;
    }

    // Convert to lowercase for case-insensitive comparison
    for (DWORD i = 0; i < len; ++i) {
        modulePath[i] = (char)tolower((unsigned char)modulePath[i]);
    }

    // Check if path contains "build" or other dev indicators
    return (strstr(modulePath, "\\build\\") != NULL || 
            strstr(modulePath, "\\debug\\") != NULL ||
            strstr(modulePath, "\\release\\") != NULL);
}

static BOOL GetDataDirectory(char *output, size_t size)
{
    if (!output || size == 0) {
        return FALSE;
    }

    PWSTR localAppData = NULL;
    int written = 0;
    size_t len = 0;
    
    // Use different folder for dev environment to avoid mixing dev and production data
    const char *subdir = IsDevEnvironment() ? "\\RoTools_Dev\\" : "\\RoTools\\";

    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, KF_FLAG_DEFAULT, NULL, &localAppData);
    if (FAILED(hr) || !localAppData) {
        return FALSE;
    }

    // Build path: %LOCALAPPDATA%\RoTools\ or %LOCALAPPDATA%\RoTools_Dev\ (including trailing slash)
    written = WideCharToMultiByte(CP_UTF8, 0, localAppData, -1, output, (int)size, NULL, NULL);
    CoTaskMemFree(localAppData);
    if (written <= 0) {
        output[0] = '\0';
        return FALSE;
    }

    len = strlen(output);
    if (len + strlen(subdir) + 1 > size) {
        output[0] = '\0';
        return FALSE;
    }
    strcat(output, subdir);

    // Ensure directory exists
    CreateDirectoryA(output, NULL);
    return TRUE;
}

static BOOL PreparePaths(void)
{
    if (s_pathsPrepared) {
        return TRUE;
    }

    char baseDir[MAX_PATH] = {0};
    if (!GetDataDirectory(baseDir, sizeof(baseDir))) {
        log_error("Failed to determine data directory. LocalAppData is unavailable.");
        return FALSE;
    }

    _snprintf(s_dataPath, MAX_PATH, "%s%s", baseDir, ACCOUNT_DATA_FILE);
    _snprintf(s_noEncryptionPath, MAX_PATH, "%s%s", baseDir, ACCOUNT_NO_ENCRYPTION_FILE);
    s_pathsPrepared = TRUE;
    return TRUE;
}

static BOOL FileExists(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return (attributes != INVALID_FILE_ATTRIBUTES) && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void UpdateEncryptionMode(void)
{
    if (!s_pathsPrepared) {
        return;
    }

    if (FileExists(s_noEncryptionPath)) {
        s_encryptionMode = ASE_MODE_PLAINTEXT;
    } else {
        s_encryptionMode = ASE_MODE_DPAPI;
    }
}

static void ClearEntropy(void)
{
    SecureZeroMemory(s_masterPasswordEntropy, sizeof(s_masterPasswordEntropy));
    s_masterPasswordEntropySize = 0;
    s_masterPasswordSet = FALSE;
}

BOOL AS_Initialize(void)
{
    if (s_initialized) {
        return TRUE;
    }

    if (!PreparePaths()) {
        return FALSE;
    }

    s_encryptionMode = ASE_MODE_DPAPI;
    s_masterPasswordRequired = FALSE;
    ClearEntropy();
    s_initialized = TRUE;
    return TRUE;
}

void AS_Shutdown(void)
{
    if (!s_initialized) {
        return;
    }

    ClearEntropy();
    s_initialized = FALSE;
}

BOOL AS_SetMasterPassword(const char *password)
{
    if (!password || password[0] == '\0') {
        ClearEntropy();
        return TRUE;
    }

    size_t length = strlen(password);
    if (length > sizeof(s_masterPasswordEntropy) - 1) {
        length = sizeof(s_masterPasswordEntropy) - 1;
    }

    memcpy(s_masterPasswordEntropy, password, length);
    s_masterPasswordEntropy[length] = 0;
    s_masterPasswordEntropySize = (DWORD)length;
    s_masterPasswordSet = TRUE;
    return TRUE;
}

BOOL AS_IsMasterPasswordSet(void)
{
    return s_masterPasswordSet;
}

BOOL AS_IsMasterPasswordRequired(void)
{
    return s_masterPasswordRequired;
}

BOOL AS_IsEncryptionDisabled(void)
{
    if (!s_initialized) {
        return FALSE;
    }

    if (!s_pathsPrepared && !PreparePaths()) {
        return FALSE;
    }

    return FileExists(s_noEncryptionPath);
}

ASEncryptionMode AS_GetEncryptionMode(void)
{
    if (!s_initialized) {
        return ASE_MODE_UNKNOWN;
    }

    UpdateEncryptionMode();
    return s_encryptionMode;
}

static BOOL ReadFileToBuffer(const char *path, uint8_t **outData, size_t *outSize)
{
    if (!path || !outData || !outSize) {
        return FALSE;
    }

    *outData = NULL;
    *outSize = 0;

    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return TRUE;
        }
        log_win_error("CreateFileA (read)", err);
        return FALSE;
    }

    LARGE_INTEGER size = {0};
    if (!GetFileSizeEx(file, &size)) {
        log_win_error("GetFileSizeEx", GetLastError());
        CloseHandle(file);
        return FALSE;
    }

    if (size.QuadPart == 0) {
        CloseHandle(file);
        return TRUE;
    }

    if ((uint64_t)size.QuadPart > UINT32_MAX) {
        log_error("Account data file exceeds supported size.");
        CloseHandle(file);
        return FALSE;
    }

    uint8_t *buffer = (uint8_t *)malloc((size_t)size.QuadPart);
    if (!buffer) {
        log_error("Failed to allocate buffer for account data.");
        CloseHandle(file);
        return FALSE;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &bytesRead, NULL) || bytesRead != (DWORD)size.QuadPart) {
        log_win_error("ReadFile", GetLastError());
        SecureZeroMemory(buffer, (size_t)size.QuadPart);
        free(buffer);
        CloseHandle(file);
        return FALSE;
    }

    CloseHandle(file);
    *outData = buffer;
    *outSize = (size_t)size.QuadPart;
    return TRUE;
}

static BOOL WriteBufferToFile(const char *path, const void *data, size_t size)
{
    if (!path || (!data && size > 0)) {
        return FALSE;
    }

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        log_win_error("CreateFileA (write)", GetLastError());
        return FALSE;
    }

    if (size > UINT32_MAX) {
        log_error("Account data is too large to write.");
        CloseHandle(file);
        return FALSE;
    }

    DWORD written = 0;
    if (size > 0 && (!WriteFile(file, data, (DWORD)size, &written, NULL) || written != (DWORD)size)) {
        log_win_error("WriteFile", GetLastError());
        CloseHandle(file);
        return FALSE;
    }

    CloseHandle(file);
    return TRUE;
}

static BOOL FillEntropyBlob(DATA_BLOB *blob)
{
    if (!blob) {
        return FALSE;
    }

    if (!s_masterPasswordSet || s_masterPasswordEntropySize == 0) {
        blob->cbData = 0;
        blob->pbData = NULL;
        return FALSE;
    }

    blob->cbData = s_masterPasswordEntropySize;
    blob->pbData = s_masterPasswordEntropy;
    return TRUE;
}

static void CopyAccountFromRecord(const AccountStorageRecord *record)
{
    if (!record) {
        return;
    }

    RbxAccount *entry = AM_CreateAccount(record->username, record->roblosecurity, record->alias, record->group, record->description);
    if (!entry) {
        log_error("Unable to allocate account entry.");
        return;
    }

    strncpy(entry->id, record->id, sizeof(entry->id) - 1);
    entry->id[sizeof(entry->id) - 1] = '\0';
    entry->last_used = (time_t)record->last_used;
    entry->launch_count = record->launch_count;
    entry->sort_order = record->sort_order;
    entry->is_favorite = record->is_favorite ? TRUE : FALSE;
    entry->disabled = record->disabled ? TRUE : FALSE;
}

BOOL AS_LoadAccounts(void)
{
    if (!s_initialized && !AS_Initialize()) {
        return FALSE;
    }

    UpdateEncryptionMode();
    s_masterPasswordRequired = FALSE;

    uint8_t *rawData = NULL;
    size_t rawSize = 0;
    if (!ReadFileToBuffer(s_dataPath, &rawData, &rawSize)) {
        return FALSE;
    }

    BOOL success = FALSE;
    DATA_BLOB decrypted = {0};
    DATA_BLOB entropy = {0};
    DATA_BLOB *entropyPtr = NULL;

    if (!rawData || rawSize == 0) {
        AM_ClearAccounts();
        success = TRUE;
        goto LoadCleanup;
    }

    uint8_t *plainData = rawData;
    size_t plainSize = rawSize;

    if (s_encryptionMode == ASE_MODE_DPAPI) {
        if (FillEntropyBlob(&entropy)) {
            entropyPtr = &entropy;
        }

        DATA_BLOB input = { (DWORD)rawSize, rawData };
        if (!CryptUnprotectData(&input, NULL, entropyPtr, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &decrypted)) {
            log_win_error("CryptUnprotectData", GetLastError());
            SecureZeroMemory(rawData, rawSize);
            free(rawData);
            return FALSE;
        }

        plainData = decrypted.pbData;
        plainSize = decrypted.cbData;
    }

    if (plainSize < sizeof(AccountStorageHeader)) {
        log_error("Account data is corrupted or too small.");
        goto LoadCleanup;
    }

    AccountStorageHeader *header = (AccountStorageHeader *)plainData;
    if (header->signature != ACCOUNT_STORAGE_SIGNATURE) {
        log_error("Account data signature mismatch.");
        goto LoadCleanup;
    }

    if (header->version != ACCOUNT_STORAGE_VERSION) {
        log_error("Account data version mismatch.");
        goto LoadCleanup;
    }

    size_t expectedSize = sizeof(AccountStorageHeader);
    if (header->accountCount > UINT32_MAX / sizeof(AccountStorageRecord)) {
        log_error("Account data count is unreasonable.");
        goto LoadCleanup;
    }

    expectedSize += (size_t)header->accountCount * sizeof(AccountStorageRecord);
    if (plainSize < expectedSize) {
        log_error("Account data truncated.");
        goto LoadCleanup;
    }

    AccountStorageRecord *records = (AccountStorageRecord *)(plainData + sizeof(AccountStorageHeader));
    AM_ClearAccounts();
    for (size_t i = 0; i < header->accountCount; ++i) {
        CopyAccountFromRecord(&records[i]);
    }

    s_masterPasswordRequired = (header->flags & AS_FLAG_MASTER_PASSWORD) != 0;

    SecureZeroMemory(records, (size_t)header->accountCount * sizeof(AccountStorageRecord));
    SecureZeroMemory(header, sizeof(AccountStorageHeader));

    success = TRUE;

LoadCleanup:
    if (decrypted.pbData) {
        SecureZeroMemory(decrypted.pbData, decrypted.cbData);
        LocalFree(decrypted.pbData);
    }

    if (rawData) {
        SecureZeroMemory(rawData, rawSize);
        free(rawData);
    }
    return success;
}

static void FillRecordFromAccount(const RbxAccount *account, AccountStorageRecord *record)
{
    if (!account || !record) {
        return;
    }

    strncpy(record->id, account->id, sizeof(record->id) - 1);
    record->id[sizeof(record->id) - 1] = '\0';
    strncpy(record->username, account->username, sizeof(record->username) - 1);
    record->username[sizeof(record->username) - 1] = '\0';
    strncpy(record->alias, account->alias, sizeof(record->alias) - 1);
    record->alias[sizeof(record->alias) - 1] = '\0';
    strncpy(record->description, account->description, sizeof(record->description) - 1);
    record->description[sizeof(record->description) - 1] = '\0';
    strncpy(record->group, account->group, sizeof(record->group) - 1);
    record->group[sizeof(record->group) - 1] = '\0';
    strncpy(record->roblosecurity, account->roblosecurity, sizeof(record->roblosecurity) - 1);
    record->roblosecurity[sizeof(record->roblosecurity) - 1] = '\0';
    record->last_used = (uint64_t)account->last_used;
    record->launch_count = account->launch_count;
    record->sort_order = account->sort_order;
    record->is_favorite = account->is_favorite ? 1 : 0;
    record->disabled = account->disabled ? 1 : 0;
}

BOOL AS_SaveAccounts(void)
{
    if (!s_initialized && !AS_Initialize()) {
        return FALSE;
    }

    UpdateEncryptionMode();

    size_t count = AM_GetAccountCount();
    size_t payloadSize = sizeof(AccountStorageHeader) + count * sizeof(AccountStorageRecord);
    uint8_t *payload = (uint8_t *)malloc(payloadSize);
    if (!payload) {
        log_error("Failed to allocate serialization buffer.");
        return FALSE;
    }

    AccountStorageHeader *header = (AccountStorageHeader *)payload;
    header->signature = ACCOUNT_STORAGE_SIGNATURE;
    header->version = ACCOUNT_STORAGE_VERSION;
    header->accountCount = (uint32_t)count;
    if (s_masterPasswordSet && s_masterPasswordEntropySize > 0) {
        header->flags |= AS_FLAG_MASTER_PASSWORD;
    } else {
        header->flags &= ~AS_FLAG_MASTER_PASSWORD;
    }

    AccountStorageRecord *records = (AccountStorageRecord *)(payload + sizeof(AccountStorageHeader));
    for (size_t i = 0; i < count; ++i) {
        const RbxAccount *account = AM_GetAccountAt(i);
        FillRecordFromAccount(account, &records[i]);
    }

    BOOL writeResult = FALSE;

    if (s_encryptionMode == ASE_MODE_PLAINTEXT) {
        writeResult = WriteBufferToFile(s_dataPath, payload, payloadSize);
    } else {
        DATA_BLOB input = { (DWORD)payloadSize, payload };
        DATA_BLOB output = {0};
        DATA_BLOB entropy = {0};
        DATA_BLOB *entropyPtr = NULL;

        if (FillEntropyBlob(&entropy)) {
            entropyPtr = &entropy;
        }

        if (!CryptProtectData(&input, L"MultiRoblox account data", entropyPtr, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            log_win_error("CryptProtectData", GetLastError());
            goto SaveCleanup;
        }

        writeResult = WriteBufferToFile(s_dataPath, output.pbData, output.cbData);

        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }

SaveCleanup:
    SecureZeroMemory(payload, payloadSize);
    free(payload);
    return writeResult;
}
