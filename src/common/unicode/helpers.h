// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <windows.h>

// UTF-16 conversion helpers used during decoupling and Unicode work.
inline std::wstring AnsiToWide(const char* s)
{
    if (s == NULL)
        return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), len);
    return out;
}

// Convert wide string to ANSI (lossy for non-codepage characters)
inline std::string WideToAnsi(const wchar_t* s)
{
    if (s == NULL)
        return std::string();
    int len = WideCharToMultiByte(CP_ACP, 0, s, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return std::string();
    std::string out(len - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, s, -1, out.data(), len, NULL, NULL);
    return out;
}

inline std::string WideToAnsi(const std::wstring& s)
{
    return WideToAnsi(s.c_str());
}

// Write wide string to ANSI char buffer with size limit
inline void WideToAnsi(const std::wstring& s, char* buffer, int bufferSize)
{
    if (buffer == NULL || bufferSize <= 0)
        return;
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, buffer, bufferSize, NULL, NULL);
    buffer[bufferSize - 1] = '\0'; // Ensure null termination
}

// Trim spaces from the beginning and spaces/dots from the end of a filename
// component, matching Explorer's manual-create behavior.
inline BOOL MakeValidFileNameComponentW(wchar_t* path)
{
    if (path == NULL)
        return FALSE;

    BOOL changed = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        changed = TRUE;
    }

    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' ' || *(n - 1) == L'.'))
        n--;
    if (*n != 0)
    {
        *n = 0;
        changed = TRUE;
    }

    return changed;
}

// Format a wide string using printf-style formatting, returns std::wstring
template <typename... Args>
inline std::wstring FormatStrW(const wchar_t* format, Args... args)
{
    int len = _scwprintf(format, args...);
    if (len <= 0)
        return std::wstring();
    std::wstring out(len, L'\0');
    swprintf_s(out.data(), len + 1, format, args...);
    return out;
}

