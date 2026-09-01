// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_PASTE_PATH_POLICY_STANDALONE lets the headless test compile this real translation
// unit without Sally's precompiled header.
#ifndef SALLY_PASTE_PATH_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "paste_path_policy.h"
#include "common/SalPathWide.h"

namespace sally
{

std::string PanelAnsiNameFromWideW(const wchar_t* wideName)
{
    if (wideName == nullptr || wideName[0] == 0)
        return std::string();

    // Deliberately mirrors files_window_directory_read.cpp's conversion, flags included.
    // WC_NO_BEST_FIT_CHARS matters: without it Windows maps some characters to visually
    // similar ANSI ones, which would spell the name differently than the panel does and the
    // focus comparison would silently miss.
    int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wideName, -1,
                                       nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return std::string();

    std::string ansi((size_t)required - 1, '\0');
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wideName, -1,
                            &ansi[0], required, nullptr, nullptr) == 0)
    {
        return std::string();
    }
    return ansi;
}

bool ResolvePastedFilePathW(const wchar_t* fullPath, DWORD attrs,
                            std::wstring& directoryOut, std::string& focusNameAnsiOut)
{
    if (fullPath == nullptr || fullPath[0] == 0)
        return false;
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return false; // does not exist - leave the existing error reporting alone
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false; // a directory is listed directly, same as the ANSI path does

    std::wstring directory(fullPath);
    std::wstring name;
    if (!CutDirectoryW(directory, &name) || name.empty())
        return false; // no parent to list ("C:\", "\server\share")

    directoryOut = directory;
    focusNameAnsiOut = PanelAnsiNameFromWideW(name.c_str());
    return true;
}

} // namespace sally
