// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CBuildConfig — lightweight configuration for standalone script building.
//
// Replaces direct access to the Configuration global and volume detection
// results in BuildScriptMain/Dir/File. Fields here are those read by the
// build-script functions that are NOT already carried by CSelectionSnapshot.
//
// Pure value type — no UI, no global dependencies.

#pragma once

#include <windows.h>

struct CBuildFilterEntry
{
    const char* NameA = nullptr;
    const wchar_t* NameW = nullptr;
    BOOL IsDir = FALSE;
    DWORD Attr = 0;
    unsigned __int64 Size = 0;
    FILETIME LastWrite = {};
};

using CBuildFilterPredicate = BOOL (*)(const CBuildFilterEntry& entry, void* context);

struct CBuildADSProbeResult
{
    BOOL HasADS = FALSE;
    BOOL HasProbeError = FALSE;
    unsigned __int64 Size = 0;
    unsigned __int64 OccupiedSpace = 0;
    DWORD WinError = NO_ERROR;
    BOOL OnlyDiscardableStreams = FALSE;
};

using CBuildADSProbe = BOOL (*)(const char* sourceNameA,
                                const wchar_t* sourceNameW,
                                BOOL isDir,
                                DWORD bytesPerCluster,
                                CBuildADSProbeResult* result,
                                void* context);

struct CBuildConfig
{
    // --- Volume capabilities (detected from source/target paths) ---
    BOOL SourceSupportsADS = FALSE; // source volume supports Alternate Data Streams
    BOOL TargetSupportsADS = FALSE; // target volume supports ADS
    BOOL TargetIsFAT32 = FALSE;     // target is FAT32 (4 GB file-size limit)
    BOOL TargetPathIsEncrypted = FALSE; // target path inherits the Encrypted attribute

    // --- Configuration.* fields used by BuildScriptMain/Dir/File ---

    // Enable the recursive directory tranche in the snapshot builder. Kept
    // opt-in so existing directory rejection tests and production fallback
    // remain explicit until the gate chooses the wider surface.
    BOOL EnableRecursiveDirectories = FALSE;

    // Enable copy/move filters through a small callback so common/ does not
    // depend on panel UI types. The callback is applied to files, matching the
    // legacy recursive builder's filter point.
    BOOL EnableFilters = FALSE;
    BOOL SkipEmptyDirs = FALSE;
    CBuildFilterPredicate FilterPredicate = nullptr;
    void* FilterContext = nullptr;

    // Enable ADS copy accounting when both source and target support ADS.
    // Prompt-heavy loss/error cases are still rejected by the production gate.
    BOOL EnableADS = FALSE;
    BOOL IgnoreADS = FALSE;
    CBuildADSProbe ADSProbe = nullptr;
    void* ADSProbeContext = nullptr;

    // Enable explicit per-item target names for rename masks / copy-of style
    // mapping. The builder never invents mapped names implicitly.
    BOOL EnableExplicitTargetNames = FALSE;

    // Clear read-only attribute when copying from CD/CDFS media.
    // Read in BuildScriptMain (line ~1203) from Configuration.ClearReadOnly.
    BOOL ClearReadOnly = FALSE;

    // Allow fast directory move on Novell NetWare volumes.
    // Read in BuildScriptMain (line ~1199) from Configuration.NetwareFastDirMove.
    BOOL NetwareFastDirMove = TRUE;

    // Confirm deletion of system/hidden directories.
    // Read in BuildScriptDir (line ~1654) from Configuration.CnfrmSHDirDel.
    BOOL ConfirmDeleteSystemHiddenDir = TRUE;

    // Confirm deletion of non-empty directories.
    // Read in BuildScriptDir (line ~2101) from Configuration.CnfrmNEDirDel.
    BOOL ConfirmDeleteNonEmptyDir = TRUE;
};
