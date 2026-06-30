// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// HeadlessScriptExecutor - executes a COperations script through the
// UI-independent file-operation executor and IWorkerObserver.

#pragma once

#include "HeadlessFileOperationExecutor.h"

#include <windows.h>

class COperations;
class IWorkerObserver;
struct CChangeAttrsData;
struct CConvertData;

namespace sally::operation_executor
{

struct CScriptExecutionResult
{
    bool success = false;
    int completedOperations = 0;
    DWORD lastError = ERROR_SUCCESS;
};

struct CScriptExecutionOptions
{
    const CChangeAttrsData* AttrsData = NULL;
    const CConvertData* ConvertData = NULL;
    bool CopySecurity = false;
};

CScriptExecutionResult ExecuteOperationsHeadless(IWorkerObserver& observer,
                                                 COperations& script,
                                                 CFileOperationExecutionState& state,
                                                 bool signalDone = true);

CScriptExecutionResult ExecuteOperationsHeadlessWithOptions(IWorkerObserver& observer,
                                                            COperations& script,
                                                            CFileOperationExecutionState& state,
                                                            const CScriptExecutionOptions& options,
                                                            bool signalDone = true);

} // namespace sally::operation_executor
