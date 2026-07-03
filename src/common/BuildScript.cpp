// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
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

static bool HasTrailingSlashW(const std::wstring& path)
{
    return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

static bool HasLongPathPrefixW(const std::wstring& path)
{
    return path.compare(0, 4, L"\\\\?\\") == 0;
}

static std::wstring MakeLongPathSafeW(const std::wstring& path)
{
    if (path.length() < 240 || HasLongPathPrefixW(path))
        return path;
    if (path.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.length() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/'))
    {
        return L"\\\\?\\" + path;
    }
    return path;
}

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

static bool NeedsSystemHiddenDeletePrompt(const CBuildConfig& config, DWORD attr)
{
    return config.ConfirmDeleteSystemHiddenDir &&
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
        return false;

    op.OpFlags |= OPFL_COPY_ADS;
    op.Size += ads.Size;
    if (script != NULL)
    {
        script->TotalFileSize += ads.Size;
        script->OccupiedSpace += ads.OccupiedSpace;
    }
    return true;
}

static bool SourceHasADSOrProbeError(const std::string& sourceA,
                                     const std::wstring& sourceW,
                                     BOOL isDir,
                                     const CBuildConfig& config,
                                     COperations* script)
{
    ADSProbeResult ads = ProbeSourceADS(sourceA, sourceW, isDir, config, script);
    return ads.HasADS || ads.HasProbeError;
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
    std::wstring searchW = dirW;
    if (!HasTrailingSlashW(searchW))
        searchW.push_back(L'\\');
    searchW.push_back(L'*');
    searchW = MakeLongPathSafeW(searchW);

    WIN32_FIND_DATAW data = {};
    HANDLE find = FindFirstFileW(searchW.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES;
    }

    do
    {
        if (data.cFileName[0] == L'\0' || IsDotDirectory(data.cFileName))
            continue;

        DirectoryEntry entry = {};
        entry.NameW = data.cFileName;
        entry.NameA = WideToAnsi(entry.NameW);
        entry.Attr = data.dwFileAttributes;
        entry.Size = (((unsigned __int64)data.nFileSizeHigh) << 32) |
                     data.nFileSizeLow;
        entry.LastWrite = data.ftLastWriteTime;
        entry.IsDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entries.push_back(entry);
    } while (FindNextFileW(find, &data));

    DWORD err = GetLastError();
    FindClose(find);
    if (err != ERROR_NO_MORE_FILES)
        return false;

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

    if (HasUnsupportedAttributes(attr) ||
        NeedsSystemHiddenDeletePrompt(config, attr))
    {
        return false;
    }

    const std::string sourceDirA = dirPlan.SourcePathA;
    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::string targetDirA = dirPlan.HasTarget() ? dirPlan.TargetPathA : opplan::JoinPathA(targetParentA, targetNameA);
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);

    if ((action == EActionType::Copy || action == EActionType::Move) &&
        SourceHasADSOrProbeError(sourceDirA, sourceDirW, TRUE, config, script) &&
        (!config.EnableADS || !config.TargetSupportsADS))
    {
        return false;
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries))
        return false;

    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty())
    {
        return false;
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

        if (HasUnsupportedAttributes(entry.Attr))
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

    if ((action == EActionType::Move && movedAll) || action == EActionType::Delete)
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

    if (HasUnsupportedAttributes(attr) ||
        NeedsSystemHiddenDeletePrompt(config, attr))
    {
        return false;
    }

    const std::string sourceDirA = dirPlan.SourcePathA;
    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::string targetDirA = dirPlan.HasTarget() ? dirPlan.TargetPathA : opplan::JoinPathA(targetParentA, targetNameA);
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);
    if ((action == EActionType::Copy || action == EActionType::Move) &&
        SourceHasADSOrProbeError(sourceDirA, sourceDirW, TRUE, config, nullptr) &&
        (!config.EnableADS || !config.TargetSupportsADS))
    {
        return false;
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries))
        return false;

    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty())
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

        if (HasUnsupportedAttributes(entry.Attr))
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
            if (SourceHasADSOrProbeError(childPlan.SourcePathA, childPlan.SourcePathW, FALSE, config, nullptr) &&
                (!config.EnableADS || !config.TargetSupportsADS))
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

        if (item.IsDir && HasUnsupportedAttributes(item))
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

    // Process each item in the snapshot
    for (size_t i = 0; i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];
        opplan::CPlannedSnapshotItem plan;
        if (!opplan::TryPlanSnapshotItem(snapshot, config, item, plan))
            return FALSE;

        if (!plan.IsDir)
        {
            if (!AddPlannedFileOperation(plan, config, script))
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
                                config, script, emittedAny, movedAll))
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
