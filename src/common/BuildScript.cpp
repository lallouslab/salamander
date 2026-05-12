// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// BuildScript.cpp — standalone COperations script builder from CSelectionSnapshot.
// See BuildScript.h for interface documentation.

#include "precomp.h"

#include "worker.h"
#include "common/BuildScript.h"

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
    if (ansiPath.empty())
        return std::wstring();

    int len = MultiByteToWideChar(CP_ACP, 0, ansiPath.c_str(), -1, NULL, 0);
    if (len <= 0)
        return std::wstring();

    std::wstring out((size_t)len, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, ansiPath.c_str(), -1, out.data(), len) == 0)
        return std::wstring();
    out.resize((size_t)len - 1);
    return out;
}

BOOL BuildScriptFromSnapshot(
    const CSelectionSnapshot& snapshot,
    const CBuildConfig& config,
    CBuildScriptState& state,
    COperations* script)
{
    if (script == NULL)
        return FALSE;

    const std::wstring sourcePathW = SnapshotPathW(snapshot.SourcePath, snapshot.SourcePathW);
    const std::wstring targetPathW = SnapshotPathW(snapshot.TargetPath, snapshot.TargetPathW);

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
    if (!snapshot.SourcePath.empty())
        script->SetWorkPath1(snapshot.SourcePath.c_str(), TRUE);
    if (!snapshot.TargetPath.empty())
        script->SetWorkPath2(snapshot.TargetPath.c_str(), TRUE);

    // ClearReadOnly mask: if ClearReadOnly config is set, remove FILE_ATTRIBUTE_READONLY
    if (config.ClearReadOnly)
        script->ClearReadonlyMask = ~FILE_ATTRIBUTE_READONLY;

    // Process each item in the snapshot
    for (size_t i = 0; i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];
        COperation op;

        switch (snapshot.Action)
        {
        case EActionType::Delete:
        {
            if (item.IsDir)
            {
                // For directory links (reparse points), use ocDeleteDirLink
                if (item.Attr & FILE_ATTRIBUTE_REPARSE_POINT)
                    op.Opcode = ocDeleteDirLink;
                else
                    op.Opcode = ocDeleteDir;
                op.OpFlags = 0;
                op.Size = (item.Attr & FILE_ATTRIBUTE_REPARSE_POINT) ? DELETE_DIRLINK_SIZE : DELETE_DIR_SIZE;
                op.Attr = item.Attr;
                op.SourceName = AllocFullPath(snapshot.SourcePath.c_str(), item.Name.c_str());
                if (op.SourceName == NULL)
                    return FALSE;
                op.TargetName = NULL;
                // Set wide path if available (with \\?\ prefix for long paths)
                if (!item.NameW.empty())
                    op.SetSourceNameW(sourcePathW, item.NameW);
                else if (!snapshot.SourcePath.empty())
                    op.SetSourceNameW(sourcePathW, std::wstring(item.Name.begin(), item.Name.end()));

                script->DirsCount++;
                script->Add(op);
                if (!script->IsGood())
                    return FALSE;
            }
            else
            {
                op.Opcode = ocDeleteFile;
                op.OpFlags = 0;
                op.Size = DELETE_FILE_SIZE;
                op.Attr = item.Attr;
                op.SourceName = AllocFullPath(snapshot.SourcePath.c_str(), item.Name.c_str());
                if (op.SourceName == NULL)
                    return FALSE;
                op.TargetName = NULL;
                if (!item.NameW.empty())
                    op.SetSourceNameW(sourcePathW, item.NameW);
                else if (!snapshot.SourcePath.empty())
                    op.SetSourceNameW(sourcePathW, std::wstring(item.Name.begin(), item.Name.end()));

                script->FilesCount++;
                script->Add(op);
                if (!script->IsGood())
                    return FALSE;
            }
            break;
        }

        case EActionType::Copy:
        case EActionType::Move:
        {
            COperationCode fileOp = (snapshot.Action == EActionType::Copy) ? ocCopyFile : ocMoveFile;

            if (item.IsDir)
            {
                // For Copy/Move of directories: emit ocCreateDir + ocDeleteDir (for move)
                // Simplified: we emit ocCreateDir with source and target paths
                COperation dirOp;
                dirOp.Opcode = ocCreateDir;
                dirOp.OpFlags = 0;
                dirOp.Size = CREATE_DIR_SIZE;
                dirOp.Attr = item.Attr;
                dirOp.SourceName = AllocFullPath(snapshot.SourcePath.c_str(), item.Name.c_str());
                if (dirOp.SourceName == NULL)
                    return FALSE;
                dirOp.TargetName = AllocFullPath(snapshot.TargetPath.c_str(), item.Name.c_str());
                if (dirOp.TargetName == NULL)
                    return FALSE;
                // Set wide paths (with \\?\ prefix for long paths)
                if (!item.NameW.empty())
                {
                    dirOp.SetSourceNameW(sourcePathW, item.NameW);
                    dirOp.SetTargetNameW(targetPathW, item.NameW);
                }
                else if (!snapshot.SourcePath.empty())
                {
                    std::wstring nameW(item.Name.begin(), item.Name.end());
                    dirOp.SetSourceNameW(sourcePathW, nameW);
                    dirOp.SetTargetNameW(targetPathW, nameW);
                }

                script->DirsCount++;
                script->Add(dirOp);
                if (!script->IsGood())
                    return FALSE;

                // For Move, also add ocDeleteDir after creation
                if (snapshot.Action == EActionType::Move)
                {
                    COperation delOp;
                    if (item.Attr & FILE_ATTRIBUTE_REPARSE_POINT)
                    {
                        delOp.Opcode = ocDeleteDirLink;
                        delOp.Size = DELETE_DIRLINK_SIZE;
                    }
                    else
                    {
                        delOp.Opcode = ocDeleteDir;
                        delOp.Size = DELETE_DIR_SIZE;
                    }
                    delOp.OpFlags = 0;
                    delOp.Attr = item.Attr;
                    delOp.SourceName = AllocFullPath(snapshot.SourcePath.c_str(), item.Name.c_str());
                    if (delOp.SourceName == NULL)
                        return FALSE;
                    delOp.TargetName = NULL;
                    if (!item.NameW.empty())
                        delOp.SetSourceNameW(sourcePathW, item.NameW);
                    else if (!snapshot.SourcePath.empty())
                        delOp.SetSourceNameW(sourcePathW, std::wstring(item.Name.begin(), item.Name.end()));

                    script->Add(delOp);
                    if (!script->IsGood())
                        return FALSE;
                }
            }
            else
            {
                // File: ocCopyFile or ocMoveFile
                op.Opcode = fileOp;
                op.OpFlags = 0;
                op.Attr = item.Attr;
                op.FileSize = CQuadWord((DWORD)(item.Size & 0xFFFFFFFF), (DWORD)(item.Size >> 32));

                // Size for progress estimation
                CQuadWord fileSizeLoc = op.FileSize;
                if (fileSizeLoc >= COPY_MIN_FILE_SIZE)
                    op.Size = fileSizeLoc;
                else
                    op.Size = COPY_MIN_FILE_SIZE;

                op.SourceName = AllocFullPath(snapshot.SourcePath.c_str(), item.Name.c_str());
                if (op.SourceName == NULL)
                    return FALSE;
                op.TargetName = AllocFullPath(snapshot.TargetPath.c_str(), item.Name.c_str());
                if (op.TargetName == NULL)
                    return FALSE;
                // Set wide paths (with \\?\ prefix for long paths)
                if (!item.NameW.empty())
                {
                    op.SetSourceNameW(sourcePathW, item.NameW);
                    op.SetTargetNameW(targetPathW, item.NameW);
                }
                else if (!snapshot.SourcePath.empty())
                {
                    std::wstring nameW(item.Name.begin(), item.Name.end());
                    op.SetSourceNameW(sourcePathW, nameW);
                    op.SetTargetNameW(targetPathW, nameW);
                }

                script->FilesCount++;
                script->TotalFileSize += fileSizeLoc;
                script->Add(op);
                if (!script->IsGood())
                    return FALSE;
            }
            break;
        }

        default:
            // Other action types not supported in this simplified builder
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
