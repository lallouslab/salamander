// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // mutex name used to access the shared memory (opened via OpenMutex after it was created with CreateMutex)
    extern const char* SALSHEXT_SHAREDMEMMUTEXNAME;
    // shared-memory name (opened via OpenFileMapping after being created with CreateFileMapping)
    extern const char* SALSHEXT_SHAREDMEMNAME;
    // event name used to request Paste in the source Salamander; used only on Vista+
    // (older OS versions can post WM_USER_SALSHEXT_PASTE directly from the copy hook; on Vista+
    // that post fails when Salamander runs "as admin")
    extern const char* SALSHEXT_DOPASTEEVENTNAME;

#define SALSHEXT_IPC_ISOLATION_ENV_A "SALLY_SHELL_EXTENSION_IPC_ISOLATION"
#define SALSHEXT_INSTANCE_ID_ENV_A "SALLY_INSTANCE_ID"

    // Compatibility names are the default. Isolation is opt-in so Explorer-side
    // shell-extension IPC is not silently separated from production Sally.
    BOOL SALSHEXT_IsIpcIsolationEnabled();
    const char* SALSHEXT_GetSharedMemMutexName(char* buffer, DWORD bufferSize);
    const char* SALSHEXT_GetSharedMemName(char* buffer, DWORD bufferSize);
    const char* SALSHEXT_GetDoPasteEventName(char* buffer, DWORD bufferSize);

#ifdef __cplusplus
} // extern "C"
#endif
