// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// BuildScript.cpp — standalone COperations script builder from CSelectionSnapshot.
// See BuildScript.h for interface documentation.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <algorithm>
#include <vector>

#include "worker.h"
#include "common/BuildScript.h"
#include "common/IFileEnumerator.h"
#include "common/IFileSystem.h"
#include "common/PathDisplayUtils.h"
#include "common/SnapshotOperationPlanner.h"
#include "common/unicode/helpers.h"

namespace opplan = sally::operation_planner;

// Helper: allocate a full path string "dir\name" (malloc'd, caller owns).
// Returns NULL on failure.
static char* AllocFullPath(const char* dir, const char* name)
{
    int l1 = (int)strlen(dir);
    int l2 = (int)strlen(name);
    int needSep = (l1 > 0 && dir[l1 - 1] != '\\') ? 1 : 0;
    int len = l1 + needSep + l2;
    char* buf = (char*)malloc(len + 1);
    if (buf == NULL)
        return NULL;
    memcpy(buf, dir, l1);
    if (needSep)
        buf[l1] = '\\';
    memcpy(buf + l1 + needSep, name, l2 + 1);
    return buf;
}

static char* DupAnsiString(const char* text)
{
    if (text == NULL)
        return NULL;

    size_t len = strlen(text);
    char* buf = (char*)malloc(len + 1);
    if (buf == NULL)
        return NULL;
    memcpy(buf, text, len + 1);
    return buf;
}

// Root of a wide path for same-disk comparison: "X:" for drive paths,
// "\\server\share" for UNC, else empty (never matches).
static std::wstring PathRootW(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':')
        return std::wstring(1, (wchar_t)towlower(path[0])) + L":";
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        size_t p = path.find(L'\\', 2);       // end of server
        if (p != std::wstring::npos)
            p = path.find(L'\\', p + 1);      // end of share
        std::wstring root = (p == std::wstring::npos) ? path : path.substr(0, p);
        for (wchar_t& c : root)
            c = (wchar_t)towlower(c);
        return root;
    }
    return std::wstring();
}

static bool SameRootPathW(const std::wstring& a, const std::wstring& b)
{
    std::wstring ra = PathRootW(a);
    return !ra.empty() && ra == PathRootW(b);
}

static bool FastMoveTargetDirExists(const std::wstring& targetDirW)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    return fs != nullptr && fs->DirectoryExists(targetDirW.c_str());
}

static bool HasTrailingSlashW(const std::wstring& path)
{
    return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

// \\?\ decoration unified in common/unicode/helpers.h (Phase 0-c).
using sally::unicode::MakeLongPathSafeW;

static bool IsDotDirectory(const wchar_t* name)
{
    return name != NULL &&
           name[0] == L'.' &&
           (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

static bool HasUnsupportedAttributes(const CSnapshotItem& item)
{
    return (item.Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static bool HasUnsupportedAttributes(DWORD attr)
{
    return (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static bool ShouldPromptForSystemHiddenDelete(EActionType action,
                                              const CBuildConfig& config,
                                              DWORD attr)
{
    return action == EActionType::Delete &&
           config.ConfirmDeleteSystemHiddenDir &&
           (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

struct ADSProbeResult
{
    bool HasADS = false;
    bool HasProbeError = false;
    CQuadWord Size;
    CQuadWord OccupiedSpace;
};

static ADSProbeResult ProbeSourceADS(const std::string& sourceA,
                                     const std::wstring& sourceW,
                                     BOOL isDir,
                                     const CBuildConfig& config,
                                     COperations* script)
{
    ADSProbeResult result;
    if (!config.SourceSupportsADS || config.IgnoreADS)
        return result;
    if (config.ADSProbe == nullptr)
    {
        result.HasProbeError = true;
        return result;
    }

    CBuildADSProbeResult probe = {};
    const DWORD bytesPerCluster = script != NULL ? script->BytesPerCluster : 0;
    if (!config.ADSProbe(sourceA.c_str(), sourceW.c_str(), isDir, bytesPerCluster,
                         &probe, config.ADSProbeContext))
    {
        result.HasProbeError = true;
        return result;
    }
    result.HasProbeError = probe.HasProbeError || probe.WinError != NO_ERROR;
    result.HasADS = probe.HasADS && !result.HasProbeError;
    if (result.HasADS)
    {
        result.Size.SetUI64(probe.Size);
        result.OccupiedSpace.SetUI64(probe.OccupiedSpace);
    }

    return result;
}

static bool ConfigureADSForOperation(const std::string& sourceA,
                                     const std::wstring& sourceW,
                                     BOOL isDir,
                                     const CBuildConfig& config,
                                     COperations* script,
                                     COperation& op)
{
    ADSProbeResult ads = ProbeSourceADS(sourceA, sourceW, isDir, config, script);
    if (ads.HasProbeError)
        return false;
    if (!ads.HasADS)
        return true;
    if (!config.EnableADS || !config.TargetSupportsADS)
    {
        // The target cannot hold the streams. Without a prompt callback, reject
        // to legacy; with one, ask — proceeding drops the streams (P5).
        if (config.AdsLossPromptCallback == nullptr)
            return false;
        CBuildAdsLossPromptResult r = config.AdsLossPromptCallback(
            sourceA.c_str(), sourceW.c_str(), config.AdsLossPromptContext);
        if (r != CBuildAdsLossPromptResult::Proceed)
            return false;
        return true; // op built WITHOUT OPFL_COPY_ADS — streams intentionally lost
    }

    op.OpFlags |= OPFL_COPY_ADS;
    op.Size += ads.Size;
    if (script != NULL)
    {
        script->TotalFileSize += ads.Size;
        script->OccupiedSpace += ads.OccupiedSpace;
    }
    return true;
}

// Feasibility (no prompt): does the ADS situation force a reject of the snapshot
// path? Preserves the exact legacy gate when no callback is set; only pure ADS
// loss (not a probe error) becomes feasible when an AdsLossPromptCallback exists
// — the actual prompt then fires in the build pass (ConfigureADSForOperation).
static bool ADSForcesReject(const std::string& sourceA,
                            const std::wstring& sourceW,
                            BOOL isDir,
                            const CBuildConfig& config,
                            COperations* script)
{
    ADSProbeResult ads = ProbeSourceADS(sourceA, sourceW, isDir, config, script);
    const bool legacyReject = (ads.HasADS || ads.HasProbeError) &&
                              (!config.EnableADS || !config.TargetSupportsADS);
    if (!legacyReject)
        return false;
    if (config.AdsLossPromptCallback != nullptr && ads.HasADS && !ads.HasProbeError)
        return false; // ADS loss is authorizable via the prompt
    return true;
}

struct DirectoryEntry
{
    std::string NameA;
    std::wstring NameW;
    DWORD Attr;
    unsigned __int64 Size;
    FILETIME LastWrite;
    bool IsDir;
};

static bool FilterAcceptsFile(const CBuildConfig& config,
                              const std::string& nameA,
                              const std::wstring& nameW,
                              DWORD attr,
                              unsigned __int64 size,
                              FILETIME lastWrite)
{
    if (!config.EnableFilters || config.FilterPredicate == nullptr)
        return true;

    CBuildFilterEntry entry;
    entry.NameA = nameA.c_str();
    entry.NameW = nameW.c_str();
    entry.IsDir = FALSE;
    entry.Attr = attr;
    entry.Size = size;
    entry.LastWrite = lastWrite;
    return config.FilterPredicate(entry, config.FilterContext) != FALSE;
}

static bool EnumerateDirectoryEntries(const std::wstring& dirW,
                                      std::vector<DirectoryEntry>& entries)
{
    // Interface-mediated (Axis D): the enumerator adds the "\*" pattern and the
    // \\?\ long-path decoration internally, so this loop stays Win32-free and
    // is drivable by a MockFileEnumerator in tests.
    IFileEnumerator* fenum =
        gFileEnumerator != nullptr ? gFileEnumerator : GetWin32FileEnumerator();
    if (fenum == nullptr)
        return false;

    HENUM h = fenum->StartEnum(dirW.c_str(), nullptr);
    if (h == INVALID_HENUM)
    {
        // ERROR_PATH_NOT_FOUND means the directory itself is gone — that must
        // fail the build (emitting ops for a vanished source would create an
        // empty target and fail later in the worker), not read as "empty".
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES;
    }

    FileEnumEntry fe;
    for (;;)
    {
        EnumResult r = fenum->NextFile(h, fe);
        if (r.noMoreFiles)
            break;
        if (!r.success)
        {
            fenum->EndEnum(h);
            return false;
        }
        if (fe.name.empty() || IsDotDirectory(fe.name.c_str()))
            continue;

        DirectoryEntry entry = {};
        entry.NameW = fe.name;
        entry.NameA = WideToAnsi(entry.NameW);
        entry.Attr = fe.attributes;
        entry.Size = fe.size;
        entry.LastWrite = fe.lastWriteTime;
        entry.IsDir = fe.IsDirectory();
        entries.push_back(entry);
    }
    fenum->EndEnum(h);

    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& left, const DirectoryEntry& right) {
                  return _wcsicmp(left.NameW.c_str(), right.NameW.c_str()) < 0;
              });
    return true;
}

static bool AddOperation(COperations* script, COperation& op)
{
    script->Add(op);
    return script->IsGood() != FALSE;
}

static DWORD TargetEncryptionFlag(DWORD sourceAttr,
                                   const CBuildConfig& config,
                                   COperations* script)
{
    if (script != NULL && !script->CopyAttrs &&
        ((sourceAttr & FILE_ATTRIBUTE_ENCRYPTED) != 0 ||
         config.TargetPathIsEncrypted))
    {
        return OPFL_AS_ENCRYPTED;
    }
    return 0;
}

static CQuadWord ClusterRoundedSize(const CQuadWord& fileSize, DWORD bytesPerCluster)
{
    if (bytesPerCluster == 0 || fileSize == CQuadWord(0, 0))
        return CQuadWord(0, 0);

    const CQuadWord cluster(bytesPerCluster, 0);
    return fileSize - ((fileSize - CQuadWord(1, 0)) % cluster) + CQuadWord(bytesPerCluster - 1, 0);
}

static bool MoveNeedsCopyAccounting(const COperation& op, const COperations* script)
{
    return (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 ||
           (script != NULL && script->SameRootButDiffVolume) ||
           !op.HasSameRootPath();
}

static bool AddFileOperation(EActionType action,
                             const std::string& sourceParentA,
                             const std::wstring& sourceParentW,
                             const std::string& targetParentA,
                             const std::wstring& targetParentW,
                             const std::string& itemNameA,
                             const std::wstring& itemNameW,
                             const std::string& targetNameA,
                             const std::wstring& targetNameW,
                             unsigned __int64 size,
                             DWORD attr,
                             FILETIME lastWrite,
                             const CBuildConfig& config,
                             COperations* script)
{
    COperation op;

    if (!FilterAcceptsFile(config, itemNameA, itemNameW, attr, size, lastWrite))
        return true;

    if (action == EActionType::ChangeCase)
    {
        // Change-case is a rename: the target leaf is the source leaf with
        // AlterFileNameW applied (P5). Same-name results are a no-op skip.
        const std::wstring alteredW =
            AlterFileNameW(itemNameW.c_str(), config.ChangeCaseFormat,
                           config.ChangeCaseChange, false);
        if (alteredW.empty() || alteredW == itemNameW)
            return true; // no rename needed — not an error
        const std::string alteredA = WideToAnsi(alteredW);

        op.Opcode = ocMoveFile;
        op.OpFlags = 0;
        op.Size = MOVE_FILE_SIZE;
        op.Attr = attr;
        op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
        if (op.SourceName == NULL)
            return false;
        op.TargetName = AllocFullPath(sourceParentA.c_str(), alteredA.c_str());
        if (op.TargetName == NULL)
        {
            free(op.SourceName);
            op.SourceName = NULL;
            return false;
        }
        op.SetSourceNameW(sourceParentW, itemNameW);
        op.SetTargetNameW(sourceParentW, alteredW);
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::ChangeAttrs)
    {
        // ocChangeAttrs repurposes TargetName as the computed NEW attributes
        // (not a path) — matching the legacy builder (copy_move.cpp:2855/3941).
        op.Opcode = ocChangeAttrs;
        op.OpFlags = 0;
        op.Attr = attr;
        CQuadWord fileSize((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));
        op.Size = (config.ChangeAttrsCompression || config.ChangeAttrsEncryption)
                      ? (fileSize >= COMPRESS_ENCRYPT_MIN_FILE_SIZE ? fileSize : COMPRESS_ENCRYPT_MIN_FILE_SIZE)
                      : CHATTRS_FILE_SIZE;
        op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
        if (op.SourceName == NULL)
            return false;
        op.SetSourceNameW(sourceParentW, itemNameW);
        op.TargetName = (char*)(DWORD_PTR)((attr & config.ChangeAttrsAnd) | config.ChangeAttrsOr);
        op.OwnsTargetName = false; // TargetName stores attributes, not a pointer
        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::Delete)
    {
        op.Opcode = ocDeleteFile;
        op.OpFlags = 0;
        op.Size = DELETE_FILE_SIZE;
        op.Attr = attr;
        op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
        if (op.SourceName == NULL)
            return false;
        op.TargetName = NULL;
        op.SetSourceNameW(sourceParentW, itemNameW);

        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::Convert || action == EActionType::RecursiveConvert)
    {
        op.Opcode = ocConvert;
        op.OpFlags = 0;
        op.Attr = attr;
        op.FileSize = CQuadWord((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));
        op.Size = op.FileSize >= CONVERT_MIN_FILE_SIZE ? op.FileSize : CONVERT_MIN_FILE_SIZE;
        op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
        if (op.SourceName == NULL)
            return false;
        op.TargetName = NULL;
        op.SetSourceNameW(sourceParentW, itemNameW);

        script->FilesCount++;
        return AddOperation(script, op);
    }

    COperationCode fileOp = (action == EActionType::Copy) ? ocCopyFile : ocMoveFile;
    op.Opcode = fileOp;
    op.OpFlags = TargetEncryptionFlag(attr, config, script);
    if (action == EActionType::Move && (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 &&
        script != NULL && !script->ShowStatus)
    {
        script->ShowStatus = TRUE;
    }
    op.Attr = attr;
    op.FileSize = CQuadWord((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));

    CQuadWord fileSizeLoc = op.FileSize;
    op.Size = fileSizeLoc >= COPY_MIN_FILE_SIZE ? fileSizeLoc : COPY_MIN_FILE_SIZE;

    const std::string sourceFullA = opplan::JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceFullW = opplan::JoinPathW(sourceParentW, itemNameW);

    op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
    if (op.SourceName == NULL)
        return false;
    op.TargetName = AllocFullPath(targetParentA.c_str(), targetNameA.c_str());
    if (op.TargetName == NULL)
        return false;
    op.SetSourceNameW(sourceParentW, itemNameW);
    op.SetTargetNameW(targetParentW, targetNameW);

    if (action == EActionType::Copy && op.AreSourceAndTargetSamePath())
        return false;

    if (action == EActionType::Move && (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 &&
        (attr & FILE_ATTRIBUTE_ENCRYPTED) != 0 &&
        script != NULL && !script->SameRootButDiffVolume && op.HasSameRootPath())
    {
        op.OpFlags &= ~OPFL_AS_ENCRYPTED;
    }

    const bool copyLikeOperation = action == EActionType::Copy || MoveNeedsCopyAccounting(op, script);
    if (copyLikeOperation &&
        !ConfigureADSForOperation(sourceFullA, sourceFullW, FALSE, config, script, op))
    {
        return false;
    }

    script->FilesCount++;
    if (copyLikeOperation)
    {
        script->TotalFileSize += fileSizeLoc;
        script->OccupiedSpace += ClusterRoundedSize(fileSizeLoc, script->BytesPerCluster);
    }
    else
    {
        op.Size = MOVE_FILE_SIZE;
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
    }
    return AddOperation(script, op);
}

static bool AddPlannedFileOperation(const opplan::CPlannedFileOperation& plan,
                                    const CBuildConfig& config,
                                    COperations* script)
{
    if (plan.IsDir)
        return false;

    return AddFileOperation(plan.Action,
                            plan.SourceParentA, plan.SourceParentW,
                            plan.TargetParentA, plan.TargetParentW,
                            plan.ItemNameA, plan.ItemNameW,
                            plan.TargetNameA, plan.TargetNameW,
                            plan.Size, plan.Attr, plan.LastWrite,
                            config, script);
}

static bool AddDirectoryDeleteOperation(const std::string& sourceParentA,
                                        const std::wstring& sourceParentW,
                                        const std::string& itemNameA,
                                        const std::wstring& itemNameW,
                                        DWORD attr,
                                        COperations* script)
{
    COperation op;
    op.Opcode = ocDeleteDir;
    op.OpFlags = 0;
    op.Size = DELETE_DIR_SIZE;
    op.Attr = attr;
    op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
    if (op.SourceName == NULL)
        return false;
    op.TargetName = NULL;
    op.SetSourceNameW(sourceParentW, itemNameW);
    return AddOperation(script, op);
}

static bool AddDirectoryCreateOperation(const std::string& sourceParentA,
                                        const std::wstring& sourceParentW,
                                        const std::string& targetParentA,
                                        const std::wstring& targetParentW,
                                        const std::string& itemNameA,
                                        const std::wstring& itemNameW,
                                        const std::string& targetNameA,
                                        const std::wstring& targetNameW,
                                        DWORD attr,
                                        const CBuildConfig& config,
                                        COperations* script,
                                        int& createDirIndex)
{
    COperation op;
    op.Opcode = ocCreateDir;
    op.OpFlags = OPFL_IGNORE_INVALID_NAME | TargetEncryptionFlag(attr, config, script);
    if ((op.OpFlags & OPFL_AS_ENCRYPTED) != 0 && script != NULL && !script->ShowStatus)
        script->ShowStatus = TRUE;
    op.Size = CREATE_DIR_SIZE;
    op.Attr = attr;
    op.SourceName = AllocFullPath(sourceParentA.c_str(), itemNameA.c_str());
    if (op.SourceName == NULL)
        return false;
    op.TargetName = AllocFullPath(targetParentA.c_str(), targetNameA.c_str());
    if (op.TargetName == NULL)
        return false;
    op.SetSourceNameW(sourceParentW, itemNameW);
    op.SetTargetNameW(targetParentW, targetNameW);

    const std::string sourceDirA = opplan::JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceDirW = opplan::JoinPathW(sourceParentW, itemNameW);
    if (!ConfigureADSForOperation(sourceDirA, sourceDirW, TRUE, config, script, op))
        return false;

    createDirIndex = script->Add(op);
    if (!script->IsGood())
        return false;

    return true;
}

static bool AddDirectoryTimeOperation(COperations* script,
                                      int createDirIndex,
                                      FILETIME lastWrite)
{
    if (!script->PreserveDirTime || createDirIndex < 0)
        return true;

    COperation op;
    op.Opcode = ocCopyDirTime;
    op.OpFlags = 0;
    op.Size = CHATTRS_FILE_SIZE;
    op.SourceName = (char*)(DWORD_PTR)lastWrite.dwLowDateTime;
    op.OwnsSourceName = false;
    op.TargetName = DupAnsiString(script->At(createDirIndex).TargetName);
    if (op.TargetName == NULL)
        return false;
    op.Attr = lastWrite.dwHighDateTime;
    if (script->At(createDirIndex).HasWideTarget())
        op.SetTargetNameW(script->At(createDirIndex).TargetNameW, std::wstring());
    return AddOperation(script, op);
}

static bool AddCreateDirSkipLabel(COperations* script,
                                  int createDirIndex,
                                  CQuadWord totalFileSizeBeforeDir)
{
    if (createDirIndex < 0)
        return true;

    COperation op;
    op.Opcode = ocLabelForSkipOfCreateDir;
    op.OpFlags = 0;
    op.Size.SetUI64(0);
    CQuadWord dirSize = script->TotalFileSize - totalFileSizeBeforeDir;
    op.SourceName = (char*)(DWORD_PTR)dirSize.LoDWord;
    op.TargetName = (char*)(DWORD_PTR)dirSize.HiDWord;
    op.OwnsSourceName = false;
    op.OwnsTargetName = false;
    op.Attr = createDirIndex;
    return AddOperation(script, op);
}

// A reparse-point directory delete is absorbable as a single link removal (no
// recursion into the target) when EnableReparseDelete is set (P5).
static bool IsReparseDeleteLink(EActionType action, DWORD attr, const CBuildConfig& config)
{
    return action == EActionType::Delete &&
           (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           config.EnableReparseDelete != FALSE;
}

// Emits a single ocDeleteDirLink op removing the junction/symlink itself —
// matching the legacy builder (copy_move.cpp:2519). NEVER recurses.
static bool EmitDeleteDirLinkOp(const std::string& fullA, const std::wstring& fullW,
                                DWORD attr, COperations* script)
{
    COperation op;
    op.Opcode = ocDeleteDirLink;
    op.OpFlags = 0;
    op.Size = DELETE_DIRLINK_SIZE;
    op.Attr = attr;
    op.SourceName = DupAnsiString(fullA.c_str());
    if (op.SourceName == nullptr)
        return false;
    op.SetSourceNameW(fullW, std::wstring());
    op.TargetName = nullptr;
    return AddOperation(script, op);
}

static bool BuildDirectoryTree(EActionType action,
                               const std::string& sourceParentA,
                               const std::wstring& sourceParentW,
                               const std::string& targetParentA,
                               const std::wstring& targetParentW,
                               const std::string& itemNameA,
                               const std::wstring& itemNameW,
                               const std::string& targetNameA,
                               const std::wstring& targetNameW,
                               DWORD attr,
                               FILETIME lastWrite,
                               const CBuildConfig& config,
                               COperations* script,
                               bool& emittedAny,
                               bool& movedAll)
{
    emittedAny = false;
    movedAll = true;

    opplan::CPlannedSnapshotItem dirPlan;
    if (!opplan::TryPlanChildItem(action,
                                  sourceParentA, sourceParentW,
                                  targetParentA, targetParentW,
                                  itemNameA, itemNameW,
                                  targetNameA, targetNameW,
                                  true, 0, attr, lastWrite,
                                  dirPlan))
    {
        return false;
    }

    if (HasUnsupportedAttributes(attr))
    {
        // Reparse-point directory: delete the LINK only (never recurse) when
        // absorption is enabled; otherwise reject to legacy.
        if (IsReparseDeleteLink(action, attr, config))
        {
            emittedAny = true;
            movedAll = true;
            return EmitDeleteDirLinkOp(dirPlan.SourcePathA, dirPlan.SourcePathW, attr, script);
        }
        return false;
    }
    // A system/hidden-directory prompt is a delete policy. Copy and move must
    // not inherit it merely because they share this recursive builder.
    const bool needsSHPrompt = ShouldPromptForSystemHiddenDelete(action, config, attr);
    if (needsSHPrompt && config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    const std::string sourceDirA = dirPlan.SourcePathA;
    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::string targetDirA = dirPlan.HasTarget() ? dirPlan.TargetPathA : opplan::JoinPathA(targetParentA, targetNameA);
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);

    if (needsSHPrompt)
    {
        CBuildDeletePromptResult r = config.DeletePromptCallback(
            CBuildDeletePromptKind::SystemHiddenDir, sourceDirA.c_str(), sourceDirW.c_str(),
            config.DeletePromptContext);
        if (r == CBuildDeletePromptResult::Cancel)
            return false;
        if (r == CBuildDeletePromptResult::Skip)
        {
            movedAll = false;
            emittedAny = false;
            return true;
        }
    }

    if ((action == EActionType::Copy || action == EActionType::Move) &&
        ADSForcesReject(sourceDirA, sourceDirW, TRUE, config, script))
    {
        return false;
    }

    // Fast directory move (P3): a same-root disk move of a whole directory into
    // a not-yet-existing target is a single ocMoveDir rename — matching the
    // legacy builder (copy_move.cpp:2569-2613). Opt-in via EnableFastDirMove so
    // the production gate is unchanged until parity/soak.
    if (action == EActionType::Move && config.EnableFastDirMove &&
        config.NetwareFastDirMove && script != nullptr &&
        !script->CopySecurity &&
        (script->CopyAttrs || !config.TargetPathIsEncrypted) &&
        !config.EnableFilters && !config.SkipEmptyDirs &&
        !script->SameRootButDiffVolume &&
        SameRootPathW(sourceDirW, targetDirW) &&
        !FastMoveTargetDirExists(targetDirW))
    {
        COperation op = {};
        op.Opcode = ocMoveDir;
        op.OpFlags = OPFL_IGNORE_INVALID_NAME;
        op.Size = MOVE_DIR_SIZE;
        op.Attr = attr;
        op.SourceName = DupAnsiString(sourceDirA.c_str());
        if (op.SourceName == nullptr)
            return false;
        op.TargetName = DupAnsiString(targetDirA.c_str());
        if (op.TargetName == nullptr)
        {
            free(op.SourceName);
            op.SourceName = nullptr;
            return false;
        }
        op.SetSourceNameW(sourceDirW, std::wstring());
        op.SetTargetNameW(targetDirW, std::wstring());
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
        emittedAny = true;
        movedAll = true;
        return AddOperation(script, op);
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries))
        return false;

    // ChangeAttrs / ChangeCase directory (P5). ChangeAttrs applies the dir's own
    // attribute change then (if SubDirs) recurses. ChangeCase renames contents
    // FIRST and the directory itself LAST (inner-before-outer, matching legacy
    // copy_move.cpp:3137). Both recurse through the same child loop.
    if (action == EActionType::ChangeAttrs || action == EActionType::ChangeCase)
    {
        script->DirsCount++;
        const bool recurse = (action == EActionType::ChangeAttrs) ? (config.ChangeAttrsSubDirs != FALSE)
                                                                  : (config.ChangeCaseSubDirs != FALSE);

        // ChangeAttrs: the directory's own op comes first.
        if (action == EActionType::ChangeAttrs)
        {
            COperation dop;
            dop.Opcode = ocChangeAttrs;
            dop.OpFlags = 0;
            dop.Attr = attr;
            dop.Size = CHATTRS_FILE_SIZE;
            dop.SourceName = DupAnsiString(sourceDirA.c_str());
            if (dop.SourceName == nullptr)
                return false;
            dop.SetSourceNameW(sourceDirW, std::wstring());
            dop.TargetName = (char*)(DWORD_PTR)((attr & config.ChangeAttrsAnd) | config.ChangeAttrsOr);
            dop.OwnsTargetName = false;
            if (!AddOperation(script, dop))
                return false;
            emittedAny = true;
        }

        if (recurse)
        {
            for (const DirectoryEntry& entry : entries)
            {
                if (HasUnsupportedAttributes(entry.Attr))
                    return false;
                opplan::CPlannedSnapshotItem childPlan;
                if (!opplan::TryPlanChildItem(action,
                                              sourceDirA, sourceDirW,
                                              targetDirA, targetDirW,
                                              entry.NameA, entry.NameW,
                                              entry.NameA, entry.NameW,
                                              entry.IsDir, entry.Size,
                                              entry.Attr, entry.LastWrite,
                                              childPlan))
                    return false;

                if (childPlan.IsDir)
                {
                    bool childEmitted = false;
                    bool childMovedAll = true;
                    if (!BuildDirectoryTree(action,
                                            childPlan.SourceParentA, childPlan.SourceParentW,
                                            childPlan.TargetParentA, childPlan.TargetParentW,
                                            childPlan.ItemNameA, childPlan.ItemNameW,
                                            childPlan.TargetNameA, childPlan.TargetNameW,
                                            childPlan.Attr, childPlan.LastWrite,
                                            config, script, childEmitted, childMovedAll))
                        return false;
                    emittedAny = emittedAny || childEmitted;
                }
                else
                {
                    if (!AddPlannedFileOperation(childPlan, config, script))
                        return false;
                    emittedAny = true;
                }
            }
        }

        // ChangeCase: the directory's own rename comes LAST (after contents).
        if (action == EActionType::ChangeCase)
        {
            const std::wstring alteredW =
                AlterFileNameW(dirPlan.ItemNameW.c_str(), config.ChangeCaseFormat,
                               config.ChangeCaseChange, true);
            if (!alteredW.empty() && alteredW != dirPlan.ItemNameW)
            {
                const std::string alteredA = WideToAnsi(alteredW);
                COperation dop;
                dop.Opcode = ocMoveDir;
                dop.OpFlags = 0;
                dop.Size = MOVE_DIR_SIZE;
                dop.Attr = attr;
                dop.SourceName = DupAnsiString(sourceDirA.c_str());
                if (dop.SourceName == nullptr)
                    return false;
                dop.TargetName = AllocFullPath(dirPlan.SourceParentA.c_str(), alteredA.c_str());
                if (dop.TargetName == nullptr)
                {
                    free(dop.SourceName);
                    dop.SourceName = nullptr;
                    return false;
                }
                dop.SetSourceNameW(sourceDirW, std::wstring());
                dop.SetTargetNameW(dirPlan.SourceParentW, alteredW);
                if (!script->FastMoveUsed)
                    script->FastMoveUsed = TRUE;
                if (!AddOperation(script, dop))
                    return false;
            }
            emittedAny = true;
        }

        movedAll = true;
        return true;
    }

    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty())
    {
        // P4: without a prompt callback the builder rejects this (legacy owns
        // the prompt). With one, ask and honor the answer.
        if (config.DeletePromptCallback == nullptr)
            return false;
        CBuildDeletePromptResult r = config.DeletePromptCallback(
            CBuildDeletePromptKind::NonEmptyDir, sourceDirA.c_str(), sourceDirW.c_str(),
            config.DeletePromptContext);
        if (r == CBuildDeletePromptResult::Cancel)
            return false;
        if (r == CBuildDeletePromptResult::Skip)
        {
            // Do not delete this directory (nor let the parent remove itself).
            movedAll = false;
            emittedAny = false;
            return true;
        }
        // Proceed: fall through and build the delete ops.
    }

    script->DirsCount++;

    int createDirIndex = -1;
    CQuadWord totalFileSizeBeforeDir = script->TotalFileSize;
    if (action == EActionType::Copy || action == EActionType::Move)
    {
        if (!AddDirectoryCreateOperation(dirPlan.SourceParentA, dirPlan.SourceParentW,
                                         dirPlan.TargetParentA, dirPlan.TargetParentW,
                                         dirPlan.ItemNameA, dirPlan.ItemNameW,
                                         dirPlan.TargetNameA, dirPlan.TargetNameW, attr, config,
                                         script, createDirIndex))
        {
            return false;
        }
    }

    for (const DirectoryEntry& entry : entries)
    {
        opplan::CPlannedSnapshotItem childPlan;
        if (!opplan::TryPlanChildItem(action,
                                      sourceDirA, sourceDirW,
                                      targetDirA, targetDirW,
                                      entry.NameA, entry.NameW,
                                      entry.NameA, entry.NameW,
                                      entry.IsDir, entry.Size,
                                      entry.Attr, entry.LastWrite,
                                      childPlan))
        {
            return false;
        }

        // Reparse children reject as usual, EXCEPT an absorbable reparse delete:
        // a junction child flows to BuildDirectoryTree (which emits a link op and
        // does NOT recurse), a reparse-file child to a normal link-removing
        // ocDeleteFile below.
        if (HasUnsupportedAttributes(entry.Attr) &&
            !IsReparseDeleteLink(action, entry.Attr, config))
            return false;

        if (childPlan.IsDir)
        {
            bool childEmitted = false;
            bool childMovedAll = true;
            if (!BuildDirectoryTree(action,
                                    childPlan.SourceParentA, childPlan.SourceParentW,
                                    childPlan.TargetParentA, childPlan.TargetParentW,
                                    childPlan.ItemNameA, childPlan.ItemNameW,
                                    childPlan.TargetNameA, childPlan.TargetNameW,
                                    childPlan.Attr, childPlan.LastWrite,
                                    config, script, childEmitted, childMovedAll))
            {
                return false;
            }
            emittedAny = emittedAny || childEmitted;
            movedAll = movedAll && childMovedAll;
        }
        else
        {
            const bool accepted = FilterAcceptsFile(config, childPlan.ItemNameA, childPlan.ItemNameW,
                                                   childPlan.Attr, childPlan.Size, childPlan.LastWrite);
            if (!accepted)
            {
                movedAll = false;
                continue;
            }
            if (!AddPlannedFileOperation(childPlan, config, script))
            {
                return false;
            }
            emittedAny = true;
        }
    }

    if ((action == EActionType::Copy || action == EActionType::Move) &&
        config.SkipEmptyDirs && !emittedAny)
    {
        if (createDirIndex >= 0)
        {
            script->Delete(createDirIndex);
            if (!script->IsGood())
                return false;
        }
        movedAll = false;
        return true;
    }

    if (action == EActionType::Copy || action == EActionType::Move)
    {
        if (!AddDirectoryTimeOperation(script, createDirIndex, lastWrite))
            return false;
    }

    if ((action == EActionType::Move || action == EActionType::Delete) && movedAll)
    {
        if (!AddDirectoryDeleteOperation(dirPlan.SourceParentA, dirPlan.SourceParentW,
                                         dirPlan.ItemNameA, dirPlan.ItemNameW, attr, script))
        {
            return false;
        }
    }

    if (action == EActionType::Copy || action == EActionType::Move)
    {
        if (!AddCreateDirSkipLabel(script, createDirIndex, totalFileSizeBeforeDir))
            return false;
    }

    emittedAny = true;

    return true;
}

static bool ValidateDirectoryTree(EActionType action,
                                  const std::string& sourceParentA,
                                  const std::wstring& sourceParentW,
                                  const std::string& targetParentA,
                                  const std::wstring& targetParentW,
                                  const std::string& itemNameA,
                                  const std::wstring& itemNameW,
                                  const std::string& targetNameA,
                                  const std::wstring& targetNameW,
                                  DWORD attr,
                                  const CBuildConfig& config)
{
    opplan::CPlannedSnapshotItem dirPlan;
    if (!opplan::TryPlanChildItem(action,
                                  sourceParentA, sourceParentW,
                                  targetParentA, targetParentW,
                                  itemNameA, itemNameW,
                                  targetNameA, targetNameW,
                                  true, 0, attr, FILETIME{},
                                  dirPlan))
    {
        return false;
    }

    if (HasUnsupportedAttributes(attr))
    {
        // An absorbable reparse-point delete is feasible as a single link
        // removal — feasibility must return here WITHOUT enumerating (never open
        // the junction / touch the link target).
        if (IsReparseDeleteLink(action, attr, config))
            return true;
        return false;
    }
    // Feasibility only (no prompt): a system/hidden dir delete is handleable
    // iff a prompt callback exists (P4); otherwise reject to legacy.
    if (ShouldPromptForSystemHiddenDelete(action, config, attr) &&
        config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    const std::string sourceDirA = dirPlan.SourcePathA;
    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::string targetDirA = dirPlan.HasTarget() ? dirPlan.TargetPathA : opplan::JoinPathA(targetParentA, targetNameA);
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);
    if ((action == EActionType::Copy || action == EActionType::Move) &&
        ADSForcesReject(sourceDirA, sourceDirW, TRUE, config, nullptr))
    {
        return false;
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries))
        return false;

    // Feasibility only — do NOT prompt here (BuildDirectoryTree does the actual
    // prompt during emission). A non-empty delete is handleable iff a prompt
    // callback exists; otherwise reject to legacy (P4).
    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty() &&
        config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    for (const DirectoryEntry& entry : entries)
    {
        opplan::CPlannedSnapshotItem childPlan;
        if (!opplan::TryPlanChildItem(action,
                                      sourceDirA, sourceDirW,
                                      targetDirA, targetDirW,
                                      entry.NameA, entry.NameW,
                                      entry.NameA, entry.NameW,
                                      entry.IsDir, entry.Size,
                                      entry.Attr, entry.LastWrite,
                                      childPlan))
        {
            return false;
        }

        if (HasUnsupportedAttributes(entry.Attr) &&
            !IsReparseDeleteLink(action, entry.Attr, config))
            return false;

        if (childPlan.IsDir)
        {
            if (!ValidateDirectoryTree(action,
                                       childPlan.SourceParentA, childPlan.SourceParentW,
                                       childPlan.TargetParentA, childPlan.TargetParentW,
                                       childPlan.ItemNameA, childPlan.ItemNameW,
                                       childPlan.TargetNameA, childPlan.TargetNameW,
                                       childPlan.Attr, config))
            {
                return false;
            }
        }
        else if (action == EActionType::Copy || action == EActionType::Move)
        {
            if (ADSForcesReject(childPlan.SourcePathA, childPlan.SourcePathW, FALSE, config, nullptr))
                return false;
        }
    }

    return true;
}

static bool ValidateFirstTrancheSnapshot(const CSelectionSnapshot& snapshot,
                                         const CBuildConfig& config,
                                         const std::string& sourcePathA,
                                         const std::wstring& sourcePathW,
                                         const std::string& targetPathA,
                                         const std::wstring& targetPathW)
{
    switch (snapshot.Action)
    {
    case EActionType::Delete:
    case EActionType::Convert:
    case EActionType::RecursiveConvert:
        if (sourcePathA.empty() || sourcePathW.empty())
            return false;
        break;

    case EActionType::ChangeAttrs:
        // P5 (opt-in): default OFF routes to legacy. Directories require the
        // recursive-directory surface (checked per item below).
        if (!config.EnableChangeAttrs)
            return false;
        if (sourcePathA.empty() || sourcePathW.empty())
            return false;
        break;

    case EActionType::ChangeCase:
        // P5 (opt-in): default OFF routes to legacy. Directories require the
        // recursive-directory surface (checked per item below).
        if (!config.EnableChangeCase)
            return false;
        if (sourcePathA.empty() || sourcePathW.empty())
            return false;
        break;

    case EActionType::Copy:
    case EActionType::Move:
        if (sourcePathA.empty() || sourcePathW.empty() ||
            targetPathA.empty() || targetPathW.empty())
        {
            return false;
        }
        if (!opplan::IsDefaultMask(snapshot.Mask) && !config.EnableExplicitTargetNames)
            return false;
        break;

    default:
        return false;
    }

    for (const CSnapshotItem& item : snapshot.Items)
    {
        if (item.IsDir && snapshot.Action == EActionType::Convert)
            return false;
        if (item.IsDir && !config.EnableRecursiveDirectories)
            return false;

        opplan::CPlannedSnapshotItem plan;
        if (!opplan::TryPlanSnapshotItem(snapshot, config, item, plan))
            return false;

        if (!plan.IsDir)
            continue;

        if (item.IsDir && HasUnsupportedAttributes(item) &&
            !IsReparseDeleteLink(snapshot.Action, item.Attr, config))
            return false;

        if (item.IsDir &&
            !ValidateDirectoryTree(snapshot.Action,
                                   plan.SourceParentA, plan.SourceParentW,
                                   plan.TargetParentA, plan.TargetParentW,
                                   plan.ItemNameA, plan.ItemNameW,
                                   plan.TargetNameA, plan.TargetNameW,
                                   item.Attr, config))
        {
            return false;
        }
    }

    return true;
}

BOOL BuildScriptFromSnapshot(
    const CSelectionSnapshot& snapshot,
    const CBuildConfig& config,
    CBuildScriptState& state,
    COperations* script)
{
    if (script == NULL)
        return FALSE;
    (void)state;

    const std::wstring sourcePathW = opplan::SnapshotPathW(snapshot.SourcePath, snapshot.SourcePathW);
    const std::wstring targetPathW = opplan::SnapshotPathW(snapshot.TargetPath, snapshot.TargetPathW);

    std::string sourcePathA;
    std::string targetPathA;
    if (!opplan::SnapshotPathA(snapshot.SourcePath, sourcePathW, sourcePathA))
        return FALSE;
    if ((snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move) &&
        !opplan::SnapshotPathA(snapshot.TargetPath, targetPathW, targetPathA))
    {
        return FALSE;
    }

    if (!ValidateFirstTrancheSnapshot(snapshot, config, sourcePathA, sourcePathW, targetPathA, targetPathW))
        return FALSE;

    // Configure COperations fields from snapshot options
    script->IsCopyOrMoveOperation = (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move);
    script->IsCopyOperation = (snapshot.Action == EActionType::Copy);
    script->OverwriteOlder = snapshot.OverwriteOlder;
    script->CopySecurity = snapshot.CopySecurity;
    script->CopyAttrs = snapshot.CopyAttrs;
    script->PreserveDirTime = snapshot.PreserveDirTime;
    script->TargetPathSupADS = config.TargetSupportsADS;
    script->InvertRecycleBin = snapshot.InvertRecycleBin;
    script->StartOnIdle = snapshot.StartOnIdle;
    if (snapshot.UseSpeedLimit && snapshot.SpeedLimit > 0)
    {
        script->ChangeSpeedLimit = TRUE;
        script->SetSpeedLimit(TRUE, snapshot.SpeedLimit);
    }

    // Set work paths for change notifications
    script->SetWorkPath1(sourcePathA.c_str(), TRUE);
    script->SetWorkPath1W(sourcePathW.c_str(), TRUE);
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        script->SetWorkPath2(targetPathA.c_str(), TRUE);
        script->SetWorkPath2W(targetPathW.c_str(), TRUE);
    }

    // ClearReadOnly mask: if ClearReadOnly config is set, remove FILE_ATTRIBUTE_READONLY
    if (config.ClearReadOnly)
        script->ClearReadonlyMask = ~FILE_ATTRIBUTE_READONLY;

    // ChangeAttrs (P5): mirror the snapshot's attr masks into a working config
    // so AddFileOperation can compute the new attributes.
    CBuildConfig runConfig = config;
    if (snapshot.Action == EActionType::ChangeAttrs)
    {
        runConfig.ChangeAttrsAnd = snapshot.AttrsData.AttrAnd;
        runConfig.ChangeAttrsOr = snapshot.AttrsData.AttrOr;
        runConfig.ChangeAttrsCompression = snapshot.AttrsData.ChangeCompression;
        runConfig.ChangeAttrsEncryption = snapshot.AttrsData.ChangeEncryption;
        runConfig.ChangeAttrsSubDirs = snapshot.AttrsData.SubDirs;
    }
    if (snapshot.Action == EActionType::ChangeCase)
    {
        runConfig.ChangeCaseFormat = snapshot.ChangeCaseData.FileNameFormat;
        runConfig.ChangeCaseChange = snapshot.ChangeCaseData.Change;
        runConfig.ChangeCaseSubDirs = snapshot.ChangeCaseData.SubDirs;
    }

    // Process each item in the snapshot
    for (size_t i = 0; i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];
        opplan::CPlannedSnapshotItem plan;
        if (!opplan::TryPlanSnapshotItem(snapshot, runConfig, item, plan))
            return FALSE;

        if (!plan.IsDir)
        {
            if (!AddPlannedFileOperation(plan, runConfig, script))
            {
                return FALSE;
            }
            continue;
        }

        bool emittedAny = false;
        bool movedAll = true;
        if (!BuildDirectoryTree(snapshot.Action,
                                plan.SourceParentA, plan.SourceParentW,
                                plan.TargetParentA, plan.TargetParentW,
                                plan.ItemNameA, plan.ItemNameW,
                                plan.TargetNameA, plan.TargetNameW,
                                item.Attr, item.LastWrite,
                                runConfig, script, emittedAny, movedAll))
        {
            return FALSE;
        }
    }

    // Compute TotalSize from all operations
    CQuadWord totalSize(0, 0);
    for (int i = 0; i < script->Count; i++)
        totalSize += script->At(i).Size;
    script->TotalSize = totalSize;

    return TRUE;
}
