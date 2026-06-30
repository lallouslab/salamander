// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/SharedObjectNames.h"

#include <windows.h>
#include <string>

namespace sally::translator_ipc
{
inline constexpr const char* kTranslatorInstanceIdEnvironmentVariableA = "SALLY_INSTANCE_ID";
inline constexpr const char* kTranslatorSharedMemoryBaseName = "Local\\AltapTranslatorSharedMemory";

inline std::string GetTranslatorInstanceIdFromProcessEnvironment()
{
    DWORD needed = GetEnvironmentVariableA(kTranslatorInstanceIdEnvironmentVariableA, nullptr, 0);
    if (needed == 0)
    {
#ifdef _DEBUG
        return "Debug";
#else
        return {};
#endif
    }

    std::string value(needed, '\0');
    DWORD written = GetEnvironmentVariableA(kTranslatorInstanceIdEnvironmentVariableA, value.data(), needed);
    if (written == 0 || written >= needed)
        return {};

    value.resize(written);
    return value;
}

inline std::string BuildTranslatorSharedMemoryName(const char* instanceId)
{
    return sally::shared_object_names::BuildSharedObjectName(
        kTranslatorSharedMemoryBaseName,
        instanceId != nullptr ? instanceId : "");
}

inline std::string BuildTranslatorSharedMemoryNameForCurrentProcess()
{
    std::string instanceId = GetTranslatorInstanceIdFromProcessEnvironment();
    return BuildTranslatorSharedMemoryName(instanceId.c_str());
}
} // namespace sally::translator_ipc
