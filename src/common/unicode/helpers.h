// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <windows.h>

namespace sally::unicode
{

// THE canonical \\?\ long-path decoration (kb/unicode Phase 0-c: the three
// per-TU copies were unified here; property-tested by gtest_path_decoration).
// Decorates only drive-absolute ("X:\...") and UNC ("\\server\...") paths at
// the long-path threshold; already-prefixed, relative, and device paths pass
// through unchanged. Core code should normally NOT call this — canonical
// COperation paths stay undecorated and Win32FileSystem decorates internally;
// this exists for the transitional call sites until Phase 6.
inline bool HasLongPathPrefixW(const std::wstring& path)
{
    return path.compare(0, 4, L"\\\\?\\") == 0;
}

inline std::wstring MakeLongPathSafeW(const std::wstring& path)
{
    if (path.length() < 240 || HasLongPathPrefixW(path))
        return path;
    if (path.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.length() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/'))
    {
        return L"\\\\?\\" + path;
    }
    return path;
}

} // namespace sally::unicode

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

namespace sally::unicode
{
// Converts a UTF-16 string to the process ANSI codepage only when the result
// round-trips exactly. Use this before falling back from a wide source of
// truth to a legacy ANSI mirror.
inline bool TryWideToAnsiRoundTripExact(const std::wstring& value, std::string& ansi)
{
    ansi.clear();
    if (value.empty())
        return true;

    BOOL usedDefaultChar = FALSE;
    int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                                       NULL, 0, NULL, &usedDefaultChar);
    if (required <= 0 || usedDefaultChar)
        return false;

    std::string converted((size_t)required, '\0');
    usedDefaultChar = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                            converted.data(), required, NULL, &usedDefaultChar) == 0 ||
        usedDefaultChar)
    {
        return false;
    }
    converted.resize((size_t)required - 1);

    int roundTripLen = MultiByteToWideChar(CP_ACP, 0, converted.c_str(), -1, NULL, 0);
    if (roundTripLen <= 0)
        return false;

    std::wstring roundTrip((size_t)roundTripLen, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, converted.c_str(), -1, roundTrip.data(), roundTripLen) == 0)
        return false;
    roundTrip.resize((size_t)roundTripLen - 1);
    if (roundTrip != value)
        return false;

    ansi = converted;
    return true;
}

// Panel directory-read contract (P7): a CFileData row keeps a wide NameW (rather
// than NULL) exactly when the ANSI Name cannot faithfully stand in for the wide
// name — i.e. when the CP_ACP conversion is lossy OR the name has any non-ASCII
// codepoint. The non-ASCII arm is a strict superset of the lossy set: it guards
// downstream AnsiToWide() under a possibly-different CP_ACP (e.g. a Korean name
// that round-trips under CP_ACP=949 still needs the original wide form). Pure
// decision, no allocation — the single authority for when a panel row is wide.
inline bool PanelNameNeedsWideName(const wchar_t* wideName, bool ansiConversionLossy)
{
    if (ansiConversionLossy)
        return true;
    if (wideName == nullptr)
        return false;
    for (const wchar_t* wp = wideName; *wp != L'\0'; ++wp)
        if (static_cast<unsigned>(*wp) > 0x7f)
            return true;
    return false;
}
} // namespace sally::unicode

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

