// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide display/validation name helpers (pure string manipulation, no I/O).
// Standalone-compilable: consumed by production (declarations also appear in
// consts.h; the compiler cross-checks in TUs that see both) and directly by
// the private tests (kb/unicode/test-map.md).

#pragma once

#include <windows.h>
#include <string>

// Returns path with trailing backslash added if needed
// (to prevent Windows from trimming trailing spaces/dots).
std::wstring MakeCopyWithBackslashIfNeededW(const wchar_t* name);

// Wide version of NameEndsWithBackslash.
BOOL NameEndsWithBackslashW(const wchar_t* name);

// Checks if path contains components ending with space or dot.
// Returns FALSE if an invalid component is found.
BOOL PathContainsValidComponentsW(const wchar_t* path);

// Wide version of AlterFileName - returns formatted filename.
// format: 0 none, 1 capitalize, 2 lower, 3 upper, 5 explorer style,
//         6 VC style (3 for dirs, 2 for files), 7 name mixed + ext lower.
// change: 0 both, 1 name only, 2 extension only.
std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir);
