// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// BuildScript.cpp — standalone COperations script builder from CSelectionSnapshot.
// See BuildScript.h for interface documentation.

#include "precomp.h"

#include <algorithm>
#include <vector>

#include "worker.h"
#include "common/BuildScript.h"
#include "common/unicode/helpers.h"

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

static std::wstring SnapshotPathW(const std::string& ansiPath, const std::wstring& widePath)
{
    if (!widePath.empty())
        return widePath;
    return AnsiToWide(ansiPath.c_str());
}

static bool SnapshotPathA(const std::string& ansiPath, const std::wstring& widePath, std::string& out)
{
    if (!ansiPath.empty())
    {
        out = ansiPath;
        return true;
    }
    if (widePath.empty())
        return false;
    return sally::unicode::TryWideToAnsiRoundTripExact(widePath, out);
}

static std::wstring SnapshotItemNameW(const CSnapshotItem& item)
{
    if (!item.NameW.empty())
        return item.NameW;
    return AnsiToWide(item.Name.c_str());
}

static bool SnapshotItemNameA(const CSnapshotItem& item, const std::wstring& itemNameW, std::string& out)
{
    if (!item.Name.empty())
    {
        out = item.Name;
        return true;
    }
    if (itemNameW.empty())
        return false;
    return sally::unicode::TryWideToAnsiRoundTripExact(itemNameW, out);
}

static std::wstring SnapshotTargetNameW(const CSnapshotItem& item,
                                        const std::wstring& fallbackNameW)
{
    if (!item.HasTargetName)
        return fallbackNameW;
    if (!item.TargetNameW.empty())
        return item.TargetNameW;
    return AnsiToWide(item.TargetName.c_str());
}

static bool SnapshotTargetNameA(const CSnapshotItem& item,
                                const std::wstring& targetNameW,
                                const std::string& fallbackNameA,
                                std::string& out)
{
    if (!item.HasTargetName)
    {
        out = fallbackNameA;
        return true;
    }
    if (!item.TargetName.empty())
    {
        out = item.TargetName;
        return true;
    }
    if (targetNameW.empty())
        return false;
    return sally::unicode::TryWideToAnsiRoundTripExact(targetNameW, out);
}

static bool IsDefaultMask(const std::string& mask)
{
    return mask.empty() || mask == "*.*";
}

static std::string JoinPathA(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    if (name.empty())
        return dir;
    std::string out = dir;
    if (out.back() != '\\')
        out.push_back('\\');
    out += name;
    return out;
}

static std::wstring JoinPathW(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty())
        return name;
    if (name.empty())
        return dir;
    std::wstring out = dir;
    if (out.back() != L'\\')
        out.push_back(L'\\');
    out += name;
    return out;
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

    DWORD adsWinError = NO_ERROR;
    BOOL onlyDiscardableStreams = FALSE;
    if (CheckFileOrDirADS(sourceA.c_str(), isDir, &result.Size, NULL, NULL, NULL,
                          &adsWinError, 0, NULL, NULL, sourceW) ||
        adsWinError != NO_ERROR)
    {
        if (adsWinError == NO_ERROR)
        {
            CQuadWord occupiedSpace;
            DWORD ignoredError = NO_ERROR;
            CheckFileOrDirADS(sourceA.c_str(), isDir, &result.Size, NULL, NULL, NULL,
                              &ignoredError,
                              script != NULL ? script->BytesPerCluster : 0,
                              &occupiedSpace, &onlyDiscardableStreams, sourceW);
            result.OccupiedSpace = occupiedSpace;
            result.HasADS = true;
        }
        else
            result.HasProbeError = true;
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

    const std::string sourceFullA = JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceFullW = JoinPathW(sourceParentW, itemNameW);

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

    if (!ConfigureADSForOperation(sourceFullA, sourceFullW, FALSE, config, script, op))
        return false;

    script->FilesCount++;
    script->TotalFileSize += fileSizeLoc;
    return AddOperation(script, op);
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

    const std::string sourceDirA = JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceDirW = JoinPathW(sourceParentW, itemNameW);
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
    op.TargetName = DupStr(script->At(createDirIndex).TargetName);
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
    if (HasUnsupportedAttributes(attr) ||
        NeedsSystemHiddenDeletePrompt(config, attr))
    {
        return false;
    }

    const std::string sourceDirA = JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceDirW = JoinPathW(sourceParentW, itemNameW);
    const std::string targetDirA = JoinPathA(targetParentA, targetNameA);
    const std::wstring targetDirW = JoinPathW(targetParentW, targetNameW);

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
        if (!AddDirectoryCreateOperation(sourceParentA, sourceParentW,
                                         targetParentA, targetParentW,
                                         itemNameA, itemNameW,
                                         targetNameA, targetNameW, attr, config,
                                         script, createDirIndex))
        {
            return false;
        }
    }

    for (const DirectoryEntry& entry : entries)
    {
        if (HasUnsupportedAttributes(entry.Attr))
            return false;

        if (entry.IsDir)
        {
            bool childEmitted = false;
            bool childMovedAll = true;
            if (!BuildDirectoryTree(action, sourceDirA, sourceDirW,
                                    targetDirA, targetDirW,
                                    entry.NameA, entry.NameW,
                                    entry.NameA, entry.NameW,
                                    entry.Attr, entry.LastWrite,
                                    config, script, childEmitted, childMovedAll))
            {
                return false;
            }
            emittedAny = emittedAny || childEmitted;
            movedAll = movedAll && childMovedAll;
        }
        else
        {
            const bool accepted = FilterAcceptsFile(config, entry.NameA, entry.NameW,
                                                   entry.Attr, entry.Size, entry.LastWrite);
            if (!accepted)
            {
                movedAll = false;
                continue;
            }
            if (!AddFileOperation(action, sourceDirA, sourceDirW,
                                  targetDirA, targetDirW,
                                  entry.NameA, entry.NameW,
                                  entry.NameA, entry.NameW,
                                  entry.Size, entry.Attr, entry.LastWrite,
                                  config, script))
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
        if (!AddDirectoryDeleteOperation(sourceParentA, sourceParentW,
                                         itemNameA, itemNameW, attr, script))
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
                                  const std::string& itemNameA,
                                  const std::wstring& itemNameW,
                                  DWORD attr,
                                  const CBuildConfig& config)
{
    if (HasUnsupportedAttributes(attr) ||
        NeedsSystemHiddenDeletePrompt(config, attr))
    {
        return false;
    }

    const std::string sourceDirA = JoinPathA(sourceParentA, itemNameA);
    const std::wstring sourceDirW = JoinPathW(sourceParentW, itemNameW);
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
        if (HasUnsupportedAttributes(entry.Attr))
            return false;

        if (entry.IsDir)
        {
            if (!ValidateDirectoryTree(action,
                                       sourceDirA, sourceDirW,
                                       entry.NameA, entry.NameW,
                                       entry.Attr, config))
            {
                return false;
            }
        }
        else if (action == EActionType::Copy || action == EActionType::Move)
        {
            const std::string sourceFileA = JoinPathA(sourceDirA, entry.NameA);
            const std::wstring sourceFileW = JoinPathW(sourceDirW, entry.NameW);
            if (SourceHasADSOrProbeError(sourceFileA, sourceFileW, FALSE, config, nullptr) &&
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
        if (!IsDefaultMask(snapshot.Mask) && !config.EnableExplicitTargetNames)
            return false;
        break;

    default:
        return false;
    }

    for (const CSnapshotItem& item : snapshot.Items)
    {
        if (item.IsDir && !config.EnableRecursiveDirectories)
            return false;

        const std::wstring itemNameW = SnapshotItemNameW(item);
        std::string itemNameA;
        if (!SnapshotItemNameA(item, itemNameW, itemNameA) ||
            itemNameA.empty() || itemNameW.empty())
        {
            return false;
        }

        if ((snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move) &&
            !IsDefaultMask(snapshot.Mask))
        {
            const std::wstring targetNameW = SnapshotTargetNameW(item, itemNameW);
            std::string targetNameA;
            if (!item.HasTargetName ||
                !SnapshotTargetNameA(item, targetNameW, itemNameA, targetNameA) ||
                targetNameA.empty() || targetNameW.empty())
            {
                return false;
            }
        }

        if (item.IsDir && HasUnsupportedAttributes(item))
            return false;

        if (item.IsDir &&
            !ValidateDirectoryTree(snapshot.Action,
                                   sourcePathA, sourcePathW,
                                   itemNameA, itemNameW,
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

    const std::wstring sourcePathW = SnapshotPathW(snapshot.SourcePath, snapshot.SourcePathW);
    const std::wstring targetPathW = SnapshotPathW(snapshot.TargetPath, snapshot.TargetPathW);

    std::string sourcePathA;
    std::string targetPathA;
    if (!SnapshotPathA(snapshot.SourcePath, sourcePathW, sourcePathA))
        return FALSE;
    if ((snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move) &&
        !SnapshotPathA(snapshot.TargetPath, targetPathW, targetPathA))
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
        const std::wstring itemNameW = SnapshotItemNameW(item);
        std::string itemNameA;
        if (!SnapshotItemNameA(item, itemNameW, itemNameA))
            return FALSE;
        const std::wstring targetNameW = SnapshotTargetNameW(item, itemNameW);
        std::string targetNameA;
        if (!SnapshotTargetNameA(item, targetNameW, itemNameA, targetNameA))
            return FALSE;

        if (item.IsDir)
        {
            bool emittedAny = false;
            bool movedAll = true;
            if (!BuildDirectoryTree(snapshot.Action,
                                    sourcePathA, sourcePathW,
                                    targetPathA, targetPathW,
                                    itemNameA, itemNameW,
                                    targetNameA, targetNameW,
                                    item.Attr, item.LastWrite,
                                    config, script, emittedAny, movedAll))
            {
                return FALSE;
            }
            continue;
        }

        if (!AddFileOperation(snapshot.Action,
                              sourcePathA, sourcePathW,
                              targetPathA, targetPathW,
                              itemNameA, itemNameW,
                              targetNameA, targetNameW,
                              item.Size, item.Attr, item.LastWrite,
                              config, script))
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
