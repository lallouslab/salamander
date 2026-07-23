// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ExternalToolRunner.h"
#include "IShell.h"

#include <windows.h>

struct ViewerEditorProcessLaunchRequest
{
    const wchar_t* commandLine;
    const wchar_t* workingDirectory;
    DWORD creationFlags;
    bool useShowWindow;
    WORD showWindow;
    bool usePosition;
    DWORD x;
    DWORD y;
    bool useSize;
    DWORD width;
    DWORD height;

    ViewerEditorProcessLaunchRequest()
        : commandLine(nullptr)
        , workingDirectory(nullptr)
        , creationFlags(NORMAL_PRIORITY_CLASS)
        , useShowWindow(false)
        , showWindow(SW_SHOWNORMAL)
        , usePosition(false)
        , x(0)
        , y(0)
        , useSize(false)
        , width(0)
        , height(0)
    {
    }
};

class IViewerEditorLauncher
{
public:
    virtual ~IViewerEditorLauncher() = default;

    virtual ExternalToolResult LaunchProcess(const ViewerEditorProcessLaunchRequest& request) = 0;
    virtual ShellExecResult OpenFileWithShell(HWND hwnd, const wchar_t* path, int showCommand) = 0;
};

class CViewerEditorLauncher : public IViewerEditorLauncher
{
public:
    CViewerEditorLauncher(IExternalToolRunner* runner = nullptr, IShell* shell = nullptr);

    ExternalToolResult LaunchProcess(const ViewerEditorProcessLaunchRequest& request) override;
    ShellExecResult OpenFileWithShell(HWND hwnd, const wchar_t* path, int showCommand) override;

private:
    IExternalToolRunner* ResolveRunner() const;
    IShell* ResolveShell() const;

    IExternalToolRunner* Runner;
    IShell* Shell;
};

extern IViewerEditorLauncher* gViewerEditorLauncher;
