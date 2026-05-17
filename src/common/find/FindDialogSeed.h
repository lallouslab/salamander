// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/unicode/helpers.h"

#include <string>
#include <vector>
#include <windows.h>

namespace sally::find
{
// The Find dialog's "Look in" seed.
//
// Built from the panel that opens the dialog; consumed by CFindDialog at
// WM_INITDIALOG. The dialog's existing data flow seeds the visible "Look in"
// combo via Transfer(ttDataToWindow), which uses ANSI WM_SETTEXT — so a
// Unicode-only panel root (e.g. C:\Temp\zz中文) renders as zz?? in the
// edit field. The seed carries both mirrors so the dialog can override the
// ANSI render with SetDlgItemTextW *after* Transfer runs, but only when
// the override is actually needed (the ASCII-only common case round-trips
// cleanly through CP_ACP and skips the override entirely).
struct LookInSeed
{
    std::string ansi;   // legacy mirror; what Transfer(ttDataToWindow) seeds
    std::wstring wide;  // wide source-of-truth; what SetDlgItemTextW gets
};

// Build a seed from the panel's caches.
//
// If `widePath` is non-null and non-empty, `wide` is the wide cache verbatim
// and `ansi` is the ANSI cache as-is (lossy for non-CP_ACP roots — that's
// fine, the wide member is what survives). If `widePath` is null/empty,
// `wide` is AnsiToWide(ansiPath) so callers without a wide cache (plugin FS
// today) still get a usable wide field.
inline LookInSeed BuildLookInSeed(const char* ansiPath, const wchar_t* widePath)
{
    LookInSeed seed;
    seed.ansi = ansiPath != nullptr ? ansiPath : "";
    if (widePath != nullptr && widePath[0] != L'\0')
        seed.wide = widePath;
    else
        seed.wide = AnsiToWide(ansiPath != nullptr ? ansiPath : "");
    return seed;
}

// Returns true when the dialog should override the Transfer-populated ANSI
// edit text with a SetDlgItemTextW call after Transfer runs.
//
// The override only fires when the wide seed genuinely carries non-ASCII
// bytes that CP_ACP can't represent. In the common ASCII-only case the
// override is skipped — the ANSI Transfer already produced a faithful
// render and reissuing the same text via SetDlgItemTextW would be busy
// work. Centralizing the decision here means the dialog stays a thin
// presenter; the seed-vs-override question is unit-testable without
// instantiating any Win32 dialog.
inline bool ShouldOverrideEditWithWide(const LookInSeed& seed)
{
    if (seed.wide.empty())
        return false;
    // Round-trip the wide through CP_ACP; if the result matches the wide
    // seed, the ANSI render Transfer produced is already faithful.
    std::string ansiRoundTrip = WideToAnsi(seed.wide);
    std::wstring backToWide = AnsiToWide(ansiRoundTrip.c_str());
    return backToWide != seed.wide;
}

// Returns true when the constructor-time panel seed is still the active
// "Look in" value and should be promoted to the Unicode combo.
//
// AutoLoad and saved Find presets can replace Data.LookInText before the
// delayed override runs. In that case the preset is the user's explicit
// search root, so the panel seed must stay out of the way.
inline bool ShouldApplyInitialLookInWideOverride(const LookInSeed& seed, const char* currentAnsiLookIn)
{
    const char* current = currentAnsiLookIn != nullptr ? currentAnsiLookIn : "";
    return seed.ansi == current && ShouldOverrideEditWithWide(seed);
}

inline void TrimLookInPathW(std::wstring& path)
{
    size_t first = 0;
    while (first < path.length() && path[first] <= L' ')
        first++;

    size_t last = path.length();
    while (last > first && path[last - 1] <= L' ')
        last--;

    if (first != 0 || last != path.length())
        path = path.substr(first, last - first);
}

inline void KeepSingleTrailingSeparatorW(std::wstring& path)
{
    if (path.empty())
        return;

    size_t end = path.length();
    while (end > 1 && (path[end - 1] == L'\\' || path[end - 1] == L'/') &&
           (path[end - 2] == L'\\' || path[end - 2] == L'/'))
    {
        end--;
    }
    path.resize(end);
}

inline std::vector<std::wstring> SplitLookInPathsW(const std::wstring& text)
{
    std::vector<std::wstring> paths;
    std::wstring current;

    for (size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == L';')
        {
            if (i + 1 < text.length() && text[i + 1] == L';')
            {
                current.push_back(L';');
                i++;
            }
            else
            {
                TrimLookInPathW(current);
                KeepSingleTrailingSeparatorW(current);
                if (!current.empty())
                    paths.push_back(current);
                current.clear();
            }
        }
        else
            current.push_back(text[i]);
    }

    TrimLookInPathW(current);
    KeepSingleTrailingSeparatorW(current);
    if (!current.empty())
        paths.push_back(current);

    return paths;
}
} // namespace sally::find
