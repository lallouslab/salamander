// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IEnvironment.h"
#include "IProcess.h"

#include <string>
#include <utility>
#include <windows.h>

struct CommandShellRequest
{
    const wchar_t* comspec;
    const wchar_t* workingDirectory;
    const wchar_t* command;
    const wchar_t* windowTitle;
    bool keepOpen;
    bool unicodeOutput;
    bool inheritHandles;
    bool createNewConsole;
    bool hideWindow;
    DWORD creationFlags;
    bool useShowWindow;
    WORD showWindow;
    bool usePosition;
    DWORD x;
    DWORD y;

    CommandShellRequest()
        : comspec(nullptr)
        , workingDirectory(nullptr)
        , command(nullptr)
        , windowTitle(nullptr)
        , keepOpen(true)
        , unicodeOutput(false)
        , inheritHandles(false)
        , createNewConsole(false)
        , hideWindow(false)
        , creationFlags(CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS)
        , useShowWindow(false)
        , showWindow(SW_SHOWNORMAL)
        , usePosition(false)
        , x(0)
        , y(0)
    {
    }
};

struct CommandShellResult
{
    bool success;
    DWORD errorCode;
    HPROCESS process;
    DWORD processId;
    IProcess* processOwner;

    static CommandShellResult Ok(HPROCESS process, DWORD processId, IProcess* processOwner)
    {
        return {true, ERROR_SUCCESS, process, processId, processOwner};
    }

    static CommandShellResult Error(DWORD errorCode)
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

struct CommandShellPolicyInfo
{
    std::wstring quotedComspec;
    std::string executableNameForPolicy;
};

struct CommandShellPolicyResult
{
    bool success;
    DWORD errorCode;
    CommandShellPolicyInfo info;

    static CommandShellPolicyResult Ok(CommandShellPolicyInfo info)
    {
        return {true, ERROR_SUCCESS, std::move(info)};
    }

    static CommandShellPolicyResult Error(DWORD errorCode)
    {
        return {false, errorCode, CommandShellPolicyInfo{}};
    }
};

class ICommandShellService
{
public:
    virtual ~ICommandShellService() = default;

    virtual CommandShellResult LaunchShell(const CommandShellRequest& request) = 0;
    virtual CommandShellResult LaunchCommand(const CommandShellRequest& request) = 0;
    virtual CommandShellPolicyResult GetPolicyInfo(const CommandShellRequest& request) = 0;
};

class CCommandShellService : public ICommandShellService
{
public:
    CCommandShellService(IProcess* process = nullptr, IEnvironment* environment = nullptr);

    CommandShellResult LaunchShell(const CommandShellRequest& request) override;
    CommandShellResult LaunchCommand(const CommandShellRequest& request) override;
    CommandShellPolicyResult GetPolicyInfo(const CommandShellRequest& request) override;

private:
    IProcess* ResolveProcess() const;
    IEnvironment* ResolveEnvironment() const;
    CommandShellResult LaunchBuiltCommand(const CommandShellRequest& request,
                                          const std::wstring& commandLine);
    bool ResolveComspec(const CommandShellRequest& request, std::wstring& comspec,
                        DWORD& errorCode) const;

    IProcess* Process;
    IEnvironment* Environment;
};

extern ICommandShellService* gCommandShellService;
