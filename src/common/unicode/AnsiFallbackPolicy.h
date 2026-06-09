// SPDX-FileCopyrightText: 2026 Sally Authors
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
