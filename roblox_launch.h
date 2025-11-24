#ifndef ROBLOX_LAUNCH_H
#define ROBLOX_LAUNCH_H

#include "account_manager.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Launches a Roblox client for the given account, joining the game specified by the link.
// The link can be a standard game link or a private server link.
// Returns the Process ID (PID) of the launched Roblox game process (RobloxPlayerBeta.exe), or 0 on failure.
DWORD RL_LaunchAccount(const RbxAccount* account, const char* joinLink);

#ifdef __cplusplus
}
#endif

#endif // ROBLOX_LAUNCH_H
