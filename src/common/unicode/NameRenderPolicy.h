// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <wchar.h>

namespace sally::unicode
{
struct NameWidthMeasurementPlan
{
    bool UseWide = false;
    int NameLength = 0;
    int ExtensionLength = 0;
};

inline int GetWideNameLengthForNameColumn(const wchar_t* nameW,
                                          bool isDir,
                                          bool sortDirsByExt,
                                          bool extensionInSeparateColumn)
{
    if (nameW == NULL)
        return 0;

    int fullLen = (int)wcslen(nameW);
    if (!extensionInSeparateColumn || (isDir && !sortDirsByExt))
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
                                                              bool extensionInSeparateColumn)
{
    NameWidthMeasurementPlan plan;
    plan.UseWide = (nameW != NULL);

    if (plan.UseWide)
    {
        plan.NameLength = GetWideNameLengthForNameColumn(nameW, isDir, sortDirsByExt, extensionInSeparateColumn);
        const wchar_t* extPosW = extensionInSeparateColumn ? GetWideExtensionStart(nameW) : NULL;
        plan.ExtensionLength = extPosW != NULL ? (int)wcslen(extPosW) : 0;
        return plan;
    }

    if (nameA == NULL)
        return plan;

    plan.NameLength = nameLenA;
    if (extensionInSeparateColumn && extA != NULL && extA[0] != 0 && extA > nameA + 1 && (!isDir || sortDirsByExt))
    {
        plan.NameLength = (int)(extA - nameA - 1);
        plan.ExtensionLength = nameLenA - (int)(extA - nameA);
    }
    return plan;
}
} // namespace sally::unicode
