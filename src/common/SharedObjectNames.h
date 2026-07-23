// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace sally::shared_object_names
{
inline bool IsObjectNameChar(char ch)
{
    return ch >= 'A' && ch <= 'Z' ||
           ch >= 'a' && ch <= 'z' ||
           ch >= '0' && ch <= '9' ||
           ch == '_' || ch == '-';
}

inline std::string SanitizeInstanceIdForObjectName(const std::string& instanceId)
{
    std::string sanitized;
    sanitized.reserve(instanceId.length());
    for (char ch : instanceId)
        sanitized.push_back(IsObjectNameChar(ch) ? ch : '_');

    while (!sanitized.empty() && sanitized.back() == '_')
        sanitized.pop_back();
    return sanitized;
}

inline std::string BuildSharedObjectName(const char* baseName, const std::string& instanceId)
{
    std::string name = baseName != nullptr ? baseName : "";
    std::string sanitized = SanitizeInstanceIdForObjectName(instanceId);
    if (!sanitized.empty())
    {
        name.push_back('_');
        name.append(sanitized);
    }
    return name;
}
} // namespace sally::shared_object_names
