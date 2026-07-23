// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// WorkerDirectHeadless - linkable headless worker-direct entrypoint for the
// COperations script subset covered by HeadlessScriptExecutor.

#pragma once

#include <windows.h>

class COperations;
class IWorkerObserver;
struct CChangeAttrsData;
struct CConvertData;

namespace sally::worker
{

bool CanRunWorkerDirectHeadless(const COperations& script,
                                const CChangeAttrsData* attrsData = NULL,
                                const CConvertData* convertData = NULL);

BOOL RunWorkerDirectHeadless(COperations* script,
                             IWorkerObserver& observer,
                             CChangeAttrsData* attrsData = NULL,
                             CConvertData* convertData = NULL);

} // namespace sally::worker
