// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide path-string helpers (pure string manipulation, no I/O). Standalone-
// compilable: consumed by production (via consts.h) and directly by the
// private tests (kb/unicode/test-map.md).

#pragma once

#include <windows.h>
#include <string>

// Wide version - appends name to path (modifies path in-place).
// Handles leading/trailing backslashes properly.
void SalPathAppendW(std::wstring& path, const wchar_t* name);
// Raw-buffer overload for in-place path manipulation.
BOOL SalPathAppendW(wchar_t* path, const wchar_t* name, int pathSize);

// Wide version - ensures path ends with backslash.
void SalPathAddBackslashW(std::wstring& path);
// Raw-buffer overload for in-place path manipulation.
BOOL SalPathAddBackslashW(wchar_t* path, int pathSize);

// Wide version - removes trailing backslash.
void SalPathRemoveBackslashW(std::wstring& path);
// Raw-buffer overload for in-place path manipulation.
void SalPathRemoveBackslashW(wchar_t* path);

// Wide version - strips path leaving just filename.
// "C:\foo\bar.txt" -> "bar.txt", "bar.txt" -> "bar.txt"
void SalPathStripPathW(std::wstring& path);

// Wide version - finds filename portion of path.
// Returns pointer within the string to the filename part.
const wchar_t* SalPathFindFileNameW(const wchar_t* path);

// Wide version - removes extension from path.
void SalPathRemoveExtensionW(std::wstring& path);

// Wide version - adds extension if not already present.
// Returns true if extension was added or already exists.
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension);

// Wide version - replaces extension (or adds if none).
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension);

// Trims leading/trailing whitespace (chars <= ' ') in place.
// Returns TRUE if the string changed.
BOOL CutSpacesFromBothSidesW(wchar_t* path);

// Wide version - cuts last directory from path.
// Returns false if path cannot be shortened (e.g., "C:\" or "\\server\share").
// If cutDir is provided, it receives the cut directory name.
bool CutDirectoryW(std::wstring& path, std::wstring* cutDir = nullptr);
