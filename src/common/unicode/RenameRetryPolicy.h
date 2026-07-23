// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::unicode
{
inline bool ShouldRetryUnicodeRenameAfterError(DWORD error)
{
    return error != ERROR_SUCCESS;
}
} // namespace sally::unicode
