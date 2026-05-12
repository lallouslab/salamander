// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <windows.h>

class IFileSystem;

namespace sally::filesystem
{
struct CreateDirectoryPlan
{
    std::wstring fullPath;
    std::wstring parentPath;
    std::wstring nextFocus;
};

struct CreateDirectoryFailure
{
    enum Stage
    {
        kResolve,
        kParent,
        kLeaf
    } stage = kResolve;

    std::wstring path;
    DWORD errorCode = 0;
    int errorTextId = 0;
};

void NormalizeManualCreateInputW(std::wstring& inputPath);
bool PrepareCreateDirectoryTargetW(const std::wstring& inputPath,
                                   const wchar_t* currentDir,
                                   CreateDirectoryPlan& plan,
                                   CreateDirectoryFailure* failure = nullptr);
bool DirectoryExistsW(const std::wstring& path);
bool IsManualCreateLeafInvalidW(const std::wstring& path);
bool EnsureDirectoryTreeExistsW(const std::wstring& dirPath,
                                bool manualCreate,
                                std::wstring* firstCreatedDir = nullptr,
                                CreateDirectoryFailure* failure = nullptr);
} // namespace sally::filesystem

BOOL SalCreateDirectoryExW(const wchar_t* name, DWORD* err);
