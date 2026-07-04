// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ftp_diskwork_core.h"

CFTPDiskWork::CFTPDiskWork()
{
    Reset();
}

void CFTPDiskWork::Reset()
{
    SocketMsg = 0;
    SocketUID = 0;
    MsgID = 0;

    Type = fdwtNone;

    Path.Clear();
    Name.Clear();

    ForceAction = fqiaNone;
    AlreadyRenamedName = FALSE;

    CannotCreateDir = 0;
    DirAlreadyExists = 0;
    CannotCreateFile = 0;
    FileAlreadyExists = 0;
    RetryOnCreatedFile = 0;
    RetryOnResumedFile = 0;

    CheckFromOffset.Set(0, 0);
    WriteOrReadFromOffset.Set(0, 0);
    FlushDataBuffer = NULL;
    ValidBytesInFlushDataBuffer = 0;
    EOLsInFlushDataBuffer = 0;
    WorkFile = NULL;

    ProblemID = ITEMPR_OK;
    WinError = NO_ERROR;
    State = sqisNone;
    NewTgtName = NULL;
    OpenedFile = NULL;
    FileSize.Set(0, 0);
    CanOverwrite = FALSE;
    CanDeleteEmptyFile = FALSE;
    DiskListing = NULL;
}

void CFTPDiskWork::CopyFrom(CFTPDiskWork* work)
{
    if (this == work)
        return;

    SocketMsg = work->SocketMsg;
    SocketUID = work->SocketUID;
    MsgID = work->MsgID;

    Type = work->Type;

    Path.Clear();
    Path.Assign(work->Path.CStr());
    Name.Clear();
    Name.Assign(work->Name.CStr());

    ForceAction = work->ForceAction;
    AlreadyRenamedName = work->AlreadyRenamedName;

    CannotCreateDir = work->CannotCreateDir;
    DirAlreadyExists = work->DirAlreadyExists;
    CannotCreateFile = work->CannotCreateFile;
    FileAlreadyExists = work->FileAlreadyExists;
    RetryOnCreatedFile = work->RetryOnCreatedFile;
    RetryOnResumedFile = work->RetryOnResumedFile;

    CheckFromOffset = work->CheckFromOffset;
    WriteOrReadFromOffset = work->WriteOrReadFromOffset;
    FlushDataBuffer = work->FlushDataBuffer;
    ValidBytesInFlushDataBuffer = work->ValidBytesInFlushDataBuffer;
    EOLsInFlushDataBuffer = work->EOLsInFlushDataBuffer;
    WorkFile = work->WorkFile;

    ProblemID = work->ProblemID;
    WinError = work->WinError;
    State = work->State;
    NewTgtName = work->NewTgtName;
    OpenedFile = work->OpenedFile;
    FileSize = work->FileSize;
    CanOverwrite = work->CanOverwrite;
    CanDeleteEmptyFile = work->CanDeleteEmptyFile;
    DiskListing = work->DiskListing;
}

void FTPPrepareCreateAndWriteFileDiskWork(CFTPDiskWork& work, int socketMsg, int socketUID,
                                          DWORD msgID, const char* targetFileName,
                                          HANDLE workFile, char* flushDataBuffer,
                                          int validBytesInFlushDataBuffer)
{
    work.SocketMsg = socketMsg;
    work.SocketUID = socketUID;
    work.MsgID = msgID;
    work.Type = fdwtCreateAndWriteFile;

    work.Path.Clear();
    work.Name.Clear();
    work.Name.Assign(targetFileName != NULL ? targetFileName : "");

    work.ForceAction = fqiaNone;
    work.AlreadyRenamedName = FALSE;
    work.CannotCreateDir = 0;
    work.DirAlreadyExists = 0;
    work.CannotCreateFile = 0;
    work.FileAlreadyExists = 0;
    work.RetryOnCreatedFile = 0;
    work.RetryOnResumedFile = 0;

    work.CheckFromOffset.Set(0, 0);
    work.WriteOrReadFromOffset.Set(0, 0);
    work.FlushDataBuffer = flushDataBuffer;
    work.ValidBytesInFlushDataBuffer = validBytesInFlushDataBuffer;
    work.EOLsInFlushDataBuffer = 0;
    work.WorkFile = workFile;

    work.ProblemID = ITEMPR_OK;
    work.WinError = NO_ERROR;
    work.State = sqisNone;
    work.NewTgtName = NULL;
    work.OpenedFile = NULL;
    work.FileSize.Set(0, 0);
    work.CanOverwrite = FALSE;
    work.CanDeleteEmptyFile = FALSE;
    work.DiskListing = NULL;
}

void FTPExecuteCreateAndWriteFileDiskWork(CFTPDiskWork& localWork, BOOL& needCopyBack,
                                          BOOL& workDone)
{
    HANDLE file = NULL;
    if (localWork.WorkFile == NULL) // the file has not been created yet
    {
        SetFileAttributes(localWork.Name, FILE_ATTRIBUTE_NORMAL); // to allow overwriting a read-only file as well
        HANDLE f = CreateFile(localWork.Name, GENERIC_WRITE,
                              FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS,
                              FILE_FLAG_SEQUENTIAL_SCAN,
                              NULL);
        if (f != INVALID_HANDLE_VALUE)
        {
            file = f;
            localWork.OpenedFile = f;
            workDone = TRUE;     // if cancelled, close the file handle and delete the file
            needCopyBack = TRUE; // return the handle of the created file
        }
        else // error while creating the file
        {
            localWork.State = sqisFailed;
            localWork.WinError = GetLastError();
            needCopyBack = TRUE; // return the error
        }
    }
    else
        file = localWork.WorkFile; // write only

    if (file != NULL && localWork.ValidBytesInFlushDataBuffer > 0) // write to the file
    {
        DWORD writtenBytes;
        if (!WriteFile(file, localWork.FlushDataBuffer, localWork.ValidBytesInFlushDataBuffer,
                       &writtenBytes, NULL) ||
            writtenBytes != (DWORD)localWork.ValidBytesInFlushDataBuffer)
        {
            localWork.State = sqisFailed;
            localWork.WinError = GetLastError();
            needCopyBack = TRUE; // return the error
        }
        // else;  // successfully written, we are successfully done
    }
}
