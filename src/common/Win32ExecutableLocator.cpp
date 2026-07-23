// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_EXECUTABLE_LOCATOR_STANDALONE
#include "precomp.h"
#endif

#include "Win32ExecutableLocator.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <winioctl.h>

namespace
{
    constexpr DWORD kAppExecutionAliasVersion = 3;
    constexpr size_t kReparseHeaderSize = sizeof(DWORD) + sizeof(WORD) + sizeof(WORD);

    bool ReadUtf16String(const BYTE*& cursor, const BYTE* end, std::wstring& value)
    {
        value.clear();
        while (static_cast<size_t>(end - cursor) >= sizeof(uint16_t))
        {
            uint16_t codeUnit = 0;
            memcpy(&codeUnit, cursor, sizeof(codeUnit));
            cursor += sizeof(codeUnit);
            if (codeUnit == 0)
                return true;
            value.push_back(static_cast<wchar_t>(codeUnit));
        }
        value.clear();
        return false;
    }

    std::wstring RemoveExtendedPathPrefix(const std::wstring& path)
    {
        if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
            return L"\\\\" + path.substr(8);
        if (path.compare(0, 4, L"\\\\?\\") == 0)
            return path.substr(4);
        return path;
    }

    bool ResolveReparseTarget(const std::wstring& path, std::wstring& targetPath)
    {
        targetPath.clear();
        HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        DWORD size = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (size == 0)
        {
            CloseHandle(file);
            return false;
        }

        std::wstring buffer(static_cast<size_t>(size) + 1, L'\0');
        DWORD written = GetFinalPathNameByHandleW(file, &buffer[0], static_cast<DWORD>(buffer.size()),
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(file);
        if (written == 0 || written >= buffer.size())
            return false;

        buffer.resize(written);
        targetPath = RemoveExtendedPathPrefix(buffer);
        return true;
    }
} // namespace

bool ParseAppExecutionAliasReparseData(const void* data, size_t size,
                                       std::wstring& packageFamilyName,
                                       std::wstring& targetPath)
{
    packageFamilyName.clear();
    targetPath.clear();
    if (data == nullptr || size < kReparseHeaderSize + sizeof(DWORD))
        return false;

    const BYTE* bytes = static_cast<const BYTE*>(data);
    DWORD tag = 0;
    WORD dataLength = 0;
    memcpy(&tag, bytes, sizeof(tag));
    memcpy(&dataLength, bytes + sizeof(tag), sizeof(dataLength));
    if (tag != IO_REPARSE_TAG_APPEXECLINK ||
        static_cast<size_t>(dataLength) > size - kReparseHeaderSize ||
        dataLength < sizeof(DWORD) ||
        ((dataLength - sizeof(DWORD)) % sizeof(uint16_t)) != 0)
        return false;

    DWORD version = 0;
    memcpy(&version, bytes + kReparseHeaderSize, sizeof(version));
    if (version != kAppExecutionAliasVersion)
        return false;

    // Version 3 stores package family, entry point, target, and application type
    // as four consecutive NUL-terminated UTF-16 strings.
    const BYTE* cursor = bytes + kReparseHeaderSize + sizeof(version);
    const BYTE* end = bytes + kReparseHeaderSize + dataLength;
    std::wstring entryPoint;
    std::wstring applicationType;
    if (!ReadUtf16String(cursor, end, packageFamilyName) ||
        !ReadUtf16String(cursor, end, entryPoint) ||
        !ReadUtf16String(cursor, end, targetPath) ||
        !ReadUtf16String(cursor, end, applicationType) ||
        packageFamilyName.empty() || entryPoint.empty() || targetPath.empty() ||
        applicationType.empty() || cursor != end)
    {
        packageFamilyName.clear();
        targetPath.clear();
        return false;
    }
    return true;
}

class Win32ExecutableLocator : public IExecutableLocator
{
public:
    ExecutableLocationResult FindOnPath(const wchar_t* executable,
                                        ExecutableLocation& location) override
    {
        location = ExecutableLocation{};
        if (executable == nullptr || executable[0] == L'\0')
            return ExecutableLocationResult::Error(ERROR_INVALID_PARAMETER);

        DWORD size = SearchPathW(nullptr, executable, nullptr, 0, nullptr, nullptr);
        if (size == 0)
            return ExecutableLocationResult::Error(GetLastError());

        std::wstring buffer(static_cast<size_t>(size) + 1, L'\0');
        DWORD written = SearchPathW(nullptr, executable, nullptr,
                                    static_cast<DWORD>(buffer.size()), &buffer[0], nullptr);
        if (written == 0 || written >= buffer.size())
            return ExecutableLocationResult::Error(written == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER);

        buffer.resize(written);
        location.executablePath = buffer;
        location.targetPath = buffer;

        DWORD attributes = GetFileAttributesW(buffer.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            location.kind = ExecutableLocationKind::Unresolved;
            location.targetPath.clear();
            return ExecutableLocationResult::Ok();
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            return ExecutableLocationResult::Ok();

        HANDLE reparsePoint = CreateFileW(buffer.c_str(), 0,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                          nullptr);
        if (reparsePoint == INVALID_HANDLE_VALUE)
        {
            location.kind = ExecutableLocationKind::Unresolved;
            location.targetPath.clear();
            return ExecutableLocationResult::Ok();
        }

        std::vector<BYTE> reparseData(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        DWORD bytesReturned = 0;
        BOOL read = DeviceIoControl(reparsePoint, FSCTL_GET_REPARSE_POINT,
                                    nullptr, 0, reparseData.data(),
                                    static_cast<DWORD>(reparseData.size()),
                                    &bytesReturned, nullptr);
        CloseHandle(reparsePoint);
        if (!read || bytesReturned < sizeof(DWORD))
        {
            location.kind = ExecutableLocationKind::Unresolved;
            location.targetPath.clear();
            return ExecutableLocationResult::Ok();
        }

        DWORD tag = 0;
        memcpy(&tag, reparseData.data(), sizeof(tag));
        if (tag == IO_REPARSE_TAG_APPEXECLINK)
        {
            if (!ParseAppExecutionAliasReparseData(reparseData.data(), bytesReturned,
                                                   location.packageFamilyName,
                                                   location.targetPath))
            {
                location.kind = ExecutableLocationKind::Unresolved;
                location.targetPath.clear();
                return ExecutableLocationResult::Ok();
            }
            location.kind = ExecutableLocationKind::AppExecutionAlias;
            return ExecutableLocationResult::Ok();
        }

        if (!ResolveReparseTarget(buffer, location.targetPath))
        {
            location.kind = ExecutableLocationKind::Unresolved;
            location.targetPath.clear();
        }
        return ExecutableLocationResult::Ok();
    }
};

static Win32ExecutableLocator g_win32ExecutableLocator;
IExecutableLocator* gExecutableLocator = &g_win32ExecutableLocator;

IExecutableLocator* GetWin32ExecutableLocator()
{
    return &g_win32ExecutableLocator;
}
