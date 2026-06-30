// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

class IEnvironment;

namespace sally::instance
{
constexpr const wchar_t* kInstanceIdEnvironmentVariableW = L"SALLY_INSTANCE_ID";

std::string SanitizeInstanceIdForObjectName(const std::string& instanceId);
std::string GetInstanceIdFromEnvironment(IEnvironment* environment = nullptr);
std::string BuildSharedObjectName(const char* baseName, const std::string& instanceId);
std::string BuildSharedObjectNameForCurrentInstance(const char* baseName);
bool IsInstanceIsolationEnabled(IEnvironment* environment = nullptr);
}
