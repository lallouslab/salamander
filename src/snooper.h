// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CFilesWindow;

extern HANDLE RefreshFinishedEvent;
extern int SnooperSuspended;

void AddDirectory(CFilesWindow* win, const char* path, BOOL registerDevNotification);                           // new directory for snooper
void ChangeDirectory(CFilesWindow* win, const char* newPath, BOOL registerDevNotification);                     // change of specified directory

// Wide siblings of AddDirectory / ChangeDirectory. Use these when the panel's
// wide path cache is populated (sally::unicode::HasWidePathW(panel->GetPathW()))
// so that FindFirstChangeNotificationW receives the real Unicode bytes instead
// of the lossy CP_ACP mirror — otherwise background change notifications stop
// working for paths outside the active code page.
void AddDirectoryW(CFilesWindow* win, const wchar_t* pathW, BOOL registerDevNotification);
void ChangeDirectoryW(CFilesWindow* win, const wchar_t* newPathW, BOOL registerDevNotification);

void DetachDirectory(CFilesWindow* win, BOOL waitForHandleClosure = FALSE, BOOL closeDevNotifification = TRUE); // no longer need to snoop

BOOL InitializeThread();
void TerminateThread();

void BeginSuspendMode(BOOL debugDoNotTestCaller = FALSE);
void EndSuspendMode(BOOL debugDoNotTestCaller = FALSE);

typedef TDirectArray<CFilesWindow*> CWindowArray; // (CFilesWindow *)
typedef TDirectArray<HANDLE> CObjectArray;        // (HANDLE)

extern CWindowArray WindowArray; // arrays indexed the same way
extern CObjectArray ObjectArray; // ObjectHandle belongs to MainWindow
