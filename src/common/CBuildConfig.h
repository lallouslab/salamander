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

// Build-time delete confirmation (P4). Lets the snapshot builder handle the
// prompt-sensitive delete cases the legacy builder used to own, without
// common/ depending on gPrompter: production wires the callback to gPrompter,
// tests script it. Default null → the builder rejects those cases (production
// gate unchanged, legacy handles the prompt).
enum class CBuildDeletePromptKind
{
    NonEmptyDir,     // deleting a non-empty directory (CnfrmNEDirDel)
    SystemHiddenDir, // deleting a system/hidden directory (CnfrmSHDirDel)
};

enum class CBuildDeletePromptResult
{
    Proceed, // build the delete
    Skip,    // skip this directory (do not delete it or its parent)
    Cancel,  // abort the whole build
};

using CBuildDeletePrompt = CBuildDeletePromptResult (*)(CBuildDeletePromptKind kind,
                                                        const char* dirNameA,
                                                        const wchar_t* dirNameW,
                                                        void* context);

// Build-time ADS-loss confirmation (P5). When a source carries Alternate Data
// Streams the target volume cannot hold, the legacy builder asked "ADS will be
// lost — continue?". This callback lets the snapshot builder ask instead of
// rejecting to legacy. Default null → reject (production gate unchanged). Fires
// only for pure ADS loss; probe errors always reject (an error is not a loss).
enum class CBuildAdsLossPromptResult
{
    Proceed, // build the op, silently dropping the streams
    Reject,  // abort the build (fall back to legacy)
};

using CBuildAdsLossPrompt = CBuildAdsLossPromptResult (*)(const char* sourceNameA,
                                                          const wchar_t* sourceNameW,
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

    // Master opt-in for snapshot-builder fast-dir-move absorption (P3). Default
    // OFF so the production gate is unchanged (fast moves still route to legacy
    // via the wouldUseFastDirMove bypass) until a direct parity test + manual
    // validation + release soak. When ON, a same-root move of a whole directory
    // into a not-yet-existing target emits a single ocMoveDir op.
    BOOL EnableFastDirMove = FALSE;

    // Confirm deletion of system/hidden directories.
    // Read in BuildScriptDir (line ~1654) from Configuration.CnfrmSHDirDel.
    BOOL ConfirmDeleteSystemHiddenDir = TRUE;

    // Confirm deletion of non-empty directories.
    // Read in BuildScriptDir (line ~2101) from Configuration.CnfrmNEDirDel.
    BOOL ConfirmDeleteNonEmptyDir = TRUE;

    // Build-time delete confirmation callback (P4). When set, the builder asks
    // instead of rejecting the prompt-sensitive delete case. Default null keeps
    // the reject-to-legacy behavior (production gate unchanged).
    CBuildDeletePrompt DeletePromptCallback = nullptr;
    void* DeletePromptContext = nullptr;

    // Build-time ADS-loss confirmation callback (P5). When set, the builder asks
    // instead of rejecting a source whose ADS the target cannot hold. Default
    // null keeps reject-to-legacy (production gate unchanged).
    CBuildAdsLossPrompt AdsLossPromptCallback = nullptr;
    void* AdsLossPromptContext = nullptr;

    // Change-attributes tranche (P5). Opt-in; DEFAULT OFF so the production gate
    // still routes ChangeAttrs to legacy. When on, the builder emits ocChangeAttrs
    // ops. The new attributes are (sourceAttr & ChangeAttrsAnd) | ChangeAttrsOr.
    BOOL EnableChangeAttrs = FALSE;
    DWORD ChangeAttrsAnd = 0xFFFFFFFF;
    DWORD ChangeAttrsOr = 0;
    BOOL ChangeAttrsCompression = FALSE;
    BOOL ChangeAttrsEncryption = FALSE;
    BOOL ChangeAttrsSubDirs = FALSE; // recurse into directory contents

    // Change-case tranche (P5). Opt-in; DEFAULT OFF so the production gate still
    // routes ChangeCase to legacy. When on, the builder emits a rename op whose
    // target leaf is AlterFileNameW(sourceLeaf, format, change). Format/change
    // codes are documented in PathDisplayUtils.h.
    BOOL EnableChangeCase = FALSE;
    int ChangeCaseFormat = 0;
    int ChangeCaseChange = 0;
    BOOL ChangeCaseSubDirs = FALSE; // recurse into directory contents

    // Reparse-point delete absorption (P5). Opt-in; DEFAULT OFF so reparse dirs
    // still reject to legacy. When on, a delete of a reparse-point directory
    // (junction/symlink) emits a single ocDeleteDirLink op that removes the LINK
    // only — the builder never recurses into the link target (data-loss safe).
    BOOL EnableReparseDelete = FALSE;
};
