// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <string>

namespace sally::tip_of_day
{
inline constexpr const wchar_t* kTipsRelativePathW = L"help\\tips.txt";

inline bool BuildModuleRelativePathFromModulePathW(const wchar_t* modulePath,
                                                   const wchar_t* relativePath,
                                                   std::wstring& result)
{
    result.clear();
    if (modulePath == nullptr || modulePath[0] == 0 || relativePath == nullptr)
        return false;

    std::wstring base(modulePath);
    size_t slash = base.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;

    result.assign(base.c_str(), slash + 1);
    while (*relativePath == L'\\' || *relativePath == L'/')
        ++relativePath;
    result.append(relativePath);
    return true;
}

inline bool BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& result)
{
    result.clear();

    for (DWORD capacity = MAX_PATH; capacity <= 32768; capacity *= 2)
    {
        std::wstring modulePath(capacity, L'\0');
        DWORD length = GetModuleFileNameW(module, modulePath.data(), capacity);
        if (length == 0)
            return false;
        if (length < capacity - 1)
        {
            modulePath.resize(length);
            return BuildModuleRelativePathFromModulePathW(modulePath.c_str(), relativePath, result);
        }
    }
    return false;
}

inline HANDLE OpenTipsFileForReadAtPathW(const wchar_t* fileName)
{
    return CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}

inline HANDLE OpenTipsFileForReadW(HINSTANCE module, std::wstring& fileName)
{
    if (!BuildModuleRelativePathW(module, kTipsRelativePathW, fileName))
    {
        fileName = kTipsRelativePathW;
        SetLastError(ERROR_PATH_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return OpenTipsFileForReadAtPathW(fileName.c_str());
}
} // namespace sally::tip_of_day
