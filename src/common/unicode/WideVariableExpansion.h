// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace sally::unicode
{

struct WideVarEntry
{
    const char* Name;
    std::wstring (*Execute)(void* param);
};

inline std::wstring AsciiToWide(const char* text, int len)
{
    std::wstring result;
    if (text == nullptr || len <= 0)
        return result;
    result.reserve(len);
    for (int i = 0; i < len; ++i)
        result.push_back((wchar_t)(unsigned char)text[i]);
    return result;
}

inline bool SegmentEqualsNoCase(const char* segment, int segmentLen, const char* name)
{
    int nameLen = (int)strlen(name);
    return segmentLen == nameLen && _strnicmp(segment, name, segmentLen) == 0;
}

inline const WideVarEntry* FindWideVarEntry(const WideVarEntry* entries, const char* name, int nameLen)
{
    if (entries == nullptr)
        return nullptr;
    for (const WideVarEntry* entry = entries; entry->Name != nullptr; ++entry)
    {
        if (SegmentEqualsNoCase(name, nameLen, entry->Name))
            return entry;
    }
    return nullptr;
}

inline bool ExpandWideVarString(const char* varText, const WideVarEntry* variables, void* param,
                                std::wstring* output, DWORD* varPlacements = nullptr,
                                int* varPlacementsCount = nullptr, bool detectMaxVarWidths = false,
                                int* maxVarWidths = nullptr, int maxVarWidthsCount = 0)
{
    if (varText == nullptr)
        return false;
    if (varPlacementsCount != nullptr && *varPlacementsCount > 0 && varPlacements == nullptr)
        return false;
    if (maxVarWidthsCount > 0 && maxVarWidths == nullptr)
        return false;

    const char* s = varText;
    int varPlacementIndex = 0;
    int varPlacementCapacity = varPlacementsCount != nullptr ? *varPlacementsCount : 0;
    int currentMaxVarIndex = 0;
    std::wstring result;

    while (*s != 0)
    {
        if (*s != '$')
        {
            if (output != nullptr)
                result.push_back((wchar_t)(unsigned char)*s);
            ++s;
            continue;
        }

        ++s;
        if (*s == 0)
            return false;

        std::wstring value;
        int valueOutLen = 0;
        bool detectMax = false;

        if (*s == '$')
        {
            value = L"$";
            ++s;
        }
        else if (*s == '(')
        {
            const char* var = s + 1;
            while (*s != ')' && *s != 0)
                ++s;
            if (*s == 0)
                return false;

            int varLen = (int)(s - var);
            const char* s2 = var;
            while (s2 < s)
            {
                if (*s2 == ':')
                {
                    if (*++s2 != ':')
                    {
                        int tmpLen = (int)(s - s2);
                        bool validMax = tmpLen == 3 && _strnicmp(s2, "max", 3) == 0;
                        detectMax = validMax && detectMaxVarWidths;
                        bool validNum = false;

                        if (!validMax && tmpLen > 0 && tmpLen <= 4)
                        {
                            const char* s3 = s2;
                            while (s3 < s && *s3 >= '0' && *s3 <= '9')
                                ++s3;
                            if (s3 == s)
                            {
                                char widthBuff[5] = {};
                                memcpy(widthBuff, s2, tmpLen);
                                valueOutLen = atoi(widthBuff);
                                validNum = valueOutLen >= 1;
                            }
                        }

                        if (!validMax && !validNum)
                            return false;
                        if (!detectMax && validMax)
                        {
                            if (currentMaxVarIndex >= maxVarWidthsCount)
                                return false;
                            valueOutLen = maxVarWidths[currentMaxVarIndex++];
                        }
                        varLen -= (int)(s - s2) + 1;
                    }
                    break;
                }
                ++s2;
            }

            const WideVarEntry* entry = FindWideVarEntry(variables, var, varLen);
            if (entry == nullptr || entry->Execute == nullptr)
                return false;
            value = entry->Execute(param);
            ++s;
        }
        else if (*s == '[')
        {
            const char* var = s + 1;
            while (*s != ']' && *s != 0)
                ++s;
            if (*s == 0)
                return false;

            std::wstring envName = AsciiToWide(var, (int)(s - var));
            DWORD needed = GetEnvironmentVariableW(envName.c_str(), nullptr, 0);
            if (needed > 0)
            {
                value.resize(needed);
                DWORD written = GetEnvironmentVariableW(envName.c_str(), value.data(), needed);
                if (written >= needed)
                    return false;
                value.resize(written);
            }
            ++s;
        }
        else
            return false;

        if (detectMax)
        {
            if (currentMaxVarIndex >= maxVarWidthsCount)
                return false;
            int len = (int)value.length();
            if (maxVarWidths[currentMaxVarIndex] < len)
                maxVarWidths[currentMaxVarIndex] = len;
            ++currentMaxVarIndex;
        }

        if (output != nullptr)
        {
            int offset = (int)result.length();
            int len = (int)value.length();
            int totalLen = valueOutLen > 0 ? valueOutLen : len;
            if (varPlacementIndex < varPlacementCapacity)
                varPlacements[varPlacementIndex++] = MAKELPARAM(offset, totalLen);
            else if (varPlacements != nullptr)
                return false;

            result.append(value.c_str(), min(totalLen, len));
            if (len < totalLen)
                result.append(totalLen - len, L' ');
        }
    }

    if (varPlacementsCount != nullptr)
        *varPlacementsCount = varPlacementIndex;
    if (output != nullptr)
        *output = result;
    return true;
}

inline std::wstring BuildCommandLineDirectoryPrefixW(const std::wstring& dir)
{
    std::wstring result = dir;
    result.push_back(L'>');
    return result;
}

inline size_t ExtensionOffsetAfterDotW(const std::wstring& name, bool isDir)
{
    if (isDir || name.empty())
        return std::wstring::npos;
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return name.length();
    return dot + 1;
}

inline std::wstring FileNamePartFromExtensionOffsetW(const std::wstring& formattedName, size_t extOffset)
{
    if (extOffset != std::wstring::npos && extOffset > 0 && extOffset <= formattedName.length())
        return formattedName.substr(0, extOffset - 1);
    return formattedName;
}

inline std::wstring FileExtensionFromExtensionOffsetW(const std::wstring& formattedName, size_t extOffset)
{
    if (extOffset != std::wstring::npos && extOffset < formattedName.length())
        return formattedName.substr(extOffset);
    return std::wstring();
}

inline bool TryWideToAnsiExact(const std::wstring& text, std::string& ansi)
{
    BOOL usedDefaultChar = FALSE;
    int len = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), (int)text.length(),
                                  nullptr, 0, nullptr, &usedDefaultChar);
    if (len < 0 || usedDefaultChar)
        return false;
    ansi.assign(len, '\0');
    if (len == 0)
        return true;
    usedDefaultChar = FALSE;
    int written = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.c_str(), (int)text.length(),
                                      ansi.data(), len, nullptr, &usedDefaultChar);
    if (written != len || usedDefaultChar)
        return false;
    return true;
}

inline std::string WideToUtf8(const std::wstring& text)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.length(),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return std::string();
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.length(),
                        result.data(), len, nullptr, nullptr);
    return result;
}

} // namespace sally::unicode
