// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_WINDOWS_TERMINAL_STANDALONE
#include "precomp.h"
#else
#include <windows.h>
#endif

#include "Win32Utf8.h"

bool Win32StrictUtf8ToWide(const char* text, size_t length, std::wstring& wide)
{
    wide.clear();
    if (text == nullptr)
        return false;
    if (length == 0)
        return true;
    if (length > static_cast<size_t>(INT_MAX))
        return false;

    int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                                       static_cast<int>(length), nullptr, 0);
    if (required <= 0)
        return false;
    wide.resize(required);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                               static_cast<int>(length), &wide[0], required) == required;
}
