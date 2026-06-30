// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "unicode/helpers.h"

#include <string>
#include <windows.h>

namespace sally
{
namespace safe_file
{
class PathContext
{
public:
    static PathContext FromAnsi(const char* fileName)
    {
        PathContext context;
        context.DisplayName = fileName != NULL ? fileName : "";
        context.WideName = AnsiToWide(fileName);
        context.ExactWide = false;
        return context;
    }

    static PathContext FromWide(const wchar_t* fileNameW, const char* displayName)
    {
        PathContext context;
        context.WideName = fileNameW != NULL ? fileNameW : L"";
        context.ExactWide = true;
        if (displayName != NULL && displayName[0] != 0)
            context.DisplayName = displayName;
        else
            context.DisplayName = WideToAnsi(context.WideName);
        return context;
    }

    const char* DisplayNameA() const { return DisplayName.c_str(); }
    const wchar_t* WideNameW() const { return WideName.c_str(); }
    const std::wstring& WideNameRef() const { return WideName; }
    bool HasExactWideName() const { return ExactWide; }

private:
    std::string DisplayName;
    std::wstring WideName;
    bool ExactWide = false;
};

inline HANDLE CreateFileExact(const PathContext& path,
                              DWORD desiredAccess,
                              DWORD shareMode,
                              LPSECURITY_ATTRIBUTES securityAttributes,
                              DWORD creationDisposition,
                              DWORD flagsAndAttributes,
                              HANDLE templateFile)
{
    return CreateFileW(path.WideNameW(), desiredAccess, shareMode, securityAttributes,
                       creationDisposition, flagsAndAttributes, templateFile);
}

inline DWORD GetFileAttributesExact(const PathContext& path)
{
    return GetFileAttributesW(path.WideNameW());
}

inline BOOL SetFileAttributesExact(const PathContext& path, DWORD fileAttributes)
{
    return SetFileAttributesW(path.WideNameW(), fileAttributes);
}

inline BOOL DeleteFileExact(const PathContext& path)
{
    return DeleteFileW(path.WideNameW());
}
} // namespace safe_file
} // namespace sally
