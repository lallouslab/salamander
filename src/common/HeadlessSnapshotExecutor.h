// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// HeadlessSnapshotExecutor - executes CSelectionSnapshot through production
// planner/executor seams without a progress dialog or panel UI.

#pragma once

#include "CBuildConfig.h"
#include "CSelectionSnapshot.h"
#include "HeadlessFileOperationExecutor.h"
#include "IWorkerObserver.h"

#include <windows.h>

#include <cstddef>

namespace sally::operation_executor
{

struct CSnapshotExecutionResult
{
    bool success = false;
    int completedItems = 0;
    DWORD lastError = ERROR_SUCCESS;
};

inline CSnapshotExecutionResult ExecuteSnapshotHeadless(
    IWorkerObserver& observer,
    const CSelectionSnapshot& snapshot,
    const CBuildConfig& config,
    CFileOperationExecutionState& state,
    bool signalDone = true)
{
    CSnapshotExecutionResult result;
    observer.SetProgress(0, 0);

    for (size_t i = 0; !observer.IsCancelled() && i < snapshot.Items.size(); ++i)
    {
        sally::operation_planner::CPlannedSnapshotItem plan;
        if (!sally::operation_planner::TryPlanSnapshotItem(snapshot, config, snapshot.Items[i], plan))
        {
            result.lastError = ERROR_INVALID_PARAMETER;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        CFileOperationResult opResult = ExecutePlannedOperation(observer, plan, state);
        if (!opResult.success)
        {
            result.lastError = opResult.lastError;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        ++result.completedItems;
        const int progress = snapshot.Items.empty()
                                 ? 1000
                                 : (int)(((i + 1) * 1000) / snapshot.Items.size());
        observer.SetProgress(0, progress);
    }

    if (observer.IsCancelled())
    {
        result.lastError = ERROR_CANCELLED;
        observer.SetError(true);
        if (signalDone)
            observer.NotifyDone();
        return result;
    }

    result.success = true;
    observer.SetProgress(0, 1000);
    observer.SetError(false);
    if (signalDone)
        observer.NotifyDone();
    return result;
}

} // namespace sally::operation_executor
