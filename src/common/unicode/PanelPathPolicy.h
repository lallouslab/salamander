// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/unicode/helpers.h"

#include <string>
#include <utility>
#include <windows.h>

namespace sally::unicode
{
// Returns true when the wide-cache value is populated (non-null and non-empty).
//
// Use this to gate the wide-vs-ANSI dispatch when a panel keeps both a wide
// source-of-truth path/name and a lossy ANSI mirror. Whenever this returns
// true, downstream code must prefer the wide cache; the ANSI cache may
// contain `?`-mangled bytes for non-CP_ACP characters and routing to ANSI
// validators with that buffer will misreport the path as nonexistent.
//
// Centralized here so the single decision can be tested in isolation and
// reused everywhere a "wide cache populated?" question is asked.
inline bool HasWidePathW(const wchar_t* widePath)
{
    return widePath != NULL && widePath[0] != L'\0';
}

// Rebinds an ANSI path that was built on top of a panel's lossy ANSI cache
// onto the panel's wide source-of-truth prefix.
//
// Use case: legacy script builders (BuildScriptMain/Dir/File) thread the
// panel's ANSI `Path` into operation entries' SourceName. For Unicode-only
// roots (e.g. C:\Temp\zz中文) that ANSI cache contains `?`-mangled bytes,
// and the wide mirror SourceNameW that COperations::Add derives from it via
// CP_ACP round-trip is equally lossy. After the script is built, call this
// function once per op to compute a wide path that pins the panel-root
// segment to the real wide cache while preserving the (typically ASCII)
// suffix that lives below the panel root.
//
// Returns the rebound wide path on success, or an empty string when:
//   - any input is null/empty
//   - `ansiPath` does not start with `anchorAnsi` (case-insensitive,
//     trailing-slash tolerant) followed by either end-of-string or a
//     directory separator. (Prevents a sibling like `C:\Temp\zz??-suffix`
//     from being matched against `C:\Temp\zz??`.)
//
// The returned path does not carry the `\\?\` long-path prefix; the caller
// applies that decoration if needed.
inline std::wstring RebindAnsiPathToWideAnchor(const char* ansiPath,
                                               const char* anchorAnsi,
                                               const wchar_t* anchorWide)
{
    if (ansiPath == NULL || ansiPath[0] == '\0')
        return std::wstring();
    if (anchorAnsi == NULL || anchorAnsi[0] == '\0')
        return std::wstring();
    if (anchorWide == NULL || anchorWide[0] == L'\0')
        return std::wstring();

    size_t cmpLen = strlen(anchorAnsi);
    if (cmpLen > 0 && (anchorAnsi[cmpLen - 1] == '\\' || anchorAnsi[cmpLen - 1] == '/'))
        --cmpLen;
    if (cmpLen == 0)
        return std::wstring();

    if (strlen(ansiPath) < cmpLen)
        return std::wstring();
    if (_strnicmp(ansiPath, anchorAnsi, cmpLen) != 0)
        return std::wstring();

    const char nextCh = ansiPath[cmpLen];
    if (nextCh != '\0' && nextCh != '\\' && nextCh != '/')
        return std::wstring();

    std::wstring rebound(anchorWide);
    if (!rebound.empty() && (rebound.back() == L'\\' || rebound.back() == L'/'))
        rebound.pop_back();

    const char* remainder = ansiPath + cmpLen;
    if (*remainder != '\0')
    {
        // The remainder is the portion of the path beyond the panel root;
        // typical content is ASCII filenames / subdirectory names. CP_ACP is
        // sufficient there because the panel root is the only place the
        // ANSI cache mangling could have occurred. (If a Unicode-only
        // segment existed *below* the panel root we would still be lossy
        // here; that path is not yet observed in the wild and would require
        // threading wide names through the script builders end-to-end.)
        std::wstring wideRemainder = AnsiToWide(remainder);
        if (!wideRemainder.empty() && wideRemainder[0] != L'\\' && wideRemainder[0] != L'/')
            rebound.push_back(L'\\');
        rebound += wideRemainder;
    }

    return rebound;
}

inline bool IsPathSeparatorA(char ch)
{
    return ch == '\\' || ch == '/';
}

inline bool IsPathSeparatorW(wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

inline size_t RootLengthA(const char* path, size_t len)
{
    if (path == NULL || len == 0)
        return 0;

    if (len >= 2 && IsPathSeparatorA(path[0]) && IsPathSeparatorA(path[1]))
    {
        size_t pos = 2;
        while (pos < len && !IsPathSeparatorA(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        ++pos;
        while (pos < len && !IsPathSeparatorA(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        return pos + 1;
    }

    if (len >= 2 && path[1] == ':')
    {
        if (len >= 3 && IsPathSeparatorA(path[2]))
            return 3;
        return 2;
    }

    if (IsPathSeparatorA(path[0]))
        return 1;

    return 0;
}

inline size_t RootLengthW(const wchar_t* path, size_t len)
{
    if (path == NULL || len == 0)
        return 0;

    if (len >= 2 && IsPathSeparatorW(path[0]) && IsPathSeparatorW(path[1]))
    {
        size_t pos = 2;
        while (pos < len && !IsPathSeparatorW(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        ++pos;
        while (pos < len && !IsPathSeparatorW(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        return pos + 1;
    }

    if (len >= 2 && path[1] == L':')
    {
        if (len >= 3 && IsPathSeparatorW(path[2]))
            return 3;
        return 2;
    }

    if (IsPathSeparatorW(path[0]))
        return 1;

    return 0;
}

inline size_t TrimTrailingPathSeparatorsA(const char* path, size_t len)
{
    size_t rootLen = RootLengthA(path, len);
    while (len > rootLen && IsPathSeparatorA(path[len - 1]))
        --len;
    return len;
}

inline std::wstring TrimTrailingPathSeparatorsW(std::wstring path)
{
    size_t rootLen = RootLengthW(path.c_str(), path.length());
    while (path.length() > rootLen && IsPathSeparatorW(path.back()))
        path.pop_back();
    return path;
}

inline bool AnsiPathPrefixMatchesAtBoundary(const char* path, size_t pathLen,
                                            const char* prefix, size_t prefixLen)
{
    if (path == NULL || prefix == NULL || prefixLen == 0 || prefixLen > pathLen)
        return false;
    if (_strnicmp(path, prefix, prefixLen) != 0)
        return false;
    if (pathLen == prefixLen)
        return true;
    if (prefixLen == RootLengthA(prefix, prefixLen))
        return true;

    return IsPathSeparatorA(path[prefixLen]);
}

inline int CountAnsiPathComponents(const char* path, size_t len)
{
    int count = 0;
    size_t pos = 0;
    while (pos < len)
    {
        while (pos < len && IsPathSeparatorA(path[pos]))
            ++pos;
        if (pos >= len)
            break;

        ++count;
        while (pos < len && !IsPathSeparatorA(path[pos]))
            ++pos;
    }

    return count;
}

inline bool CutLastWidePathComponent(std::wstring& path)
{
    path = TrimTrailingPathSeparatorsW(path);

    size_t rootLen = RootLengthW(path.c_str(), path.length());
    if (path.length() <= rootLen)
        return false;

    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;

    if (slash + 1 <= rootLen)
        path.resize(rootLen);
    else
        path.resize(slash);

    return true;
}

inline std::wstring AppendAnsiPathSuffixToWideRoot(std::wstring wideRoot, const char* suffix)
{
    std::wstring wideSuffix = AnsiToWide(suffix);
    if (wideSuffix.empty())
        return wideRoot;

    if (!IsPathSeparatorW(wideSuffix[0]) && !wideRoot.empty() && !IsPathSeparatorW(wideRoot.back()))
        wideRoot.push_back(L'\\');
    wideRoot += wideSuffix;
    return wideRoot;
}

inline std::wstring MapRelatedAnsiPathToWidePath(const char* ansiPath,
                                                 const char* anchorAnsi,
                                                 const wchar_t* anchorWide)
{
    if (ansiPath == NULL || ansiPath[0] == '\0')
        return std::wstring();
    if (anchorAnsi == NULL || anchorAnsi[0] == '\0')
        return std::wstring();
    if (anchorWide == NULL || anchorWide[0] == L'\0')
        return std::wstring();

    size_t pathLen = TrimTrailingPathSeparatorsA(ansiPath, strlen(ansiPath));
    size_t anchorLen = TrimTrailingPathSeparatorsA(anchorAnsi, strlen(anchorAnsi));
    if (pathLen == 0 || anchorLen == 0)
        return std::wstring();

    std::wstring wideAnchor = TrimTrailingPathSeparatorsW(std::wstring(anchorWide));
    if (wideAnchor.empty())
        return std::wstring();

    if (AnsiPathPrefixMatchesAtBoundary(ansiPath, pathLen, anchorAnsi, anchorLen))
    {
        if (pathLen == anchorLen)
            return wideAnchor;

        return AppendAnsiPathSuffixToWideRoot(wideAnchor, ansiPath + anchorLen);
    }

    if (AnsiPathPrefixMatchesAtBoundary(anchorAnsi, anchorLen, ansiPath, pathLen))
    {
        int componentsToCut = CountAnsiPathComponents(anchorAnsi + pathLen, anchorLen - pathLen);
        std::wstring mapped = wideAnchor;
        while (componentsToCut-- > 0)
        {
            if (!CutLastWidePathComponent(mapped))
                return std::wstring();
        }
        return mapped;
    }

    return std::wstring();
}

inline std::wstring EffectiveItemNameW(const char* nameA, const wchar_t* nameW)
{
    if (HasWidePathW(nameW))
        return std::wstring(nameW);

    return AnsiToWide(nameA);
}

// The wide-twin pair carried by a deferred history item. Plain data, no
// ownership of plugin FS or icons, so it can be constructed in tests without
// dragging the sally.h object graph (CPathHistory / TIndirectArray /
// CPluginFSInterfaceAbstract) into the test binary.
//
// Used by CPathHistory's deferred-flush block (the unlock path that re-adds
// a parked NewItem after Lock = FALSE). The flush originally re-added the
// item by calling AddPathUnique with only the ANSI fields, silently
// dropping the wide twins. Centralizing the args composition here means
// future refactors can't re-introduce the same loss.
struct DeferredHistoryWideTwins
{
    std::wstring pathOrArchiveOrFSNameW;
    std::wstring archivePathOrFSUserPartW;
};

// Extracts the wide-twin pointers a flush-site should pass to AddPathUnique.
// Returns nullptr for either pointer when the corresponding wide string is
// empty so the AddPathUnique constructor's "ignore empty wide" guard does
// the right thing without the caller having to special-case it (plugin FS
// items today carry empty wide twins; the constructor's guard then leaves
// the item's wide fields empty, matching round-1/2 behavior exactly).
inline void DeferredHistoryWidePointers(const DeferredHistoryWideTwins& twins,
                                        const wchar_t*& outNameW,
                                        const wchar_t*& outUserPartW)
{
    outNameW = twins.pathOrArchiveOrFSNameW.empty() ? nullptr : twins.pathOrArchiveOrFSNameW.c_str();
    outUserPartW = twins.archivePathOrFSUserPartW.empty() ? nullptr : twins.archivePathOrFSUserPartW.c_str();
}

inline bool HasTrailingSlashW(const std::wstring& path)
{
    return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

inline std::wstring BuildPanelChildPathW(const std::wstring& parentPathW,
                                         const char* childNameA,
                                         const wchar_t* childNameW)
{
    std::wstring childName = EffectiveItemNameW(childNameA, childNameW);
    if (parentPathW.empty())
        return childName;
    if (childName.empty())
        return parentPathW;

    std::wstring result = parentPathW;
    if (!HasTrailingSlashW(result))
        result.push_back(L'\\');
    result += childName;
    return result;
}

inline bool TryExactAnsiFallback(const std::wstring& value, std::string& ansi)
{
    ansi.clear();
    if (value.empty())
        return true;

    BOOL usedDefaultChar = FALSE;
    int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                                       NULL, 0, NULL, &usedDefaultChar);
    if (required <= 0 || usedDefaultChar)
        return false;

    std::string converted((size_t)required, '\0');
    usedDefaultChar = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                            converted.data(), required, NULL, &usedDefaultChar) == 0 ||
        usedDefaultChar)
    {
        return false;
    }
    converted.resize((size_t)required - 1);

    int roundTripLen = MultiByteToWideChar(CP_ACP, 0, converted.c_str(), -1, NULL, 0);
    if (roundTripLen <= 0)
        return false;

    std::wstring roundTrip((size_t)roundTripLen, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, converted.c_str(), -1, roundTrip.data(), roundTripLen) == 0)
        return false;
    roundTrip.resize((size_t)roundTripLen - 1);
    if (roundTrip != value)
        return false;

    ansi = std::move(converted);
    return true;
}

inline bool WidePathNeedsExactPreservation(const wchar_t* widePath)
{
    if (!HasWidePathW(widePath))
        return false;

    std::string ansi;
    return !TryExactAnsiFallback(std::wstring(widePath), ansi);
}

inline std::wstring EffectivePanelPathW(const char* ansiPath, const wchar_t* widePath)
{
    if (HasWidePathW(widePath))
        return std::wstring(widePath);

    return AnsiToWide(ansiPath);
}
} // namespace sally::unicode
