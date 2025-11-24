#pragma once

#include <windows.h>

typedef enum ASEncryptionMode {
    ASE_MODE_UNKNOWN = -1,
    ASE_MODE_DPAPI,
    ASE_MODE_PLAINTEXT,
} ASEncryptionMode;

#ifdef __cplusplus
extern "C" {
#endif

// Lifecycle helpers
BOOL AS_Initialize(void);
void AS_Shutdown(void);

// Persistence
BOOL AS_LoadAccounts(void);
BOOL AS_SaveAccounts(void);

// Optional master password handler (permits binding DPAPI entropy)
BOOL AS_SetMasterPassword(const char *password);
BOOL AS_IsMasterPasswordSet(void);
BOOL AS_IsMasterPasswordRequired(void);

// Status helpers for UI
BOOL AS_IsEncryptionDisabled(void);
ASEncryptionMode AS_GetEncryptionMode(void);

#ifdef __cplusplus
}
#endif
