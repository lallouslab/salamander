// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The FTP plugin still contains several Open Salamander-era call sites that
// relied on fixed char arrays being accepted by CRT/WinAPI wrappers. Keep the
// compatibility scope local to the plugin while preserving explicit sizes.

inline LPSTR _sal_lstrcpynA(CPathBuffer& dst, LPCSTR src)
{
    return _sal_lstrcpynA(dst.Get(), src, dst.Size());
}

template <size_t N>
inline LPSTR _sal_lstrcpynA(char (&dst)[N], LPCSTR src)
{
    return _sal_lstrcpynA(dst, src, static_cast<int>(N));
}

inline int _snprintf_s(CPathBuffer& dst, size_t sizeOfBuffer, size_t count, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = _vsnprintf_s(dst.Get(), sizeOfBuffer, count, format, args);
    va_end(args);
    return result;
}

inline int _snprintf_s(CPathBuffer& dst, size_t count, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = _vsnprintf_s(dst.Get(), dst.Size(), count, format, args);
    va_end(args);
    return result;
}
