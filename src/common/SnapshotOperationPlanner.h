// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SnapshotOperationPlanner — UI-free operation DTOs from CSelectionSnapshot.
//
// This is the planning seam between the panel/UI snapshot and the legacy
// COperations worker script. Tests can exercise the same path/name decisions
// that production uses without linking the full worker/UI surface.

#pragma once

#include "CBuildConfig.h"
#include "CSelectionSnapshot.h"
#include "unicode/helpers.h"

#include <string>
#include <utility>

namespace sally::operation_planner
{

enum class PlannedOperationKind
{
    CopyFile,
    MoveFile,
    DeleteFile,
    ConvertFile,
    CopyDirectory,
    MoveDirectory,
    DeleteDirectory,
    ConvertDirectory,
    ChangeAttrsFile,      // P5
    ChangeAttrsDirectory, // P5
    ChangeCaseFile,       // P5
    ChangeCaseDirectory,  // P5
};

struct CPlannedSnapshotItem
{
    EActionType Action = EActionType::Copy;
    PlannedOperationKind Kind = PlannedOperationKind::CopyFile;
    bool IsDir = false;

    std::string SourceParentA;
    std::wstring SourceParentW;
    std::string TargetParentA;
    std::wstring TargetParentW;

    std::string ItemNameA;
    std::wstring ItemNameW;
    std::string TargetNameA;
    std::wstring TargetNameW;

    std::string SourcePathA;
    std::wstring SourcePathW;
    std::string TargetPathA;
    std::wstring TargetPathW;

    unsigned __int64 Size = 0;
    DWORD Attr = 0;
    FILETIME LastWrite = {};

    bool HasTarget() const
    {
        return Kind == PlannedOperationKind::CopyFile ||
               Kind == PlannedOperationKind::MoveFile ||
               Kind == PlannedOperationKind::CopyDirectory ||
               Kind == PlannedOperationKind::MoveDirectory;
    }

    bool IsFile() const
    {
        return !IsDir;
    }
};

using CPlannedFileOperation = CPlannedSnapshotItem;

inline std::wstring SnapshotPathW(const std::string& ansiPath, const std::wstring& widePath)
{
    if (!widePath.empty())
        return widePath;
    return AnsiToWide(ansiPath.c_str());
}

inline bool SnapshotPathA(const std::string& ansiPath, const std::wstring& widePath, std::string& out)
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

inline std::wstring SnapshotItemNameW(const CSnapshotItem& item)
{
    if (!item.NameW.empty())
        return item.NameW;
    return AnsiToWide(item.Name.c_str());
}

inline bool SnapshotItemNameA(const CSnapshotItem& item, const std::wstring& itemNameW, std::string& out)
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

inline std::wstring SnapshotTargetNameW(const CSnapshotItem& item,
                                        const std::wstring& fallbackNameW)
{
    if (!item.HasTargetName)
        return fallbackNameW;
    if (!item.TargetNameW.empty())
        return item.TargetNameW;
    return AnsiToWide(item.TargetName.c_str());
}

inline bool SnapshotTargetNameA(const CSnapshotItem& item,
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

inline bool IsDefaultMask(const std::string& mask)
{
    return mask.empty() || mask == "*.*";
}

inline std::string JoinPathA(const std::string& dir, const std::string& name)
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

inline std::wstring JoinPathW(const std::wstring& dir, const std::wstring& name)
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

inline PlannedOperationKind PlannedKindFor(EActionType action, bool isDir)
{
    if (action == EActionType::Copy)
        return isDir ? PlannedOperationKind::CopyDirectory : PlannedOperationKind::CopyFile;
    if (action == EActionType::Move)
        return isDir ? PlannedOperationKind::MoveDirectory : PlannedOperationKind::MoveFile;
    if (action == EActionType::Convert || action == EActionType::RecursiveConvert)
        return isDir ? PlannedOperationKind::ConvertDirectory : PlannedOperationKind::ConvertFile;
    if (action == EActionType::ChangeAttrs)
        return isDir ? PlannedOperationKind::ChangeAttrsDirectory : PlannedOperationKind::ChangeAttrsFile;
    if (action == EActionType::ChangeCase)
        return isDir ? PlannedOperationKind::ChangeCaseDirectory : PlannedOperationKind::ChangeCaseFile;
    return isDir ? PlannedOperationKind::DeleteDirectory : PlannedOperationKind::DeleteFile;
}

inline bool TryPlanChildItem(EActionType action,
                             const std::string& sourceParentA,
                             const std::wstring& sourceParentW,
                             const std::string& targetParentA,
                             const std::wstring& targetParentW,
                             const std::string& itemNameA,
                             const std::wstring& itemNameW,
                             const std::string& targetNameA,
                             const std::wstring& targetNameW,
                             bool isDir,
                             unsigned __int64 size,
                             DWORD attr,
                             FILETIME lastWrite,
                             CPlannedSnapshotItem& plan)
{
    plan = CPlannedSnapshotItem{};
    if (action != EActionType::Copy &&
        action != EActionType::Move &&
        action != EActionType::Delete &&
        action != EActionType::Convert &&
        action != EActionType::RecursiveConvert &&
        action != EActionType::ChangeAttrs && // P5
        action != EActionType::ChangeCase)    // P5
    {
        return false;
    }
    if (sourceParentA.empty() || sourceParentW.empty() ||
        itemNameA.empty() || itemNameW.empty())
    {
        return false;
    }

    const bool needsTarget = (action == EActionType::Copy || action == EActionType::Move);
    if (needsTarget &&
        (targetParentA.empty() || targetParentW.empty() ||
         targetNameA.empty() || targetNameW.empty()))
    {
        return false;
    }

    plan.Action = action;
    plan.Kind = PlannedKindFor(action, isDir);
    plan.IsDir = isDir;
    plan.SourceParentA = sourceParentA;
    plan.SourceParentW = sourceParentW;
    plan.TargetParentA = targetParentA;
    plan.TargetParentW = targetParentW;
    plan.ItemNameA = itemNameA;
    plan.ItemNameW = itemNameW;
    plan.TargetNameA = needsTarget ? targetNameA : itemNameA;
    plan.TargetNameW = needsTarget ? targetNameW : itemNameW;
    plan.SourcePathA = JoinPathA(plan.SourceParentA, plan.ItemNameA);
    plan.SourcePathW = JoinPathW(plan.SourceParentW, plan.ItemNameW);
    if (plan.HasTarget())
    {
        plan.TargetPathA = JoinPathA(plan.TargetParentA, plan.TargetNameA);
        plan.TargetPathW = JoinPathW(plan.TargetParentW, plan.TargetNameW);
    }
    plan.Size = size;
    plan.Attr = attr;
    plan.LastWrite = lastWrite;

    return true;
}

inline bool TryPlanSnapshotItem(const CSelectionSnapshot& snapshot,
                                const CBuildConfig& config,
                                const CSnapshotItem& item,
                                CPlannedSnapshotItem& plan)
{
    plan = CPlannedSnapshotItem{};

    if (snapshot.Action != EActionType::Copy &&
        snapshot.Action != EActionType::Move &&
        snapshot.Action != EActionType::Delete &&
        snapshot.Action != EActionType::Convert &&
        snapshot.Action != EActionType::RecursiveConvert &&
        snapshot.Action != EActionType::ChangeAttrs && // P5 (file-only)
        snapshot.Action != EActionType::ChangeCase)    // P5 (file-only)
    {
        return false;
    }

    std::wstring sourcePathW = SnapshotPathW(snapshot.SourcePath, snapshot.SourcePathW);
    std::string sourcePathA;
    if (sourcePathW.empty() || !SnapshotPathA(snapshot.SourcePath, sourcePathW, sourcePathA))
        return false;

    std::wstring targetPathW;
    std::string targetPathA;
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        targetPathW = SnapshotPathW(snapshot.TargetPath, snapshot.TargetPathW);
        if (targetPathW.empty() || !SnapshotPathA(snapshot.TargetPath, targetPathW, targetPathA))
            return false;
        if (!IsDefaultMask(snapshot.Mask) && !config.EnableExplicitTargetNames)
            return false;
    }

    const std::wstring itemNameW = SnapshotItemNameW(item);
    std::string itemNameA;
    if (itemNameW.empty() || !SnapshotItemNameA(item, itemNameW, itemNameA) || itemNameA.empty())
        return false;

    std::string targetNameA;
    std::wstring targetNameW;
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        targetNameW = SnapshotTargetNameW(item, itemNameW);
        if (!SnapshotTargetNameA(item, targetNameW, itemNameA, targetNameA))
            return false;

        if (!IsDefaultMask(snapshot.Mask) &&
            (!item.HasTargetName || targetNameW.empty() || targetNameA.empty()))
        {
            return false;
        }
    }
    else
    {
        targetNameA = itemNameA;
        targetNameW = itemNameW;
    }

    return TryPlanChildItem(snapshot.Action,
                            sourcePathA, sourcePathW,
                            targetPathA, targetPathW,
                            itemNameA, itemNameW,
                            targetNameA, targetNameW,
                            item.IsDir,
                            item.Size, item.Attr, item.LastWrite,
                            plan);
}

inline bool TryPlanFileOperation(const CSelectionSnapshot& snapshot,
                                 const CBuildConfig& config,
                                 const CSnapshotItem& item,
                                 CPlannedFileOperation& plan)
{
    CPlannedSnapshotItem itemPlan;
    if (!TryPlanSnapshotItem(snapshot, config, item, itemPlan) || itemPlan.IsDir)
        return false;
    plan = std::move(itemPlan);
    return true;
}

} // namespace sally::operation_planner
