// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <wchar.h>

namespace sally::unicode
{
enum class NameColumnViewMode
{
    Brief,
    Detailed,
};

struct NameWidthMeasurementPlan
{
    bool UseWide = false;
    int NameLength = 0;
    int ExtensionLength = 0;
};

inline bool ShouldUseSeparateExtensionColumn(bool isDir,
                                             bool sortDirsByExt,
                                             bool extensionInSeparateColumn,
                                             NameColumnViewMode viewMode)
{
    return viewMode == NameColumnViewMode::Detailed &&
           extensionInSeparateColumn &&
           (!isDir || sortDirsByExt);
}

inline int GetWideNameLengthForNameColumn(const wchar_t* nameW,
                                          bool isDir,
                                          bool sortDirsByExt,
                                          bool extensionInSeparateColumn,
                                          NameColumnViewMode viewMode = NameColumnViewMode::Detailed)
{
    if (nameW == NULL)
        return 0;

    int fullLen = (int)wcslen(nameW);
    if (!ShouldUseSeparateExtensionColumn(isDir, sortDirsByExt, extensionInSeparateColumn, viewMode))
        return fullLen;

    const wchar_t* dot = wcsrchr(nameW, L'.');
    if (dot == NULL || dot <= nameW)
        return fullLen; // ".htaccess" and names without extension stay in Name column

    return (int)(dot - nameW);
}

inline const wchar_t* GetWideExtensionStart(const wchar_t* nameW)
{
    if (nameW == NULL)
        return NULL;

    const wchar_t* dot = wcsrchr(nameW, L'.');
    if (dot == NULL || dot <= nameW)
        return NULL; // no extension or ".htaccess" style name

    return dot + 1;
}

inline NameWidthMeasurementPlan BuildNameWidthMeasurementPlan(const char* nameA,
                                                              int nameLenA,
                                                              const char* extA,
                                                              const wchar_t* nameW,
                                                              bool isDir,
                                                              bool sortDirsByExt,
                                                              bool extensionInSeparateColumn,
                                                              NameColumnViewMode viewMode = NameColumnViewMode::Detailed)
{
    NameWidthMeasurementPlan plan;
    plan.UseWide = (nameW != NULL);
    const bool splitExtension = ShouldUseSeparateExtensionColumn(isDir, sortDirsByExt, extensionInSeparateColumn, viewMode);

    if (plan.UseWide)
    {
        plan.NameLength = GetWideNameLengthForNameColumn(nameW, isDir, sortDirsByExt, extensionInSeparateColumn, viewMode);
        const wchar_t* extPosW = splitExtension ? GetWideExtensionStart(nameW) : NULL;
        plan.ExtensionLength = extPosW != NULL ? (int)wcslen(extPosW) : 0;
        return plan;
    }

    if (nameA == NULL)
        return plan;

    plan.NameLength = nameLenA;
    if (splitExtension && extA != NULL && extA[0] != 0 && extA > nameA + 1)
    {
        plan.NameLength = (int)(extA - nameA - 1);
        plan.ExtensionLength = nameLenA - (int)(extA - nameA);
    }
    return plan;
}
} // namespace sally::unicode
