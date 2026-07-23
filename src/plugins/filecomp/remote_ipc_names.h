// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../shared/plugcore/messages.h"
#include "../shared/plugcore/messages_ipc_names.h"
#include "remotmsg.h"

namespace sally
{
namespace filecomp
{
inline void AppendHexNoLeading(char* dest, int destSize, DWORD value)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[9];
    int out = 0;
    BOOL seen = FALSE;
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        char digit = hex[(value >> shift) & 0x0f];
        if (digit != '0' || seen || shift == 0)
        {
            text[out++] = digit;
            seen = TRUE;
        }
    }
    text[out] = 0;
    sally::plugcore::AppendBounded(dest, destSize, text);
}

inline void AppendHex8(char* dest, int destSize, DWORD value)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[9];
    for (int i = 0; i < 8; ++i)
        text[i] = hex[(value >> (28 - i * 4)) & 0x0f];
    text[8] = 0;
    sally::plugcore::AppendBounded(dest, destSize, text);
}

inline DWORD HashFileCompInstanceId(const char* instanceId)
{
    char sanitized[128];
    sally::plugcore::SanitizeMessageCenterInstanceId(sanitized, (int)sizeof(sanitized), instanceId);

    DWORD hash = 2166136261u;
    for (const char* ch = sanitized; *ch != 0; ++ch)
    {
        hash ^= (BYTE)*ch;
        hash *= 16777619u;
    }
    return hash != 0 ? hash : 1;
}

inline void BuildFileCompStartedEventName(char* dest, int destSize, const char* instanceId)
{
    sally::plugcore::BuildMessageCenterObjectName(dest, destSize, StartedEventName, instanceId);
}

inline void BuildFileCompStartedEventNameForCurrentProcess(char* dest, int destSize)
{
    sally::plugcore::BuildMessageCenterObjectNameForCurrentProcess(dest, destSize, StartedEventName);
}

inline void BuildFileCompReleaseEventName(char* dest, int destSize, DWORD processId, const char* instanceId)
{
    if (dest == NULL || destSize <= 0)
        return;

    dest[0] = 0;

    char sanitized[128];
    sally::plugcore::SanitizeMessageCenterInstanceId(sanitized, (int)sizeof(sanitized), instanceId);
    if (sanitized[0] == 0)
    {
        sally::plugcore::AppendBounded(dest, destSize, "FCREMOTE");
        AppendHexNoLeading(dest, destSize, processId);
        return;
    }

    sally::plugcore::AppendBounded(dest, destSize, "FC");
    AppendHex8(dest, destSize, processId);
    AppendHex8(dest, destSize, HashFileCompInstanceId(sanitized));
}

inline void BuildFileCompReleaseEventNameForCurrentProcess(char* dest, int destSize, DWORD processId)
{
    char instanceId[128];
    sally::plugcore::GetMessageCenterInstanceIdForCurrentProcess(instanceId, (int)sizeof(instanceId));
    BuildFileCompReleaseEventName(dest, destSize, processId, instanceId);
}
} // namespace filecomp
} // namespace sally
