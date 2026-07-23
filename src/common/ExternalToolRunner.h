// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IProcess.h"

#include <windows.h>

struct ExternalToolRequest
{
    const wchar_t* commandLine;
    const wchar_t* workingDirectory;
    bool inheritHandles;
    bool createNewConsole;
    bool hideWindow;
    DWORD creationFlags;
    const wchar_t* windowTitle;
    bool useShowWindow;
    WORD showWindow;
    bool usePosition;
    DWORD x;
    DWORD y;
    bool useSize;
    DWORD width;
    DWORD height;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;

    ExternalToolRequest()
        : commandLine(nullptr)
        , workingDirectory(nullptr)
        , inheritHandles(false)
        , createNewConsole(false)
        , hideWindow(false)
        , creationFlags(CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS)
        , windowTitle(nullptr)
        , useShowWindow(false)
        , showWindow(SW_SHOWNORMAL)
        , usePosition(false)
        , x(0)
        , y(0)
        , useSize(false)
        , width(0)
        , height(0)
        , hStdInput(nullptr)
        , hStdOutput(nullptr)
        , hStdError(nullptr)
    {
    }
};

struct ExternalToolResult
{
    bool success;
    DWORD errorCode;
    HPROCESS process;
    DWORD processId;
    IProcess* processOwner;

    static ExternalToolResult Ok(HPROCESS process, DWORD processId, IProcess* processOwner)
    {
        return {true, ERROR_SUCCESS, process, processId, processOwner};
    }

    static ExternalToolResult Error(DWORD errorCode)
    {
        return {false, errorCode, INVALID_HPROCESS, 0, nullptr};
    }

    void CloseProcess()
    {
        if (processOwner != nullptr && process != INVALID_HPROCESS)
        {
            processOwner->CloseProcess(process);
            process = INVALID_HPROCESS;
        }
    }

    HANDLE DetachNativeProcessHandle()
    {
        if (processOwner == nullptr || process == INVALID_HPROCESS)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }

        HANDLE nativeHandle = processOwner->DetachProcessHandle(process);
        if (nativeHandle != NULL)
            process = INVALID_HPROCESS;
        return nativeHandle;
    }
};

class IExternalToolRunner
{
public:
    virtual ~IExternalToolRunner() = default;

    virtual ExternalToolResult Launch(const ExternalToolRequest& request) = 0;
};

class CExternalToolRunner : public IExternalToolRunner
{
public:
    explicit CExternalToolRunner(IProcess* process = nullptr);

    ExternalToolResult Launch(const ExternalToolRequest& request) override;

private:
    IProcess* ResolveProcess() const;

    IProcess* Process;
};

extern IExternalToolRunner* gExternalToolRunner;
