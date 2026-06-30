// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// OperationsCore - linkable COperations/COperation pieces used by the
// standalone script builder and headless integration executors.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "worker.h"
#include "common/widepath.h"
#include "common/unicode/PathIdentityPolicy.h"
#include "common/unicode/PanelPathPolicy.h"

//
// ****************************************************************************
// CTransferSpeedMeter
//

CTransferSpeedMeter::CTransferSpeedMeter()
{
    Clear();
}

void CTransferSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    ResetSpeed = TRUE;
    MaxPacketSize = 0;
}

//
// ****************************************************************************
// CProgressSpeedMeter
//

CProgressSpeedMeter::CProgressSpeedMeter()
{
    Clear();
}

void CProgressSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    MaxPacketSize = 0;
}

void COperation::PopulateWidePathsFromAnsi()
{
    // Widen SourceName if owned and not already set
    if (OwnsSourceName && SourceName != NULL && SourceNameW.empty())
    {
        wchar_t* wide = SalAllocWidePath(SourceName);
        if (wide != NULL)
        {
            SourceNameW = wide;
            SalFreeWidePath(wide);
        }
    }

    // Widen TargetName if owned and not already set
    if (OwnsTargetName && TargetName != NULL && TargetNameW.empty())
    {
        wchar_t* wide = SalAllocWidePath(TargetName);
        if (wide != NULL)
        {
            TargetNameW = wide;
            SalFreeWidePath(wide);
        }
    }
}

static std::wstring BuildOperationNameW(std::wstring widePath, const std::wstring& wideFileName)
{
    if (!wideFileName.empty())
    {
        if (!widePath.empty() && widePath.back() != L'\\')
            widePath += L'\\';
        widePath += wideFileName;
    }

    if (widePath.length() >= SAL_LONG_PATH_THRESHOLD &&
        widePath.compare(0, 4, L"\\\\?\\") != 0)
    {
        if (widePath.length() >= 2 && widePath[0] == L'\\' && widePath[1] == L'\\')
            widePath = L"\\\\?\\UNC\\" + widePath.substr(2);
        else
            widePath = L"\\\\?\\" + widePath;
    }

    return widePath;
}

void COperations::ReanchorWideSourcePaths(const char* anchorAnsi, const wchar_t* anchorWide)
{
    for (size_t i = 0; i < m_ops.size(); ++i)
    {
        COperation& op = m_ops[i];
        if (!op.OwnsSourceName || op.SourceName == NULL || op.SourceNameWExplicit)
            continue;

        std::wstring rebound = sally::unicode::RebindAnsiPathToWideAnchor(
            op.SourceName, anchorAnsi, anchorWide);
        if (rebound.empty())
            continue;

        op.SourceNameW = BuildOperationNameW(std::move(rebound), std::wstring());
        op.SourceNameWExplicit = true;
    }
}

void COperation::SetSourceNameW(const char* ansiPath, const std::wstring& wideFileName)
{
    if (ansiPath == NULL)
        return;

    // Convert directory path to wide
    int pathLen = MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, NULL, 0);
    if (pathLen == 0)
        return;

    std::wstring widePath;
    widePath.resize(pathLen);
    MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, &widePath[0], pathLen);
    widePath.resize(pathLen - 1);  // Remove null terminator from size

    SetSourceNameW(widePath, wideFileName);
}

void COperation::SetTargetNameW(const char* ansiPath, const std::wstring& wideFileName)
{
    if (ansiPath == NULL)
        return;

    // Convert directory path to wide
    int pathLen = MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, NULL, 0);
    if (pathLen == 0)
        return;

    std::wstring widePath;
    widePath.resize(pathLen);
    MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, &widePath[0], pathLen);
    widePath.resize(pathLen - 1);  // Remove null terminator from size

    SetTargetNameW(widePath, wideFileName);
}

void COperation::SetSourceNameW(const std::wstring& widePath, const std::wstring& wideFileName)
{
    SourceNameW = BuildOperationNameW(widePath, wideFileName);
    SourceNameWExplicit = true;
}

void COperation::SetTargetNameW(const std::wstring& widePath, const std::wstring& wideFileName)
{
    TargetNameW = BuildOperationNameW(widePath, wideFileName);
    TargetNameWExplicit = true;
}

BOOL COperation::AreSourceAndTargetExactlySamePath() const
{
    return sally::unicode::ArePathsExactlySame(SourceName, TargetName, SourceNameW, TargetNameW);
}

// Case-insensitive comparison of source and target paths - uses wide paths if both available
BOOL COperation::AreSourceAndTargetSamePath() const
{
    return sally::unicode::ArePathsEquivalentForCopy(SourceName, TargetName, SourceNameW, TargetNameW);
}

//
// ****************************************************************************
// COperations
//

COperations::COperations(int base, int delta, const char* waitInQueueSubject, const char* waitInQueueFrom,
                         const char* waitInQueueTo) : Sizes(1, 400), Count(0)
{
    TotalSize = CQuadWord(0, 0);
    CompressedSize = CQuadWord(0, 0);
    OccupiedSpace = CQuadWord(0, 0);
    TotalFileSize = CQuadWord(0, 0);
    FreeSpace = CQuadWord(0, 0);
    BytesPerCluster = 0;
    ClearReadonlyMask = 0xFFFFFFFF;
    InvertRecycleBin = FALSE;
    CanUseRecycleBin = TRUE;
    SameRootButDiffVolume = FALSE;
    TargetPathSupADS = FALSE;
    //  TargetPathSupEFS = FALSE;
    IsCopyOrMoveOperation = FALSE;
    OverwriteOlder = FALSE;
    CopySecurity = FALSE;
    PreserveDirTime = FALSE;
    SourcePathIsNetwork = FALSE;
    CopyAttrs = FALSE;
    StartOnIdle = FALSE;
    ShowStatus = FALSE;
    IsCopyOperation = FALSE;
    FastMoveUsed = FALSE;
    ChangeSpeedLimit = FALSE;
    FilesCount = 0;
    DirsCount = 0;
    RemapNameFrom = NULL;
    RemapNameFromLen = 0;
    RemapNameTo = NULL;
    RemapNameToLen = 0;
    RemovableTgtDisk = FALSE;
    RemovableSrcDisk = FALSE;
    SkipAllCountSizeErrors = FALSE;
    WorkPath1[0] = 0;
    WorkPath1InclSubDirs = FALSE;
    WorkPath2[0] = 0;
    WorkPath2InclSubDirs = FALSE;
    WaitInQueueSubject = waitInQueueSubject ? waitInQueueSubject : "";
    WaitInQueueFrom = waitInQueueFrom ? waitInQueueFrom : "";
    WaitInQueueTo = waitInQueueTo ? waitInQueueTo : "";
    HANDLES(InitializeCriticalSection(&StatusCS));
    TransferredFileSize = CQuadWord(0, 0);
    ProgressSize = CQuadWord(0, 0);
    UseSpeedLimit = FALSE;
    SpeedLimit = 1;
    SleepAfterWrite = -1;
    LastBufferLimit = 1;
    LastSetupTime = GetTickCount();
    BytesTrFromLastSetup = CQuadWord(0, 0);
    UseProgressBufferLimit = FALSE;
    ProgressBufferLimit = ASYNC_SLOW_COPY_BUF_SIZE;
    LastProgBufLimTestTime = GetTickCount() - 1000;
    LastFileBlockCount = 0;
    LastFileStartTime = GetTickCount();
}

void COperations::SetSpeedLimit(BOOL useSpeedLimit, DWORD speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    UseSpeedLimit = useSpeedLimit;
    SpeedLimit = speedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}
