// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/unicode/helpers.h"

#include <string>

namespace sally::unicode
{
inline bool WideStringRequiresWidePath(const std::wstring& value)
{
    std::string ansi;
    return !TryWideToAnsiRoundTripExact(value, ansi);
}
} // namespace sally::unicode
