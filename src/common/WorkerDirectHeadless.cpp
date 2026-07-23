// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "worker.h"
#include "common/HeadlessScriptExecutor.h"
#include "common/WorkerDirectHeadless.h"

namespace sally::worker
{

static bool IsHeadlessDirectOpcodeSupported(COperationCode opcode)
{
    switch (opcode)
    {
    case ocCopyFile:
    case ocMoveFile:
    case ocDeleteFile:
    case ocCreateDir:
    case ocMoveDir:
    case ocDeleteDir:
    case ocDeleteDirLink:
    case ocCopyDirTime:
    case ocLabelForSkipOfCreateDir:
    case ocCountSize:
    case ocChangeAttrs:
    case ocConvert:
        return true;
    default:
        return false;
    }
}

static bool IsHeadlessDirectOperationSupported(const COperation& op)
{
    if (!IsHeadlessDirectOpcodeSupported(op.Opcode))
        return false;

    if (op.Opcode == ocConvert &&
        (op.Attr & (FILE_ATTRIBUTE_DIRECTORY |
                    FILE_ATTRIBUTE_REPARSE_POINT |
                    FILE_ATTRIBUTE_COMPRESSED |
                    FILE_ATTRIBUTE_ENCRYPTED |
                    FILE_ATTRIBUTE_SPARSE_FILE)) != 0)
    {
        return false;
    }

    const DWORD unsupportedFlags = OPFL_AS_ENCRYPTED;
    return (op.OpFlags & unsupportedFlags) == 0;
}

bool CanRunWorkerDirectHeadless(const COperations& script,
                                const CChangeAttrsData* attrsData,
                                const CConvertData* convertData)
{
    bool hasConvert = false;
    for (int i = 0; i < script.Count; ++i)
    {
        if (script.At(i).Opcode == ocConvert)
            hasConvert = true;
        if (!IsHeadlessDirectOperationSupported(script.At(i)))
            return false;
    }
    if (hasConvert && convertData == NULL)
        return false;
    return true;
}

BOOL RunWorkerDirectHeadless(COperations* script,
                             IWorkerObserver& observer,
                             CChangeAttrsData* attrsData,
                             CConvertData* convertData)
{
    if (script == NULL || !CanRunWorkerDirectHeadless(*script, attrsData, convertData))
    {
        observer.SetError(true);
        observer.NotifyDone();
        return FALSE;
    }

    if (script->TotalSize == CQuadWord(0, 0))
        script->TotalSize = CQuadWord(1, 0);

    sally::operation_executor::CFileOperationExecutionState state;
    sally::operation_executor::CScriptExecutionOptions options;
    options.AttrsData = attrsData;
    options.ConvertData = convertData;
    options.CopySecurity = script->CopySecurity;
    sally::operation_executor::CScriptExecutionResult result =
        sally::operation_executor::ExecuteOperationsHeadlessWithOptions(observer, *script, state, options);
    return result.success ? TRUE : FALSE;
}

} // namespace sally::worker
