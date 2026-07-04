// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// SalGetFullNameW + SalRemovePointsFromPath (see SalGetFullName.cpp).
// Declarations also appear in consts.h; the compiler cross-checks in TUs that
// see both.

#pragma once

#include <windows.h>
#include <string>

// Eliminates '.' and '..' components in the path after the root.
BOOL SalRemovePointsFromPath(char* afterRoot);
BOOL SalRemovePointsFromPath(WCHAR* afterRoot);

// Wide path resolution: trims, resolves relative/drive-relative forms against
// curDir / the per-drive default dirs, removes dots, normalizes backslashes.
BOOL SalGetFullNameW(std::wstring& name, int* errTextID = NULL, const wchar_t* curDir = NULL,
                     std::wstring* nextFocus = NULL, BOOL* callNethood = NULL,
                     BOOL allowRelPathWithSpaces = FALSE);

// HOST SEAM: returns the remembered default directory for drive
// 'a'..'z' (lower-case letter). Production implements this over the
// DefaultDir table (sally_path_utils.cpp); tests provide a stub
// (tests/sally/src/tests/test_defaultdir_stub.cpp).
const char* SalGetDefaultDirForDrive(wchar_t lowerDriveLetter);
