// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide path-string helpers. Extracted from sally_path_utils.cpp and
// sally_entry_lifecycle.cpp so production and the private tests compile the
// same translation unit (kb/unicode/test-map.md).

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <string>

#include "common/SalPathWide.h"

// Wide version - appends name to path (modifies path in-place)
// Handles leading/trailing backslashes properly
void SalPathAppendW(std::wstring& path, const wchar_t* name)
{
    if (name == nullptr)
        return;

    // Skip leading backslash in name
    if (*name == L'\\')
        name++;

    // Remove trailing backslash from path
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();

    // Append name if non-empty
    if (*name != L'\0')
    {
        if (!path.empty())
            path += L'\\';
        path += name;
    }
}

// Raw-buffer overload for in-place path manipulation
BOOL SalPathAppendW(wchar_t* path, const wchar_t* name, int pathSize)
{
    if (name == NULL)
        return TRUE;
    int l1 = (int)wcslen(path);
    int l2 = (int)wcslen(name);
    if (l1 > 0 && path[l1 - 1] != L'\\')
    {
        if (l1 + 1 + l2 + 1 > pathSize)
            return FALSE;
        path[l1++] = L'\\';
    }
    else
    {
        if (l1 + l2 + 1 > pathSize)
            return FALSE;
    }
    memmove(path + l1, name, (l2 + 1) * sizeof(wchar_t));
    return TRUE;
}

// Wide version - ensures path ends with backslash
void SalPathAddBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
}

// Raw-buffer overload for in-place path manipulation
BOOL SalPathAddBackslashW(wchar_t* path, int pathSize)
{
    int l = (int)wcslen(path);
    if (l > 0 && path[l - 1] != L'\\')
    {
        if (l + 2 > pathSize)
            return FALSE;
        path[l] = L'\\';
        path[l + 1] = 0;
    }
    return TRUE;
}

// Wide version - removes trailing backslash
void SalPathRemoveBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
}

// Raw-buffer overload for in-place path manipulation
void SalPathRemoveBackslashW(wchar_t* path)
{
    int l = (int)wcslen(path);
    if (l > 0 && path[l - 1] == L'\\')
        path[l - 1] = 0;
}

// Wide version - strips path leaving just filename
// "C:\foo\bar.txt" -> "bar.txt", "bar.txt" -> "bar.txt"
void SalPathStripPathW(std::wstring& path)
{
    size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(pos + 1);
}

// Wide version - finds filename portion of path
// Returns pointer within the string to the filename part
const wchar_t* SalPathFindFileNameW(const wchar_t* path)
{
    if (path == nullptr)
        return nullptr;

    const wchar_t* result = path;
    for (const wchar_t* p = path; *p != L'\0'; p++)
    {
        if (*p == L'\\')
            result = p + 1;
    }
    return result;
}

// Wide version - removes extension from path
// "C:\foo\bar.txt" -> "C:\foo\bar"
void SalPathRemoveExtensionW(std::wstring& path)
{
    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            return;
        }
        if (path[i - 1] == L'\\')
            return; // No extension found
    }
}

// Wide version - adds extension if not already present
// Returns true if extension was added or already exists
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
            return true; // Extension already exists
        if (path[i - 1] == L'\\')
            break; // No extension, add it
    }
    path += extension;
    return true;
}

// Wide version - replaces extension (or adds if none)
// "C:\foo\bar.txt" + ".bak" -> "C:\foo\bar.bak"
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (extension == nullptr)
        return false;

    size_t len = path.length();
    for (size_t i = len; i > 0; i--)
    {
        if (path[i - 1] == L'.')
        {
            path.resize(i - 1);
            break;
        }
        if (path[i - 1] == L'\\')
            break; // No existing extension
    }
    path += extension;
    return true;
}

// Trims leading/trailing whitespace (chars <= ' ') in place.
// Returns TRUE if the string changed.
BOOL CutSpacesFromBothSidesW(wchar_t* path)
{
    BOOL ch = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        ch = TRUE;
    }
    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' '))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

// Wide version - cuts last directory from path
// Returns false if path cannot be shortened (e.g., "C:\" or "\\server\share")
// If cutDir is provided, it receives the cut directory name
bool CutDirectoryW(std::wstring& path, std::wstring* cutDir)
{
    if (path.empty())
    {
        if (cutDir)
            cutDir->clear();
        return false;
    }

    // Remove trailing backslash for processing
    size_t len = path.length();
    if (len > 0 && path[len - 1] == L'\\')
        len--;

    // Find last backslash
    size_t lastBS = path.rfind(L'\\', len - 1);
    if (lastBS == std::wstring::npos)
    {
        if (cutDir)
            cutDir->clear();
        return false; // No backslash found
    }

    // Find second-to-last backslash
    size_t prevBS = (lastBS > 0) ? path.rfind(L'\\', lastBS - 1) : std::wstring::npos;

    // Check for root path cases
    if (prevBS == std::wstring::npos)
    {
        // "C:\somedir" case - cut to "C:\"
        if (cutDir)
            *cutDir = path.substr(lastBS + 1, len - lastBS - 1);
        path.resize(lastBS + 1); // Keep the backslash: "C:\"
        return true;
    }

    // Check for UNC root "\\server\share"
    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\' && prevBS <= 2)
    {
        if (cutDir)
            cutDir->clear();
        return false; // Cannot shorten UNC root
    }

    // Normal case: "C:\dir1\dir2" -> "C:\dir1"
    if (cutDir)
        *cutDir = path.substr(lastBS + 1, len - lastBS - 1);
    path.resize(lastBS);
    return true;
}
