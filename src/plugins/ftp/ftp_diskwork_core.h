// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

struct CFTPDiskWork;

void FTPPrepareCreateAndWriteFileDiskWork(CFTPDiskWork& work, int socketMsg, int socketUID,
                                          DWORD msgID, const char* targetFileName,
                                          HANDLE workFile, char* flushDataBuffer,
                                          int validBytesInFlushDataBuffer);
void FTPExecuteCreateAndWriteFileDiskWork(CFTPDiskWork& localWork, BOOL& needCopyBack,
                                          BOOL& workDone);
