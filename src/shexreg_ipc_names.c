// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shexreg_ipc_names.h"

//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalShExt_SharedMemMutex"; // salshext.dll (Sal 2.5 beta 1)
//const char *SALSHEXT_SHAREDMEMNAME = "SalShExt_SharedMem";           // salshext.dll (Sal 2.5 beta 1)
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex"; // salexten.dll - pracovni verze, pred 2.5 beta 2
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem";           // salexten.dll - pracovni verze, pred 2.5 beta 2
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex2";// salexten.dll (od verze 2.5 beta 2) + salamext.dll
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem2";          // salexten.dll (od verze 2.5 beta 2) + salamext.dll
//const char *SALSHEXT_DOPASTEEVENTNAME = "SalExten_DoPasteEvent2";    // salamext.dll - pracovni verze pro 2.52 beta 1, pouzivala se jen pod Vista+
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex3";// salamext.dll (od verze 2.52 beta 1)
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem3";          // salamext.dll (od verze 2.52 beta 1)
//const char *SALSHEXT_DOPASTEEVENTNAME = "SalExten_DoPasteEvent3";    // salamext.dll (od verze 2.52 beta 1, pouziva se jen pod Vista+)
const char* SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex5"; // salextx64.dll (Sally 1.0)
const char* SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem5";           // salextx64.dll (Sally 1.0)
const char* SALSHEXT_DOPASTEEVENTNAME = "SalExten_DoPasteEvent5";     // salextx64.dll (Sally 1.0)

static int SALSHEXT_IsTruthyEnvValue(const char* value)
{
    return value != NULL &&
           (lstrcmpiA(value, "1") == 0 ||
            lstrcmpiA(value, "true") == 0 ||
            lstrcmpiA(value, "yes") == 0 ||
            lstrcmpiA(value, "on") == 0);
}

BOOL SALSHEXT_IsIpcIsolationEnabled()
{
    char enabled[16];
    char instanceId[128];
    DWORD enabledLen = GetEnvironmentVariableA(SALSHEXT_IPC_ISOLATION_ENV_A, enabled, sizeof(enabled));
    DWORD instanceLen = GetEnvironmentVariableA(SALSHEXT_INSTANCE_ID_ENV_A, instanceId, sizeof(instanceId));

    if (enabledLen == 0 || enabledLen >= sizeof(enabled) ||
        instanceLen == 0 || instanceLen >= sizeof(instanceId))
    {
        return FALSE;
    }
    return SALSHEXT_IsTruthyEnvValue(enabled);
}

static void SALSHEXT_SanitizeInstanceId(const char* instanceId, char* buffer, DWORD bufferSize)
{
    DWORD out = 0;
    if (bufferSize == 0)
        return;

    for (; instanceId != NULL && *instanceId != 0 && out + 1 < bufferSize; instanceId++)
    {
        unsigned char ch = (unsigned char)*instanceId;
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_')
        {
            buffer[out++] = (char)ch;
        }
        else if (out == 0 || buffer[out - 1] != '_')
        {
            buffer[out++] = '_';
        }
    }

    while (out > 0 && buffer[out - 1] == '_')
        out--;
    buffer[out] = 0;
}

static BOOL SALSHEXT_AppendNamePart(char* buffer, DWORD bufferSize, DWORD* length, const char* text)
{
    if (buffer == NULL || bufferSize == 0 || length == NULL || *length >= bufferSize)
        return FALSE;

    while (text != NULL && *text != 0)
    {
        if (*length + 1 >= bufferSize)
        {
            buffer[0] = 0;
            return FALSE;
        }
        buffer[*length] = *text;
        (*length)++;
        text++;
    }
    buffer[*length] = 0;
    return TRUE;
}

static const char* SALSHEXT_GetSharedObjectName(const char* baseName, char* buffer, DWORD bufferSize)
{
    char instanceId[128];
    char sanitized[128];
    DWORD instanceLen;
    DWORD length;

    if (!SALSHEXT_IsIpcIsolationEnabled())
        return baseName;

    instanceLen = GetEnvironmentVariableA(SALSHEXT_INSTANCE_ID_ENV_A, instanceId, sizeof(instanceId));
    if (instanceLen == 0 || instanceLen >= sizeof(instanceId))
        return baseName;

    SALSHEXT_SanitizeInstanceId(instanceId, sanitized, sizeof(sanitized));
    if (sanitized[0] == 0 || buffer == NULL || bufferSize == 0)
        return baseName;

    length = 0;
    buffer[0] = 0;
    if (!SALSHEXT_AppendNamePart(buffer, bufferSize, &length, baseName) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, "_") ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, sanitized))
    {
        return baseName;
    }
    return buffer;
}

const char* SALSHEXT_GetSharedMemMutexName(char* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_SHAREDMEMMUTEXNAME, buffer, bufferSize);
}

const char* SALSHEXT_GetSharedMemName(char* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_SHAREDMEMNAME, buffer, bufferSize);
}

const char* SALSHEXT_GetDoPasteEventName(char* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_DOPASTEEVENTNAME, buffer, bufferSize);
}
