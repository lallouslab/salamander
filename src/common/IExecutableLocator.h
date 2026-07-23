// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <windows.h>

struct ExecutableLocationResult
{
    bool success;
    DWORD errorCode;

    static ExecutableLocationResult Ok() { return {true, ERROR_SUCCESS}; }
    static ExecutableLocationResult Error(DWORD errorCode) { return {false, errorCode}; }
};

enum class ExecutableLocationKind
{
    Direct,
    AppExecutionAlias,
    Unresolved,
};

struct ExecutableLocation
{
    ExecutableLocationKind kind = ExecutableLocationKind::Direct;
    std::wstring executablePath;    // Path passed to CreateProcess (keeps package activation semantics).
    std::wstring targetPath;        // Physical target, or executablePath for a direct executable.
    std::wstring packageFamilyName; // Present only for a resolved App Execution Alias.
};

class IExecutableLocator
{
public:
    virtual ~IExecutableLocator() = default;
    virtual ExecutableLocationResult FindOnPath(const wchar_t* executable,
                                                ExecutableLocation& location) = 0;
};

extern IExecutableLocator* gExecutableLocator;
IExecutableLocator* GetWin32ExecutableLocator();
