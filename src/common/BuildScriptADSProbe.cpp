// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "worker.h"
#include "common/BuildScript.h"

BOOL BuildScriptLegacyADSProbe(const char* sourceNameA,
                               const wchar_t* sourceNameW,
                               BOOL isDir,
                               DWORD bytesPerCluster,
                               CBuildADSProbeResult* result,
                               void* context)
{
    (void)context;
    if (sourceNameA == NULL || result == NULL)
        return FALSE;

    CQuadWord adsSize(0, 0);
    CQuadWord adsOccupiedSpace(0, 0);
    DWORD winError = NO_ERROR;
    BOOL onlyDiscardableStreams = FALSE;
    const std::wstring sourceWide = sourceNameW != NULL ? sourceNameW : L"";
    const BOOL hasADS = CheckFileOrDirADS(sourceNameA, isDir, &adsSize, NULL, NULL, NULL,
                                          &winError, bytesPerCluster,
                                          &adsOccupiedSpace, &onlyDiscardableStreams,
                                          sourceWide);

    result->HasADS = hasADS != FALSE;
    result->HasProbeError = winError != NO_ERROR;
    result->Size = adsSize.Value;
    result->OccupiedSpace = adsOccupiedSpace.Value;
    result->WinError = winError;
    result->OnlyDiscardableStreams = onlyDiscardableStreams;
    return TRUE;
}
