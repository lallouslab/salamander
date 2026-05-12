// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "dialogs.h"
#include "snooper.h"
#include "worker.h"
#include "cache.h"
#include "usermenu.h"
#include "execute.h"
#include "pack.h"
#include "viewer.h"
#include "codetbl.h"
#include "find.h"
#include "menu.h"
#include "common/widepath.h"
#include "common/fsutil.h"
#include "common/CreateDirectoryFlow.h"
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/unicode/AnsiFallbackPolicy.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/NameFallbackRecovery.h"
#include "common/unicode/PathIdentityPolicy.h"
#include "common/unicode/RenameRetryPolicy.h"
#include <vector>

namespace
{
bool WideStringUsesAnsiFallback(const std::wstring& value)
{
    return sally::unicode::WideStringRequiresWidePath(value) ? TRUE : FALSE;
}

bool TryGetAnsiEditorLaunchPath(const std::wstring& pathW, char* pathA, int pathASize)
{
    if (pathW.empty() || pathA == NULL || pathASize <= 0)
        return false;

    if (!WideStringUsesAnsiFallback(pathW))
    {
        WideToAnsi(pathW, pathA, pathASize);
        return pathA[0] != 0;
    }

    std::wstring shortPathW = GetShortPathW(pathW.c_str());
    if (shortPathW.empty() || WideStringUsesAnsiFallback(shortPathW))
        return false;

    WideToAnsi(shortPathW, pathA, pathASize);
    return pathA[0] != 0;
}

BOOL FileNameInvalidForManualCreateW(const wchar_t* path)
{
    const wchar_t* name = wcsrchr(path, L'\\');
    if (name != NULL)
    {
        name++;
        int nameLen = (int)wcslen(name);
        return nameLen > 0 && (*name <= L' ' || name[nameLen - 1] <= L' ' || name[nameLen - 1] == L'.');
    }
    return FALSE;
}

void RepairLossyQuickRenameHistoryForCurrentName(wchar_t* historyW[], int historyCount,
                                                 const char* currentAnsiName, const wchar_t* currentWideName)
{
    if (historyW == NULL || historyCount <= 0 || currentAnsiName == NULL || currentWideName == NULL || currentWideName[0] == L'\0')
        return;

    std::wstring ansiNameW = AnsiToWide(currentAnsiName);
    if (ansiNameW.empty())
        return;

    for (int i = 0; i < historyCount; i++)
    {
        wchar_t* item = historyW[i];
        if (item == NULL || item[0] == L'\0' || wcschr(item, L'?') == NULL)
            continue;

        std::wstring repaired = sally::unicode::RecoverWideCharsFromLossyInput(item, ansiNameW, currentWideName);
        if (repaired.empty() || wcscmp(item, repaired.c_str()) == 0)
            continue;

        wchar_t* updated = (wchar_t*)malloc((repaired.length() + 1) * sizeof(wchar_t));
        if (updated == NULL)
            continue;

        memcpy(updated, repaired.c_str(), (repaired.length() + 1) * sizeof(wchar_t));
        free(historyW[i]);
        historyW[i] = updated;
    }
}
} // namespace

//
// ****************************************************************************
// CFilesWindow
//

void CFilesWindow::Convert()
{
    CALL_STACK_MESSAGE1("CFilesWindow::Convert()");
    if (Dirs->Count + Files->Count == 0)
        return;
    BeginStopRefresh(); // snooper takes a break

    if (!FilesActionInProgress)
    {
        FilesActionInProgress = TRUE;

        BOOL subDir;
        if (Dirs->Count > 0)
            subDir = (strcmp(Dirs->At(0).Name, "..") == 0);
        else
            subDir = FALSE;

        CConvertFilesDlg convertDlg(HWindow, SelectionContainsDirectory());
        while (1)
        {
            if (convertDlg.Execute() == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);
                if (convertDlg.CodeType == 0 && convertDlg.EOFType == 0)
                    break; // nothing to do

                CCriteriaData filter;
                filter.UseMasks = TRUE;
                filter.Masks.SetMasksString(convertDlg.Mask);
                int errpos = 0;
                if (!filter.Masks.PrepareMasks(errpos))
                    break; // invalid mask

                if (CheckPath(TRUE) != ERROR_SUCCESS) // the path we need to work on failed
                {
                    FilesActionInProgress = FALSE;
                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                CConvertData dlgData;

                dlgData.EOFType = convertDlg.EOFType;

                // the CodeTables object was initialized in the Convert dialog
                if (!CodeTables.GetCode(dlgData.CodeTable, convertDlg.CodeType))
                {
                    // if we fail to obtain the encoding table or no encoding is selected,
                    // perform one-to-one encoding, i.e. no conversion
                    int i;
                    for (i = 0; i < 256; i++)
                        dlgData.CodeTable[i] = i;
                }

                //---  determine which directories and files are selected
                std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
                CFileData* f = NULL;
                int count = GetSelCount();
                if (count != 0)
                {
                    indexes = std::make_unique<int[]>(count);
                    GetSelItems(count, indexes.get());
                }
                else // nothing is selected
                {
                    int i = GetCaretIndex();
                    if (i < 0 || i >= Dirs->Count + Files->Count)
                    {
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // invalid index (no files)
                    }
                    if (i == 0 && subDir)
                    {
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // we do not work with ".."
                    }
                    f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                }
                //---
                COperations* script = new COperations(1000, 500, NULL, NULL, NULL);
                if (script == NULL)
                    TRACE_E(LOW_MEMORY);
                else
                {
                    HWND hFocusedWnd = GetFocus();
                    CreateSafeWaitWindow(LoadStr(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
                    EnableWindow(MainWindow->HWindow, FALSE);

                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    BOOL res = BuildScriptMain(script, convertDlg.SubDirs ? atRecursiveConvert : atConvert,
                                               NULL, NULL, count, indexes.get(), f, NULL, NULL, FALSE, &filter);
                    // Repair only auto-widened source roots that still came from the ANSI
                    // panel cache. Operations with explicit PathW+NameW are left intact.
                    if (res && Is(ptDisk) && sally::unicode::HasWidePathW(GetPathW()))
                        script->ReanchorWideSourcePaths(GetPath(), GetPathW());
                    if (script->Count == 0)
                        res = FALSE;
                    // reordered to allow the main window to activate (must not be disabled), otherwise it switches to another app
                    EnableWindow(MainWindow->HWindow, TRUE);
                    DestroySafeWaitWindow();

                    // if Salamander is active, call SetFocus on the stored window (SetFocus fails
                    // if the main window is disabled - after deactivation/activation of the disabled main window the active panel
                    // does not have focus)
                    HWND hwnd = GetForegroundWindow();
                    while (hwnd != NULL && hwnd != MainWindow->HWindow)
                        hwnd = GetParent(hwnd);
                    if (hwnd == MainWindow->HWindow)
                        SetFocus(hFocusedWnd);

                    SetCursor(oldCur);

                    // prepare refresh of manually refreshed directories
                    // change in the directory displayed in the panel and also in subdirectories if work was done there as well
                    script->SetWorkPath1(GetPath(), convertDlg.SubDirs);

                if (!res || !StartProgressDialog(script, LoadStr(IDS_CONVERTTITLE), NULL, &dlgData))
                {
                    if (script->IsGood() && script->Count == 0)
                    {
                        gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_NOFILE_MATCHEDMASK));

                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                    }
                    UpdateWindow(MainWindow->HWindow);
                    if (!script->IsGood())
                        script->ResetState();
                    FreeScript(script);
                }
                    else // removal of selection index (no waiting for operation finish, operation runs in another thread)
                    {
                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                        UpdateWindow(MainWindow->HWindow);
                    }
                }
                // RAII: indexes auto-deleted when scope exits
            }
            UpdateWindow(MainWindow->HWindow);
            break;
        }
        FilesActionInProgress = FALSE;
    }
    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::ChangeAttr(BOOL setCompress, BOOL compressed, BOOL setEncryption, BOOL encrypted)
{
    CALL_STACK_MESSAGE5("CFilesWindow::ChangeAttr(%d, %d, %d, %d)", setCompress, compressed, setEncryption, encrypted);
    if (Dirs->Count + Files->Count == 0)
        return;
    int selectedCount = GetSelCount();
    if (selectedCount == 0 || selectedCount == 1)
    {
        int index;
        if (selectedCount == 0)
            index = GetCaretIndex();
        else
            GetSelItems(1, &index);
        // focus is on UpDir -- nothing to convert
        if (Dirs->Count > 0 && index == 0 && strcmp(Dirs->At(0).Name, "..") == 0)
            return;
    }
    BeginStopRefresh(); // snooper takes a break

    // if no item is selected, select the one under focus and store its name
    CPathBuffer temporarySelected; // Heap-allocated for long path support
    temporarySelected[0] = 0;
    if ((!setCompress || Configuration.CnfrmNTFSPress) &&
        (!setEncryption || Configuration.CnfrmNTFSCrypt))
    {
        SelectFocusedItemAndGetName(temporarySelected, temporarySelected.Size());
    }

    if (Is(ptDisk))
    {
        if (!FilesActionInProgress)
        {
            FilesActionInProgress = TRUE;

            BOOL subDir;
            if (Dirs->Count > 0)
                subDir = (strcmp(Dirs->At(0).Name, "..") == 0);
            else
                subDir = FALSE;

            DWORD attr, attrDiff;
            SYSTEMTIME timeModified;
            SYSTEMTIME timeCreated;
            SYSTEMTIME timeAccessed;
            if (!setCompress && !setEncryption)
            {
                int count = GetSelCount();
                if (count == 1 || count == 0)
                {
                    int index;
                    if (count == 0)
                        index = GetCaretIndex();
                    else
                        GetSelItems(1, &index);
                    if (index >= 0 && index < Files->Count + Dirs->Count)
                    {
                        CFileData* f = (index < Dirs->Count) ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                        if (strcmp(f->Name, "..") != 0)
                        {
                            BOOL isDir = index < Dirs->Count;

                            BOOL timeObtained = FALSE;

                            // retrieve the file times using decoupled helper
                            std::wstring fullPath = BuildPathW(GetPath(), f->Name);
                            SalFileInfo fileInfo = GetFileInfoW(fullPath.c_str());
                            if (fileInfo.IsValid)
                            {
                                FILETIME ft;
                                if (FileTimeToLocalFileTime(&fileInfo.CreationTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeCreated) &&
                                    FileTimeToLocalFileTime(&fileInfo.LastAccessTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeAccessed) &&
                                    FileTimeToLocalFileTime(&fileInfo.LastWriteTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeModified))
                                {
                                    timeObtained = TRUE;
                                }
                            }
                            if (!timeObtained)
                            {
                                // if we failed to obtain the time from the file, use at least the one we have
                                FILETIME ft;
                                if (!FileTimeToLocalFileTime(&f->LastWrite, &ft) ||
                                    !FileTimeToSystemTime(&ft, &timeModified))
                                {
                                    GetLocalTime(&timeModified); // the time we have is invalid, use the current time
                                }
                                timeCreated = timeModified;
                                timeAccessed = timeModified;
                            }

                            attr = f->Attr;
                            attrDiff = 0;
                            count = -1;
                        }
                    }
                }
                if (count != -1)
                {
                    GetLocalTime(&timeModified);
                    timeAccessed = timeModified;
                    timeCreated = timeModified;
                    attr = 0;
                    attrDiff = 0;
                    BOOL first = TRUE;

                    int totalCount = Dirs->Count + Files->Count;
                    CFileData* f;
                    int i;
                    for (i = 0; i < totalCount; i++)
                    {
                        BOOL isDir = i < Dirs->Count;
                        f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                        if (i == 0 && isDir && strcmp(Dirs->At(0).Name, "..") == 0)
                            continue;
                        if (f->Selected == 1)
                        {
                            if (first)
                            {
                                attr = f->Attr;
                                first = FALSE;
                            }
                            else
                            {
                                if (f->Attr != attr)
                                    attrDiff |= f->Attr ^ attr;
                            }
                        }
                    }
                }
            }

            CChangeAttrDialog chDlg(HWindow, attr, attrDiff,
                                    SelectionContainsDirectory(), FileBasedCompression,
                                    FileBasedEncryption,
                                    &timeModified, &timeCreated, &timeAccessed);
            if (setCompress || setEncryption)
            {
                chDlg.Archive = 2;
                chDlg.ReadOnly = 2;
                chDlg.Hidden = 2;
                chDlg.System = 2;
                if (setCompress)
                {
                    chDlg.Compressed = compressed;
                    chDlg.Encrypted = compressed ? 0 : 2; // compression excludes encryption; without compression encryption may remain as is
                }
                else
                {
                    chDlg.Compressed = encrypted ? 0 : 2; // encryption excludes compression; without encryption compression may remain as is
                    chDlg.Encrypted = encrypted;
                }
                chDlg.ChangeTimeModified = FALSE;
                chDlg.ChangeTimeCreated = FALSE;
                chDlg.ChangeTimeAccessed = FALSE;
                chDlg.RecurseSubDirs = TRUE;

                if (setCompress && Configuration.CnfrmNTFSPress || // ask whether to compress/decompress
                    setEncryption && Configuration.CnfrmNTFSCrypt) // ask whether to encrypt/decrypt
                {
                    CPathBuffer subject;
                    char expanded[200];
                    int count = GetSelCount();
                    CPathBuffer path; // Heap-allocated for long path support
                    if (count > 1)
                    {
                        int totalCount = Dirs->Count + Files->Count;
                        int files = 0;
                        int dirs = 0;
                        CFileData* f;
                        int i;
                        for (i = 0; i < totalCount; i++)
                        {
                            BOOL isDir = i < Dirs->Count;
                            f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                            if (i == 0 && isDir && strcmp(Dirs->At(0).Name, "..") == 0)
                                continue;
                            if (f->Selected == 1)
                            {
                                if (isDir)
                                    dirs++;
                                else
                                    files++;
                            }
                        }

                        ExpandPluralFilesDirs(expanded, 200, files, dirs, epfdmNormal, FALSE);
                    }
                    else
                    {
                        int index;
                        if (count == 0)
                            index = GetCaretIndex();
                        else
                            GetSelItems(1, &index);

                        BOOL isDir = index < Dirs->Count;
                        CFileData* f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                        AlterFileName(path, f->Name, -1, Configuration.FileNameFormat, 0, index < Dirs->Count);
                        lstrcpy(expanded, LoadStr(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE));
                    }
                    int resTextID;
                    int resTitleID;
                    if (setCompress)
                    {
                        resTextID = compressed ? IDS_CONFIRM_NTFSCOMPRESS : IDS_CONFIRM_NTFSUNCOMPRESS;
                        resTitleID = compressed ? IDS_CONFIRM_NTFSCOMPRESS_TITLE : IDS_CONFIRM_NTFSUNCOMPRESS_TITLE;
                    }
                    else
                    {
                        resTextID = encrypted ? IDS_CONFIRM_NTFSENCRYPT : IDS_CONFIRM_NTFSDECRYPT;
                        resTitleID = encrypted ? IDS_CONFIRM_NTFSENCRYPT_TITLE : IDS_CONFIRM_NTFSDECRYPT_TITLE;
                    }
                    sprintf(subject, LoadStr(resTextID), expanded);
                    CTruncatedString str;
                    str.Set(subject, count > 1 ? NULL : path.Get());
                    CMessageBox msgBox(HWindow, MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT,
                                       LoadStr(resTitleID), &str, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL);
                    if (msgBox.Execute() != IDYES)
                    {
                        // if we selected an item, deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;
                    }
                    UpdateWindow(MainWindow->HWindow);
                }
            }
            if (setCompress || setEncryption || chDlg.Execute() == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);

                if (CheckPath(TRUE) != ERROR_SUCCESS)
                {
                    // if we selected an item, we deselect it again
                    UnselectItemWithName(temporarySelected);
                    FilesActionInProgress = FALSE;
                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                CChangeAttrsData dlgData;
                dlgData.ChangeTimeModified = chDlg.ChangeTimeModified;
                if (dlgData.ChangeTimeModified)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeModified, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeModified);
                }
                dlgData.ChangeTimeCreated = chDlg.ChangeTimeCreated;
                if (dlgData.ChangeTimeCreated)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeCreated, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeCreated);
                }
                dlgData.ChangeTimeAccessed = chDlg.ChangeTimeAccessed;
                if (dlgData.ChangeTimeAccessed)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeAccessed, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeAccessed);
                }
                //---  determine which directories and files are selected
                std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
                CFileData* f = NULL;
                int count = GetSelCount();
                if (count != 0)
                {
                    indexes = std::make_unique<int[]>(count);
                    GetSelItems(count, indexes.get());
                }
                else // nothing is selected
                {
                    int i = GetCaretIndex();
                    if (i < 0 || i >= Dirs->Count + Files->Count)
                    {
                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // invalid index (no files)
                    }
                    if (i == 0 && subDir)
                    {
                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // we do not work with ".."
                    }
                    f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                }
                //---
                COperations* script = new COperations(1000, 500, NULL, NULL, NULL);
                if (script == NULL)
                    TRACE_E(LOW_MEMORY);
                else
                {
                    HWND hFocusedWnd = GetFocus();
                    CreateSafeWaitWindow(LoadStr(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
                    EnableWindow(MainWindow->HWindow, FALSE);

                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    // ensure a correct relationship between Compressed and Encrypted
                    if (chDlg.Encrypted == 1)
                    {
                        if (chDlg.Compressed != 0)
                            TRACE_E("CFilesWindow::ChangeAttr(): unexpected value of chDlg.Compressed!");
                        chDlg.Compressed = 0;
                    }
                    else
                    {
                        if (chDlg.Compressed == 1)
                        {
                            if (chDlg.Encrypted != 0)
                                TRACE_E("CFilesWindow::ChangeAttr(): unexpected value of chDlg.Encrypted!");
                            chDlg.Encrypted = 0;
                        }
                    }

                    CAttrsData attrsData;
                    attrsData.AttrAnd = 0xFFFFFFFF;
                    attrsData.AttrOr = 0;
                    attrsData.SubDirs = chDlg.RecurseSubDirs;
                    attrsData.ChangeCompression = FALSE;
                    attrsData.ChangeEncryption = FALSE;
                    dlgData.ChangeCompression = FALSE;
                    dlgData.ChangeEncryption = FALSE;

                    if (chDlg.Archive == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_ARCHIVE);
                    if (chDlg.ReadOnly == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_READONLY);
                    if (chDlg.Hidden == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_HIDDEN);
                    if (chDlg.System == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_SYSTEM);
                    if (chDlg.Compressed == 0)
                    {
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_COMPRESSED);
                        attrsData.ChangeCompression = TRUE;
                        dlgData.ChangeCompression = TRUE;
                    }
                    if (chDlg.Encrypted == 0)
                    {
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_ENCRYPTED);
                        attrsData.ChangeEncryption = TRUE;
                        dlgData.ChangeEncryption = TRUE;
                    }

                    if (chDlg.Archive == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_ARCHIVE;
                    if (chDlg.ReadOnly == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_READONLY;
                    if (chDlg.Hidden == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_HIDDEN;
                    if (chDlg.System == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_SYSTEM;
                    if (chDlg.Compressed == 1)
                    {
                        attrsData.AttrOr |= FILE_ATTRIBUTE_COMPRESSED;
                        attrsData.ChangeCompression = TRUE;
                        dlgData.ChangeCompression = TRUE;
                    }
                    if (chDlg.Encrypted == 1)
                    {
                        attrsData.AttrOr |= FILE_ATTRIBUTE_ENCRYPTED;
                        attrsData.ChangeEncryption = TRUE;
                        dlgData.ChangeEncryption = TRUE;
                    }

                    script->ClearReadonlyMask = 0xFFFFFFFF;
                    BOOL res = BuildScriptMain(script, atChangeAttrs, NULL, NULL, count,
                                               indexes.get(), f, &attrsData, NULL, FALSE, NULL);
                    if (script->Count == 0)
                        res = FALSE;
                    // reordered to allow the main window to activate (must not be disabled), otherwise it switches to another app
                    EnableWindow(MainWindow->HWindow, TRUE);
                    DestroySafeWaitWindow();

                    // if Salamander is active, call SetFocus on the stored window (SetFocus fails
                    // if the main window is disabled - after deactivation/activation of the disabled main window the active panel
                    // does not have focus)
                    HWND hwnd = GetForegroundWindow();
                    while (hwnd != NULL && hwnd != MainWindow->HWindow)
                        hwnd = GetParent(hwnd);
                    if (hwnd == MainWindow->HWindow)
                        SetFocus(hFocusedWnd);

                    SetCursor(oldCur);

                    // prepare refresh of manually refreshed directories
                    // change in the directory displayed in the panel and also in subdirectories if work was done there as well
                    script->SetWorkPath1(GetPath(), chDlg.RecurseSubDirs);

                    if (!res || !StartProgressDialog(script, LoadStr(IDS_CHANGEATTRSTITLE), &dlgData, NULL))
                    {
                        UpdateWindow(MainWindow->HWindow);
                        if (!script->IsGood())
                            script->ResetState();
                        FreeScript(script);
                    }
                    else // removal of selection index (no waiting for operation finish, operation runs in another thread)
                    {
                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                        UpdateWindow(MainWindow->HWindow);
                    }
                }
                // RAII: indexes auto-deleted when scope exits
            }
            UpdateWindow(MainWindow->HWindow);
            FilesActionInProgress = FALSE;
        }
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_CHANGEATTRS)) // FS is in the panel
        {
            // lower the thread priority to "normal" (so the operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            int panel = MainWindow->LeftPanel == this ? PANEL_LEFT : PANEL_RIGHT;

            int count = GetSelCount();
            int selectedDirs = 0;
            if (count > 0)
            {
                // count how many directories are selected (the rest of the selected items are files)
                int i;
                for (i = 0; i < Dirs->Count; i++) // ".." cannot be selected, the check would be unnecessary
                {
                    if (Dirs->At(i).Selected)
                        selectedDirs++;
                }
            }
            else
                count = 0;

            BOOL success = GetPluginFS()->ChangeAttributes(GetPluginFS()->GetPluginFSName(), HWindow,
                                                           panel, count - selectedDirs, selectedDirs);

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            if (success) // success -> unselect
            {
                SetSel(FALSE, -1, TRUE);                        // explicit repaint
                PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                UpdateWindow(MainWindow->HWindow);
            }
        }
    }
    // if we selected an item, we deselect it again
    UnselectItemWithName(temporarySelected);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::FindFile()
{
    CALL_STACK_MESSAGE1("CFilesWindow::FindFile()");
    if (Is(ptDisk) && CheckPath(TRUE) != ERROR_SUCCESS)
        return;

    if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
        GetPluginFS()->IsServiceSupported(FS_SERVICE_OPENFINDDLG))
    { // try to open Find for the FS in the panel; if it succeeds, there is no point in opening the standard Find dialog
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        BOOL done = GetPluginFS()->OpenFindDialog(GetPluginFS()->GetPluginFSName(),
                                                  this == MainWindow->LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        if (done)
            return;
    }

    if (SystemPolicies.GetNoFind() || SystemPolicies.GetNoShellSearchButton())
    {
        gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                     LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
        return;
    }

    OpenFindDialog(MainWindow->HWindow,
                   Is(ptDisk) ? GetPath() : "",
                   Is(ptDisk) ? GetPathW() : L"");
}

void CFilesWindow::ViewFile(char* name, BOOL altView, DWORD handlerID, int enumFileNamesSourceUID,
                            int enumFileNamesLastFileIndex)
{
    CALL_STACK_MESSAGE6("CFilesWindow::ViewFile(%s, %d, %u, %d, %d)", name, altView, handlerID,
                        enumFileNamesSourceUID, enumFileNamesLastFileIndex);
    // verify that the file is on an accessible path
    CPathBuffer path;
    if (name == NULL) // file from the panel
    {
        if (Is(ptDisk) || Is(ptZIPArchive))
        {
            if (CheckPath(TRUE) != ERROR_SUCCESS)
                return;
        }
    }
    else // file from a Windows path (Find + Alt+F11)
    {
        char* backSlash = strrchr(name, '\\');
        if (backSlash != NULL)
        {
            memcpy(path, name, backSlash - name);
            path[backSlash - name] = 0;
            if (CheckPath(TRUE, path) != ERROR_SUCCESS)
                return;
        }
    }

    BOOL addToHistory = name != NULL;
    // if viewing/editing from the panel, obtain the full long name
    BOOL useDiskCache = FALSE;          // TRUE only for ZIP - uses disk-cache
    BOOL arcCacheCacheCopies = TRUE;    // cache copies in disk-cache unless the archiver plugin requests otherwise
    CPathBuffer dcFileName; // ZIP: name for disk-cache
    std::wstring viewNameW;
    if (name == NULL)
    {
        int i = GetCaretIndex();
        if (i >= Dirs->Count && i < Dirs->Count + Files->Count)
        {
            CFileData* f = &Files->At(i - Dirs->Count);
            if (Is(ptDisk))
            {
                if (enumFileNamesLastFileIndex == -1)
                    enumFileNamesLastFileIndex = i - Dirs->Count;
                viewNameW = sally::unicode::BuildPanelChildPathW(
                    sally::unicode::EffectivePanelPathW(GetPath(), GetPathW()),
                    f->Name,
                    f->NameW);
                std::string pathA;
                if (!sally::unicode::TryExactAnsiFallback(viewNameW, pathA))
                    pathA = WideToAnsi(viewNameW);
                lstrcpyn(path, pathA.c_str(), path.Size());

                if (f->DosName != NULL && SalLPGetFileAttributes(path) == INVALID_FILE_ATTRIBUTES &&
                    sally::unicode::TryExactAnsiFallback(viewNameW, pathA))
                {
                    DWORD err = GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME)
                    {
                        char* s = path + strlen(path);
                        while (s > path && *(s - 1) != '\\')
                            s--;
                        strcpy(s, f->DosName);
                        if (SalLPGetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) // still error -> revert to the long name
                            lstrcpyn(path, pathA.c_str(), path.Size());
                    }
                }
                name = path;
                addToHistory = TRUE;
            }
            else
            {
                if (Is(ptZIPArchive))
                {
                    useDiskCache = TRUE;
                    StrICpy(dcFileName, GetZIPArchive()); // the archive file name should be compared case-insensitively (Windows file system), so we always convert it to lowercase
                    if (GetZIPPath()[0] != 0)
                    {
                        if (GetZIPPath()[0] != '\\')
                            strcat(dcFileName, "\\");
                        strcat(dcFileName, GetZIPPath());
                    }
                    if (dcFileName[strlen(dcFileName) - 1] != '\\')
                        strcat(dcFileName, "\\");
                    strcat(dcFileName, f->Name);

                    // setting disk-cache for the plugin (standard values change only for the plugin)
                    CPathBuffer arcCacheTmpPath; // Heap-allocated for long path support
                    arcCacheTmpPath[0] = 0;
                    BOOL arcCacheOwnDelete = FALSE;
                    CPluginInterfaceAbstract* plugin = NULL; // != NULL if the plugin handles its own deletion
                    int format = PackerFormatConfig.PackIsArchive(GetZIPArchive());
                    if (format != 0) // a supported archive was found
                    {
                        format--;
                        int index = PackerFormatConfig.GetUnpackerIndex(format);
                        if (index < 0) // view: is the processing internal (plugin)?
                        {
                            CPluginData* data = Plugins.Get(-index - 1);
                            if (data != NULL)
                            {
                                data->GetCacheInfo(arcCacheTmpPath, &arcCacheOwnDelete, &arcCacheCacheCopies);
                                if (arcCacheOwnDelete)
                                    plugin = data->GetPluginInterface()->GetInterface();
                            }
                        }
                    }

                    CPathBuffer nameInArchive;  // Heap-allocated for long path support
                    strcpy(nameInArchive, dcFileName + strlen(GetZIPArchive()) + 1);

                    // besides itself, compare the file with all the others and look for a case-sensitive identical name;
                    // if it exists, these two files must be distinguished in the disk-cache; I chose
                    // an allocated Name address - in opposite panels with the same archive the disk-cache won't be used,
                    // but given the improbability of this case, this approach is more than sufficient
                    int x;
                    for (x = 0; x < Files->Count; x++)
                    {
                        if (i - Dirs->Count != x)
                        {
                            CFileData* f2 = &Files->At(x);
                            if (strcmp(f2->Name, f->Name) == 0)
                            {
                                sprintf(dcFileName + strlen(dcFileName), ":0x%p", f->Name);
                                break;
                            }
                        }
                    }

                    BOOL exists;
                    int errorCode;
                    CPathBuffer validTmpName; // Heap-allocated for long path support
                    validTmpName[0] = 0;
                    if (!SalIsValidFileNameComponent(f->Name))
                    {
                        lstrcpyn(validTmpName, f->Name, validTmpName.Size());
                        SalMakeValidFileNameComponent(validTmpName);
                    }
                    name = (char*)DiskCache.GetName(dcFileName,
                                                    validTmpName[0] != 0 ? validTmpName.Get() : f->Name,
                                                    &exists, FALSE,
                                                    arcCacheTmpPath[0] != 0 ? arcCacheTmpPath.Get() : NULL,
                                                    plugin != NULL, plugin, &errorCode);
                    if (name == NULL)
                    {
                        if (errorCode == DCGNE_TOOLONGNAME)
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_UNPACKTOOLONGNAME));
                        return;
                    }

                    if (!exists) // we must unpack it
                    {
                        char* backSlash = strrchr(name, '\\');
                        CPathBuffer tmpPath; // Heap-allocated for long path support
                        memcpy(tmpPath.Get(), name, backSlash - name);
                        tmpPath[backSlash - name] = 0;
                        BeginStopRefresh(); // snooper takes a break
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
                        BOOL renamingNotSupported = FALSE;
                        if (PackUnpackOneFile(this, GetZIPArchive(), PluginData.GetInterface(), nameInArchive, f, tmpPath,
                                              validTmpName[0] == 0 ? NULL : validTmpName.Get(),
                                              validTmpName[0] == 0 ? NULL : &renamingNotSupported))
                        {
                            SetCursor(oldCur);
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                            CQuadWord size(0, 0);
                            HANDLE file = HANDLES_Q(CreateFileW(AnsiToWide(name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                               NULL, OPEN_EXISTING, 0, NULL));
                            if (file != INVALID_HANDLE_VALUE)
                            {
                                DWORD err;
                                SalGetFileSize(file, size, err); // ignore errors; file size isn't that important
                                HANDLES(CloseHandle(file));
                            }
                            DiskCache.NamePrepared(dcFileName, size);
                        }
                        else
                        {
                            SetCursor(oldCur);
                            if (renamingNotSupported) // to avoid repeating the same message for many plugins, display it here for all of them
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_UNPACKINVNAMERENUNSUP));
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                            DiskCache.ReleaseName(dcFileName, FALSE); // not unpacked, nothing to cache
                            EndStopRefresh();                         // snooper will start again now
                            return;
                        }
                        EndStopRefresh(); // snooper will start again now
                    }
                }
                else
                {
                    if (Is(ptPluginFS))
                    {
                        if (GetPluginFS()->NotEmpty() && // FS is fine and supports view-file
                            GetPluginFS()->IsServiceSupported(FS_SERVICE_VIEWFILE))
                        {
                            // lower the thread priority to "normal" (so the operations don't overload the machine)
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                            CSalamanderForViewFileOnFS sal(altView, handlerID);
                            GetPluginFS()->ViewFile(GetPluginFS()->GetPluginFSName(), HWindow, &sal, *f);

                            // raise the thread priority again, the operation has finished
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                        }
                        return; // view on the FS is already done
                    }
                    else
                    {
                        TRACE_E("Incorrect call to CFilesWindow::ViewFile()");
                        return;
                    }
                }
            }
        }
        else
        {
            return;
        }
    }

    HANDLE lock = NULL;
    BOOL lockOwner = FALSE;
    ViewFileInt(HWindow, name, altView, handlerID, useDiskCache, lock, lockOwner, addToHistory,
                enumFileNamesSourceUID, enumFileNamesLastFileIndex,
                !viewNameW.empty() ? viewNameW.c_str() : NULL);

    if (useDiskCache)
    {
        if (lock != NULL) // ensure association between the viewer and disk-cache
        {
            DiskCache.AssignName(dcFileName, lock, lockOwner, arcCacheCacheCopies ? crtCache : crtDirect);
        }
        else // viewer didn't open or has no "lock" object - try leaving the file in disk-cache
        {
            DiskCache.ReleaseName(dcFileName, arcCacheCacheCopies);
        }
    }
}

BOOL ViewFileInt(HWND parent, const char* name, BOOL altView, DWORD handlerID, BOOL returnLock,
                 HANDLE& lock, BOOL& lockOwner, BOOL addToHistory, int enumFileNamesSourceUID,
                 int enumFileNamesLastFileIndex, const wchar_t* nameW)
{
    BOOL success = FALSE;
    lock = NULL;
    lockOwner = FALSE;

    // obtain the full DOS name
    CPathBuffer dosName; // Heap-allocated for long path support
    if (GetShortPathName(name, dosName, dosName.Size()) == 0)
    {
        TRACE_E("GetShortPathName() failed");
        dosName[0] = 0;
    }

    // find the file name and check if it has an extension - needed for masks
    const char* namePart = strrchr(name, '\\');
    if (namePart == NULL)
    {
        TRACE_E("Invalid parameter for ViewFileInt(): " << name);
        return FALSE;
    }
    namePart++;
    const char* tmpExt = strrchr(namePart, '.');
    //if (tmpExt == NULL || tmpExt == namePart) tmpExt = namePart + lstrlen(namePart); // ".cvspass" is not an extension...
    if (tmpExt == NULL)
        tmpExt = namePart + lstrlen(namePart); // ".cvspass" is treated as an extension in Windows...
    else
        tmpExt++;

    // position for viewers
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
    // the values are shifted by its dimensions. Perform correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);

    // if called, for example, from find and the main window is minimized,
    // we do not want a minimized viewer
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // find the correct viewer and launch it
    CViewerMasks* masks = (altView ? MainWindow->AltViewerMasks : MainWindow->ViewerMasks);
    CViewerMasksItem* viewer = NULL;

    if (handlerID != 0xFFFFFFFF)
    {
        // attempt to find a viewer with matching HandlerID
        int j;
        for (j = 0; viewer == NULL && j < 2; j++)
        {
            CViewerMasks* masks2 = (j == 0 ? MainWindow->ViewerMasks : MainWindow->AltViewerMasks);
            int i;
            for (i = 0; viewer == NULL && i < masks2->Count; i++)
            {
                if (masks2->At(i)->HandlerID == handlerID)
                    viewer = masks2->At(i);
            }
        }
    }

    if (viewer == NULL)
    {
        int i;
        for (i = 0; i < masks->Count; i++)
        {
            int err;
            if (masks->At(i)->Masks->PrepareMasks(err))
            {
                if (masks->At(i)->Masks->AgreeMasks(namePart, tmpExt))
                {
                    viewer = masks->At(i);

                    if (viewer != NULL && viewer->ViewerType != VIEWER_EXTERNAL &&
                        viewer->ViewerType != VIEWER_INTERNAL)
                    { // plug-in viewers only
                        CPluginData* plugin = Plugins.Get(-viewer->ViewerType - 1);
                        if (plugin != NULL && plugin->SupportViewer)
                        {
                            if (!plugin->CanViewFile(name))
                                continue; // try to find another viewer, this one won't do it
                        }
                        else
                            TRACE_E("Unexpected error (before CanViewFile) in (Alt)ViewerMasks (invalid ViewerType).");
                    }
                    break; // everything is fine, open the viewer
                }
            }
            else
                TRACE_E("Unexpected error in group mask.");
        }
    }

    if (viewer != NULL)
    {
        //    if (MakeFileAvailOfflineIfOneDriveOnWin81(parent, name))
        //    {
        if (addToHistory)
            MainWindow->FileHistory->AddFile(fhitView, viewer->HandlerID, name); // add file to history

        switch (viewer->ViewerType)
        {
        case VIEWER_EXTERNAL:
        {
            CPathBuffer expCommand; // Heap-allocated for long path support
            CPathBuffer expArguments; // Heap-allocated for long path support
            CPathBuffer expInitDir; // Heap-allocated for long path support
            if (ExpandCommand(parent, viewer->Command.c_str(), expCommand, expCommand.Size(), FALSE) &&
                ExpandArguments(parent, name, dosName, viewer->Arguments.c_str(), expArguments, expArguments.Size(), NULL) &&
                ExpandInitDir(parent, name, dosName, viewer->InitDir.c_str(), expInitDir, expInitDir.Size(), FALSE))
            {
                if (SystemPolicies.GetMyRunRestricted() &&
                    !SystemPolicies.GetMyCanRun(expCommand))
                {
                    gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                                 LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
                    break;
                }

                STARTUPINFOW si;
                PROCESS_INFORMATION pi;
                memset(&si, 0, sizeof(si));
                si.cb = sizeof(si);
                si.dwX = place.rcNormalPosition.left;
                si.dwY = place.rcNormalPosition.top;
                si.dwXSize = place.rcNormalPosition.right - place.rcNormalPosition.left;
                si.dwYSize = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
                si.dwFlags |= STARTF_USEPOSITION | STARTF_USESIZE |
                              STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_SHOWNORMAL;

                // Wide-aware command-line assembly. Same shape as EditFile:
                // ExpandCommand/Arguments/InitDir run ANSI through CP_ACP, then we
                // convert to wide and substitute the lossy CP_ACP mirrors of $(FullPath),
                // $(Name), $(Path) (in both with-trailing-slash and without flavors)
                // with their wide counterparts derived from the caller-supplied nameW.
                // Without this CreateProcessA fails with ERROR_DIRECTORY (267) when the
                // panel root is non-CP_ACP, even though the panel rendered it correctly.
                std::wstring effectiveNameW;
                if (nameW != NULL && nameW[0] != L'\0')
                    effectiveNameW = nameW;
                else if (name != NULL)
                    effectiveNameW = AnsiToWide(name);

                std::wstring expCommandW = AnsiToWide(expCommand.Get());
                std::wstring expArgumentsW = AnsiToWide(expArguments.Get());
                std::wstring expInitDirW = AnsiToWide(expInitDir.Get());

                if (!effectiveNameW.empty() && name != NULL && name[0] != '\0')
                {
                    const std::wstring lossyFullPathW = AnsiToWide(name);
                    auto replaceAll = [](std::wstring& s, const std::wstring& from, const std::wstring& to) {
                        if (from.empty() || from == to)
                            return;
                        size_t pos = 0;
                        while ((pos = s.find(from, pos)) != std::wstring::npos)
                        {
                            s.replace(pos, from.size(), to);
                            pos += to.size();
                        }
                    };
                    replaceAll(expArgumentsW, lossyFullPathW, effectiveNameW);
                    replaceAll(expInitDirW, lossyFullPathW, effectiveNameW);

                    const char* leafA = strrchr(name, '\\');
                    const size_t lastSlashW = effectiveNameW.find_last_of(L'\\');
                    if (leafA != NULL && lastSlashW != std::wstring::npos)
                    {
                        const std::wstring lossyLeafW = AnsiToWide(leafA + 1);
                        const std::wstring leafW = effectiveNameW.substr(lastSlashW + 1);
                        replaceAll(expArgumentsW, lossyLeafW, leafW);
                        replaceAll(expInitDirW, lossyLeafW, leafW);

                        std::string dirA(name, (size_t)(leafA - name + 1));
                        std::wstring lossyDirWithSlash = AnsiToWide(dirA.c_str());
                        std::wstring dirWithSlash = effectiveNameW.substr(0, lastSlashW + 1);
                        replaceAll(expArgumentsW, lossyDirWithSlash, dirWithSlash);
                        replaceAll(expInitDirW, lossyDirWithSlash, dirWithSlash);
                        if (!dirA.empty() && dirA.back() == '\\')
                            dirA.pop_back();
                        if (!dirA.empty())
                        {
                            std::wstring lossyDirNoSlash = AnsiToWide(dirA.c_str());
                            std::wstring dirNoSlash = (lastSlashW == 0)
                                                          ? std::wstring()
                                                          : effectiveNameW.substr(0, lastSlashW);
                            replaceAll(expArgumentsW, lossyDirNoSlash, dirNoSlash);
                            replaceAll(expInitDirW, lossyDirNoSlash, dirNoSlash);
                        }
                    }
                }

                MainWindow->SetDefaultDirectories();

                if (expInitDirW.empty()) // matches the original ANSI "this should never happen" branch
                {
                    expInitDirW = effectiveNameW.empty() ? AnsiToWide(name) : effectiveNameW;
                    size_t lastSlash = expInitDirW.find_last_of(L'\\');
                    if (lastSlash != std::wstring::npos)
                        expInitDirW.resize(lastSlash);
                    else
                        expInitDirW.clear();
                }

                std::wstring cmdLineW = expCommandW;
                if (!cmdLineW.empty() && cmdLineW.front() != L'"' &&
                    cmdLineW.find(L' ') != std::wstring::npos)
                {
                    cmdLineW.insert(cmdLineW.begin(), L'"');
                    cmdLineW.push_back(L'"');
                }
                cmdLineW.push_back(L' ');
                cmdLineW.append(expArgumentsW);

                BOOL launched = ::CreateProcessW(NULL, cmdLineW.data(), NULL, NULL, FALSE,
                                                 NORMAL_PRIORITY_CLASS, NULL,
                                                 expInitDirW.empty() ? NULL : expInitDirW.c_str(),
                                                 &si, &pi);
                if (!launched)
                {
                    DWORD err = GetLastError();
                    std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECVIEW),
                                                  expCommandW.c_str(),
                                                  GetErrorTextW(err));
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                }
                else
                {
                    HANDLES_ADD(__htProcess, __hoCreateProcess, pi.hProcess);
                    HANDLES_ADD(__htThread, __hoCreateProcess, pi.hThread);
                    success = TRUE;
                    if (returnLock)
                    {
                        lock = pi.hProcess;
                        lockOwner = TRUE;
                    }
                    else
                        HANDLES(CloseHandle(pi.hProcess));
                    HANDLES(CloseHandle(pi.hThread));
                }
            }
            break;
        }

        case VIEWER_INTERNAL:
        {
            if (Configuration.SavePosition &&
                Configuration.WindowPlacement.length != 0)
            {
                place = Configuration.WindowPlacement;
                // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
                // the values are shifted by its dimensions. Perform correction.
                RECT monitorRect2;
                RECT workRect2;
                MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect2, &monitorRect2);
                OffsetRect(&place.rcNormalPosition, workRect2.left - monitorRect2.left,
                           workRect2.top - monitorRect2.top);
                MultiMonEnsureRectVisible(&place.rcNormalPosition, TRUE);
            }

            HANDLE lockAux = NULL;
            BOOL lockOwnerAux = FALSE;
            BOOL viewerOpened = FALSE;
            if (nameW != NULL && nameW[0] != L'\0')
            {
                viewerOpened = OpenViewerW(nameW, name, vtText,
                                           place.rcNormalPosition.left,
                                           place.rcNormalPosition.top,
                                           place.rcNormalPosition.right - place.rcNormalPosition.left,
                                           place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                           place.showCmd,
                                           returnLock, &lockAux, &lockOwnerAux, NULL,
                                           enumFileNamesSourceUID, enumFileNamesLastFileIndex);
            }
            else
            {
                viewerOpened = OpenViewer(name, vtText,
                                          place.rcNormalPosition.left,
                                          place.rcNormalPosition.top,
                                          place.rcNormalPosition.right - place.rcNormalPosition.left,
                                          place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                          place.showCmd,
                                          returnLock, &lockAux, &lockOwnerAux, NULL,
                                          enumFileNamesSourceUID, enumFileNamesLastFileIndex);
            }
            if (viewerOpened)
            {
                success = TRUE;
                if (returnLock && lockAux != NULL)
                {
                    lock = lockAux;
                    lockOwner = lockOwnerAux;
                }
            }
            break;
        }

        default: // plug-ins
        {
            HANDLE lockAux = NULL;
            BOOL lockOwnerAux = FALSE;

            CPluginData* plugin = Plugins.Get(-viewer->ViewerType - 1);
            if (plugin != NULL && plugin->SupportViewer)
            {
                if (plugin->ViewFile(name, place.rcNormalPosition.left, place.rcNormalPosition.top,
                                     place.rcNormalPosition.right - place.rcNormalPosition.left,
                                     place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                     place.showCmd, Configuration.AlwaysOnTop,
                                     returnLock, &lockAux, &lockOwnerAux,
                                     enumFileNamesSourceUID, enumFileNamesLastFileIndex))
                {
                    success = TRUE;
                    if (returnLock && lockAux != NULL)
                    {
                        lock = lockAux;
                        lockOwner = lockOwnerAux;
                    }
                }
            }
            else
                TRACE_E("Unexpected error in (Alt)ViewerMasks (invalid ViewerType).");
            break;
        }
        }
        //    }
    }
    else
    {
        CPathBuffer buff;
        int textID = altView ? IDS_CANT_VIEW_FILE_ALT : IDS_CANT_VIEW_FILE;
        sprintf(buff, LoadStr(textID), name);
        gPrompter->ShowError(LoadStr(IDS_ERRORTITLE), buff.Get());
    }
    return success;
}

void CFilesWindow::EditFile(char* name, DWORD handlerID)
{
    CALL_STACK_MESSAGE3("CFilesWindow::EditFile(%s, %u)", name, handlerID);
    if (!Is(ptDisk) && name == NULL)
    {
        TRACE_E("Incorrect call to CFilesWindow::EditFile()");
        return;
    }

    // verify that the file is on an accessible path
    CPathBuffer path;
    if (name == NULL)
    {
        if (CheckPath(TRUE) != ERROR_SUCCESS)
            return;
    }
    else
    {
        char* backSlash = strrchr(name, '\\');
        if (backSlash != NULL)
        {
            memcpy(path, name, backSlash - name);
            path[backSlash - name] = 0;
            if (CheckPath(TRUE, path) != ERROR_SUCCESS)
                return;
        }
    }

    BOOL addToHistory = name != NULL && Is(ptDisk);

    // Wide twin of `name`. Built from GetPathW() + f->NameW when available so
    // that Unicode panel paths (e.g. "C:\Temp\SalLongPathTest\zz中文\한글_x.txt")
    // can be passed to CreateProcessW without going through CP_ACP — otherwise
    // the working-directory parameter resolves to "zz??" and CreateProcessA
    // returns ERROR_DIRECTORY (267).
    std::wstring nameW;

    // if viewing/editing from the panel, obtain the full long name
    if (name == NULL)
    {
        int i = GetCaretIndex();
        if (i >= Dirs->Count && i < Dirs->Count + Files->Count)
        {
            CFileData* f = &Files->At(i - Dirs->Count);
            if (Is(ptDisk))
            {
                lstrcpyn(path, GetPath(), path.Size());
                if (GetPath()[strlen(GetPath()) - 1] != '\\')
                    strcat(path, "\\");
                char* s = path + strlen(path);
                if ((s - path) + f->NameLen >= (int)path.Size())
                {
                    if (f->DosName != NULL && strlen(f->DosName) + (s - path) < (int)path.Size())
                        strcpy(s, f->DosName);
                    else
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TOOLONGNAME));
                        return;
                    }
                }
                else
                    strcpy(s, f->Name);
                // try whether the file name is valid, otherwise try its DOS name as well
                // (handles files accessible only through Unicode or DOS names)
                if (f->DosName != NULL && SalLPGetFileAttributes(path) == INVALID_FILE_ATTRIBUTES)
                {
                    DWORD err = GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME)
                    {
                        if (strlen(f->DosName) + (s - path) < (int)path.Size())
                        {
                            strcpy(s, f->DosName);
                            if (SalLPGetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) // still error -> revert to the long name
                            {
                                if ((s - path) + f->NameLen < (int)path.Size())
                                    strcpy(s, f->Name);
                            }
                        }
                    }
                }
                name = path;
                addToHistory = TRUE;
                // Build the wide twin while f is still in scope. Prefer
                // f->NameW (preserved Unicode leaf, populated by directory
                // read for any non-ASCII filename) over AnsiToWide(f->Name),
                // which would re-introduce CP_ACP loss for Korean/CJK leaves.
                if (sally::unicode::HasWidePathW(GetPathW()))
                {
                    nameW = GetPathW();
                    if (!nameW.empty() && nameW.back() != L'\\')
                        nameW += L'\\';
                    if (f->NameW != NULL)
                        nameW += f->NameW;
                    else if (f->Name != NULL)
                        nameW += AnsiToWide(f->Name);
                }
            }
        }
        else
        {
            return;
        }
    }

    // Fallback wide twin when name was supplied by an external caller
    // (Find dialog, history). No preserved CFileData here; the best we can
    // do is AnsiToWide. The Find dialog's wide propagation is a separate
    // stream (tracked in kb/unicode/TODO.md item 3).
    if (nameW.empty() && name != NULL)
        nameW = AnsiToWide(name);

    // obtain the full DOS name
    CPathBuffer dosName; // Heap-allocated for long path support
    if (GetShortPathName(name, dosName, dosName.Size()) == 0)
    {
        TRACE_I("GetShortPathName() failed.");
        dosName[0] = 0;
    }

    // find the file name and check if it has an extension - needed for masks
    char* namePart = strrchr(name, '\\');
    if (namePart == NULL)
    {
        TRACE_E("Invalid parameter CFilesWindow::EditFile(): " << name);
        return;
    }
    namePart++;
    char* tmpExt = strrchr(namePart, '.');
    //if (tmpExt == NULL || tmpExt == namePart) tmpExt = namePart + lstrlen(namePart); // ".cvspass" is not an extension...
    if (tmpExt == NULL)
        tmpExt = namePart + lstrlen(namePart); // ".cvspass" is treated as an extension in Windows...
    else
        tmpExt++;

    // position for editors
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
    // the values are shifted by its dimensions. Perform correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);
    // if called, for example, from find and the main window is minimized,
    // we do not want a minimized editor
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // find the correct editor and launch it
    CEditorMasks* masks = MainWindow->EditorMasks;

    CEditorMasksItem* editor = NULL;

    if (handlerID != 0xFFFFFFFF)
    {
        // attempt to find an editor with matching HandlerID
        int i;
        for (i = 0; editor == NULL && i < masks->Count; i++)
        {
            if (masks->At(i)->HandlerID == handlerID)
                editor = masks->At(i);
        }
    }

    if (editor == NULL)
    {
        int i;
        for (i = 0; i < masks->Count; i++)
        {
            int err;
            if (masks->At(i)->Masks->PrepareMasks(err))
            {
                if (masks->At(i)->Masks->AgreeMasks(namePart, tmpExt))
                {
                    editor = masks->At(i);
                    break;
                }
            }
            else
                TRACE_E("Unexpected error in group mask");
        }
    }

    if (editor != NULL)
    {
        if (addToHistory)
            MainWindow->FileHistory->AddFile(fhitEdit, editor->HandlerID, name); // add file to history

        CPathBuffer expCommand; // Heap-allocated for long path support
        CPathBuffer expArguments; // Heap-allocated for long path support
        CPathBuffer expInitDir; // Heap-allocated for long path support
        if (ExpandCommand(HWindow, editor->Command.c_str(), expCommand, expCommand.Size(), FALSE) &&
            ExpandArguments(HWindow, name, dosName, editor->Arguments.c_str(), expArguments, expArguments.Size(), NULL) &&
            ExpandInitDir(HWindow, name, dosName, editor->InitDir.c_str(), expInitDir, expInitDir.Size(), FALSE))
        {
            if (SystemPolicies.GetMyRunRestricted() &&
                !SystemPolicies.GetMyCanRun(expCommand))
            {
                gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                             LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
                return;
            }

            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            si.dwX = place.rcNormalPosition.left;
            si.dwY = place.rcNormalPosition.top;
            si.dwXSize = place.rcNormalPosition.right - place.rcNormalPosition.left;
            si.dwYSize = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
            si.dwFlags |= STARTF_USEPOSITION | STARTF_USESIZE |
                          STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOWNORMAL;

            // Wide-aware command-line assembly. The ANSI ExpandCommand/Arguments/InitDir
            // above expanded the templates through CP_ACP, so any $(FullPath), $(Name) or
            // $(Path) substitutions that referenced a non-CP_ACP filename came out lossy
            // (e.g. "zz??\??_Korea_22a_9.txt"). Convert each piece to wide via AnsiToWide
            // and then substitute the lossy CP_ACP-mirrored substrings with their wide
            // counterparts derived from nameW. This recovers the common $(FullPath),
            // $(Name) and $(Path) cases without touching the var-expansion engine.
            std::wstring expCommandW = AnsiToWide(expCommand.Get());
            std::wstring expArgumentsW = AnsiToWide(expArguments.Get());
            std::wstring expInitDirW = AnsiToWide(expInitDir.Get());

            if (!nameW.empty() && name != NULL && name[0] != '\0')
            {
                const std::wstring lossyFullPathW = AnsiToWide(name);
                auto replaceAll = [](std::wstring& s, const std::wstring& from, const std::wstring& to) {
                    if (from.empty() || from == to)
                        return;
                    size_t pos = 0;
                    while ((pos = s.find(from, pos)) != std::wstring::npos)
                    {
                        s.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                };
                replaceAll(expArgumentsW, lossyFullPathW, nameW);
                replaceAll(expInitDirW, lossyFullPathW, nameW);

                const char* leafA = strrchr(name, '\\');
                const size_t lastSlashW = nameW.find_last_of(L'\\');
                if (leafA != NULL && lastSlashW != std::wstring::npos)
                {
                    const std::wstring lossyLeafW = AnsiToWide(leafA + 1);
                    const std::wstring leafW = nameW.substr(lastSlashW + 1);
                    replaceAll(expArgumentsW, lossyLeafW, leafW);
                    replaceAll(expInitDirW, lossyLeafW, leafW);

                    // Path component matched in two flavors: $(Path) expansion
                    // returns the directory with a trailing backslash, but the
                    // ANSI ExpandInitDir strips the trailing backslash from its
                    // result before returning (CreateProcess's lpCurrentDirectory
                    // convention). Do both replacements; the longer-with-slash
                    // form is tried first so it doesn't get shadowed by the
                    // shorter no-slash form's substitution.
                    std::string dirA(name, (size_t)(leafA - name + 1));
                    std::wstring lossyDirWithSlash = AnsiToWide(dirA.c_str());
                    std::wstring dirWithSlash = nameW.substr(0, lastSlashW + 1);
                    replaceAll(expArgumentsW, lossyDirWithSlash, dirWithSlash);
                    replaceAll(expInitDirW, lossyDirWithSlash, dirWithSlash);
                    if (!dirA.empty() && dirA.back() == '\\')
                        dirA.pop_back();
                    if (!dirA.empty())
                    {
                        std::wstring lossyDirNoSlash = AnsiToWide(dirA.c_str());
                        std::wstring dirNoSlash = (lastSlashW == 0)
                                                      ? std::wstring()
                                                      : nameW.substr(0, lastSlashW);
                        replaceAll(expArgumentsW, lossyDirNoSlash, dirNoSlash);
                        replaceAll(expInitDirW, lossyDirNoSlash, dirNoSlash);
                    }
                }
            }

            MainWindow->SetDefaultDirectories();

            if (expInitDirW.empty()) // belt-and-suspenders fallback (parallel of the original ANSI "this should never happen" branch)
            {
                expInitDirW = nameW.empty() ? AnsiToWide(name) : nameW;
                size_t lastSlash = expInitDirW.find_last_of(L'\\');
                if (lastSlash != std::wstring::npos)
                    expInitDirW.resize(lastSlash);
                else
                    expInitDirW.clear();
            }

            // Assemble wide cmdLine = quoted command + ' ' + arguments. Quote the
            // command (binary path) if it contains spaces and isn't already quoted.
            std::wstring cmdLineW = expCommandW;
            if (!cmdLineW.empty() && cmdLineW.front() != L'"' &&
                cmdLineW.find(L' ') != std::wstring::npos)
            {
                cmdLineW.insert(cmdLineW.begin(), L'"');
                cmdLineW.push_back(L'"');
            }
            cmdLineW.push_back(L' ');
            cmdLineW.append(expArgumentsW);

            // CreateProcessW: the lpCommandLine buffer must be writable per MSDN.
            // std::wstring::data() returns a writable contiguous buffer (C++17+).
            // Bypass the HANDLES wrapper around ::CreateProcessW (C__Handles only
            // provides an ANSI CreateProcess shim) and register the resulting
            // process+thread handles with HANDLES_ADD so the close calls below
            // can match against the tracker.
            BOOL launched = ::CreateProcessW(NULL, cmdLineW.data(), NULL, NULL, FALSE,
                                             NORMAL_PRIORITY_CLASS, NULL,
                                             expInitDirW.empty() ? NULL : expInitDirW.c_str(),
                                             &si, &pi);
            if (!launched)
            {
                DWORD err = GetLastError();
                std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECEDIT),
                                              expCommandW.c_str(),
                                              GetErrorTextW(err));
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
            else
            {
                HANDLES_ADD(__htProcess, __hoCreateProcess, pi.hProcess);
                HANDLES_ADD(__htThread, __hoCreateProcess, pi.hThread);
                HANDLES(CloseHandle(pi.hProcess));
                HANDLES(CloseHandle(pi.hThread));
            }
        }
    }
    else
    {
        CPathBuffer buff;
        sprintf(buff, LoadStr(IDS_CANT_EDIT_FILE), name);
        gPrompter->ShowError(LoadStr(IDS_ERRORTITLE), buff.Get());
    }
}

void CFilesWindow::EditNewFile()
{
    CALL_STACK_MESSAGE1("CFilesWindow::EditNewFile()");
    BeginStopRefresh(); // snooper takes a break

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);

    CPathBuffer path; // Heap-allocated for long path support
    std::wstring pathW;
    if (Configuration.UseEditNewFileDefault)
    {
        lstrcpyn(path, Configuration.EditNewFileDefault, path.Size());
        pathW = AnsiToWide(path);
    }
    else
    {
        // Try to derive name from the focused item (issue #12)
        BOOL usedFocused = FALSE;
        int totalCount = Dirs->Count + Files->Count;
        if (FocusedIndex >= 0 && FocusedIndex < totalCount)
        {
            if (FocusedIndex >= Dirs->Count)
            {
                // Focused on a file
                CFileData* f = &Files->At(FocusedIndex - Dirs->Count);
                std::wstring nameW = f->NameW ? f->NameW : AnsiToWide(f->Name);
                const wchar_t* dot = wcsrchr(nameW.c_str(), L'.');
                std::wstring suggestion;
                if (dot != NULL && dot > nameW.c_str())
                {
                    // "report.docx" -> "report-new.docx"
                    suggestion.assign(nameW.c_str(), dot);
                    suggestion += L"-new";
                    suggestion += dot;
                }
                else
                {
                    // "Makefile" -> "Makefile-new"
                    suggestion = nameW + L"-new";
                }
                pathW = suggestion;
                WideToAnsi(pathW, path.Get(), path.Size());
                usedFocused = TRUE;
            }
            else
            {
                // Focused on a directory — skip ".."
                CFileData* d = &Dirs->At(FocusedIndex);
                if (strcmp(d->Name, "..") != 0)
                {
                    // "MyFolder" -> "MyFolder-new.txt"
                    std::wstring nameW = d->NameW ? d->NameW : AnsiToWide(d->Name);
                    pathW = nameW + L"-new.txt";
                    WideToAnsi(pathW, path.Get(), path.Size());
                    usedFocused = TRUE;
                }
            }
        }
        if (!usedFocused)
        {
            lstrcpyn(path, LoadStr(IDS_EDITNEWFILE_DEFAULTNAME), path.Size());
            pathW = AnsiToWide(path);
        }
    }
    CTruncatedString subject;
    subject.Set(LoadStr(IDS_NEWFILENAME), NULL);

    BOOL first = TRUE;

    while (1)
    {
        CEditNewFileDialog dlg(HWindow, path, path.Size(), &subject,
                               Configuration.EditNewHistory, EDITNEW_HISTORY_SIZE,
                               Configuration.EditNewHistoryW, EDITNEW_HISTORY_SIZE);
        dlg.SetUnicodePath(pathW);

        // Some users always create .txt and are satisfied with overwriting just the extension; others create various files and want to overwrite the whole name,
        // so we compromised and introduced a dedicated option for Edit New File in the configuration.
        // ------------------
        // For EditNew, the smart selection of only the name makes no sense because people also change the extension, see our forum:
        // https://forum.altap.cz/viewtopic.php?t=2655
        // -----------------
        // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
        // the same code appears here four times
        if (!Configuration.EditNewSelectAll)
        {
            int selectionEnd = -1;
            if (first)
            {
                const wchar_t* dot = wcsrchr(pathW.c_str(), L'.');
                if (dot != NULL && dot > pathW.c_str()) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                                                        //      if (dot != NULL)
                    selectionEnd = (int)(dot - pathW.c_str());
                dlg.SetSelectionEnd(selectionEnd);
                first = FALSE; // after an error we get the full file name, so we select it all
            }
        }

        if (dlg.Execute() == IDOK)
        {
            UpdateWindow(MainWindow->HWindow);

            std::wstring inputW = dlg.GetUnicodeResult();
            if (inputW.empty())
                inputW = AnsiToWide(path);
            if (!inputW.empty())
            {
                wchar_t* writable = &inputW[0];
                wchar_t* lastCompNameW = wcsrchr(writable, L'\\');
                MakeValidFileNameComponentW(lastCompNameW != NULL ? lastCompNameW + 1 : writable);
            }
            pathW = inputW;
            WideToAnsi(pathW, path.Get(), path.Size()); // keep ANSI fallback for legacy callers

            // clean the name from undesirable characters at the beginning and end
            // we do this only for the last component; the previous ones already exist and it doesn't matter
            // (the system handles it) or they are checked during creation and an error is shown
            // (we don't clean them, we let the user do some work, it's easy enough)
            char* errText;
            int errTextID;
            std::wstring nextFocusW;
            std::wstring curDirW;
            if (Is(ptDisk))
                curDirW = AnsiToWide(GetPath());
            if (SalGetFullNameW(pathW, &errTextID, Is(ptDisk) ? curDirW.c_str() : NULL, &nextFocusW, NULL, FALSE))
            {
                std::wstring checkPathW = pathW;
                if (!CutDirectoryW(checkPathW))
                {
                    errText = LoadStr(IDS_PATHISINVALID);
                }
                else if (SalCheckPathW(TRUE, checkPathW.c_str(), ERROR_SUCCESS, TRUE, HWindow) != ERROR_SUCCESS)
                {
                    EndStopRefresh(); // snooper will start again now
                    return;
                }
                else
                {
                    BOOL invalidName = FileNameInvalidForManualCreateW(pathW.c_str());
                    HANDLE hFile = INVALID_HANDLE_VALUE;
                    if (invalidName)
                        SetLastError(ERROR_INVALID_NAME);
                    else
                    {
                        hFile = gFileSystem->CreateFile(pathW.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                        HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hFile, GetLastError(), TRUE);
                    }
                    BOOL editExisting = FALSE;
                    if (hFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS)
                    {
                        PromptResult pr = gPrompter->ConfirmOverwrite(NULL, LoadStrW(IDS_EDITNEWALREADYEX));
                        if (pr.type == PromptResult::kYes)
                            editExisting = TRUE;
                        else
                            break;
                    }
                    if (hFile != INVALID_HANDLE_VALUE || editExisting)
                    {
                        if (!editExisting)
                            HANDLES(CloseHandle(hFile));

                        if (!nextFocusW.empty())
                            WideToAnsi(nextFocusW, NextFocusName, NextFocusName.Size());

                        CPathBuffer editorPathA;
                        if (TryGetAnsiEditorLaunchPath(pathW, editorPathA.Get(), editorPathA.Size()))
                        {
                            EditFile(editorPathA);
                        }
                        else
                        {
                            // Wide-only names without an ANSI/8.3 alias still need a wide shell fallback.
                            HINSTANCE openRes = ShellExecuteW(HWindow, L"open", pathW.c_str(), NULL, NULL, SW_SHOWNORMAL);
                            if ((INT_PTR)openRes <= 32)
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextW((DWORD)(INT_PTR)openRes));
                        }

                        // change only in the directory where the file was created
                        CPathBuffer checkPathA;
                        WideToAnsi(checkPathW, checkPathA.Get(), checkPathA.Size());
                        MainWindow->PostChangeOnPathNotification(checkPathA, FALSE);

                        break;
                    }
                    else
                        errText = GetErrorText(GetLastError());
                }
            }
            else
                errText = LoadStr(errTextID);
            gPrompter->ShowError(LoadStr(IDS_ERRORTITLE), errText);
        }
        else
            break;
    }
    EndStopRefresh(); // snooper will start again now
}

// fills the popup based on available viewers
void CFilesWindow::FillViewWithMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFilesWindow::FillViewWithMenu()");

    // remove existing items
    popup->RemoveAllItems();

    // retrieve the list of viewer indexes
    TDirectArray<CViewerMasksItem*> items(50, 10);
    if (!FillViewWithData(&items))
        return;

    MENU_ITEM_INFO mii;
    char buff[1024];
    int i;
    for (i = 0; i < items.Count; i++)
    {
        CViewerMasksItem* item = items[i];

        int imgIndex = -1; // no icon
        if (item->ViewerType < 0)
        {
            int pluginIndex = -item->ViewerType - 1;
            CPluginData* plugin = Plugins.Get(pluginIndex);
            lstrcpy(buff, plugin->Name.c_str());
            if (plugin->PluginIconIndex != -1) // the plugin has an icon
                imgIndex = pluginIndex;
        }
        if (item->ViewerType == VIEWER_EXTERNAL)
            sprintf(buff, LoadStr(IDS_VIEWWITH_EXTERNAL), item->Command.c_str());
        if (item->ViewerType == VIEWER_INTERNAL)
            lstrcpy(buff, LoadStr(IDS_VIEWWITH_INTERNAL));

        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_IMAGEINDEX;
        mii.Type = MENU_TYPE_STRING;
        mii.String = buff;
        mii.ID = CM_VIEWWITH_MIN + i;
        mii.ImageIndex = imgIndex;
        if (mii.ID > CM_VIEWWITH_MAX)
        {
            TRACE_E("mii.ID > CM_VIEWWITH_MAX");
            break;
        }
        popup->InsertItem(-1, TRUE, &mii);
    }
    if (popup->GetItemCount() == 0)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE;
        mii.Type = MENU_TYPE_STRING;
        mii.State = MENU_STATE_GRAYED;
        mii.String = LoadStr(IDS_VIEWWITH_EMPTY);
        popup->InsertItem(-1, TRUE, &mii);
    }
    else
        popup->AssignHotKeys();
}

BOOL CFilesWindow::FillViewWithData(TDirectArray<CViewerMasksItem*>* items)
{
    // merging proceeds through normal and alternate viewers
    int type;
    for (type = 0; type < 2; type++)
    {
        CViewerMasks* masks;
        if (type == 0)
            masks = MainWindow->ViewerMasks;
        else
            masks = MainWindow->AltViewerMasks;

        int i;
        for (i = 0; i < masks->Count; i++)
        {
            CViewerMasksItem* item = masks->At(i);

            BOOL alreadyAdded = FALSE; // we do not want the item listed more than once

            int j;
            for (j = 0; j < items->Count; j++)
            {
                CViewerMasksItem* oldItem = items->At(j);

                if (item->ViewerType == VIEWER_EXTERNAL)
                {
                    if (stricmp(item->Command.c_str(), oldItem->Command.c_str()) == 0 &&
                        stricmp(item->Arguments.c_str(), oldItem->Arguments.c_str()) == 0 &&
                        stricmp(item->InitDir.c_str(), oldItem->InitDir.c_str()) == 0)
                    {
                        alreadyAdded = TRUE;
                        break;
                    }
                }
                else
                {
                    if (item->ViewerType == oldItem->ViewerType)
                    {
                        alreadyAdded = TRUE;
                        break;
                    }
                }
            }

            if (!alreadyAdded)
            {
                items->Add(masks->At(i));
                if (!items->IsGood())
                {
                    items->ResetState();
                    return FALSE;
                }
            }
        }
    }
    return TRUE;
}

void CFilesWindow::OnViewFileWith(int index)
{
    BeginStopRefresh(); // snooper takes a break

    // get the list of viewer indexes
    TDirectArray<CViewerMasksItem*> items(50, 10);
    if (!FillViewWithData(&items))
    {
        EndStopRefresh(); // snooper will start again now
        return;
    }

    if (index < 0 || index >= items.Count)
    {
        TRACE_E("index=" << index);
        EndStopRefresh(); // snooper will start again now
        return;
    }

    ViewFile(NULL, FALSE, items[index]->HandlerID, Is(ptDisk) ? EnumFileNamesSourceUID : -1,
             -1 /* index determined by focus */);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::ViewFileWith(char* name, HWND hMenuParent, const POINT* menuPoint, DWORD* handlerID,
                                int enumFileNamesSourceUID, int enumFileNamesLastFileIndex)
{
    CALL_STACK_MESSAGE5("CFilesWindow::ViewFileWith(%s, , , %s, %d, %d)", name,
                        (handlerID == NULL ? "NULL" : "non-NULL"), enumFileNamesSourceUID,
                        enumFileNamesLastFileIndex);
    BeginStopRefresh(); // snooper takes a break
    if (handlerID != NULL)
        *handlerID = 0xFFFFFFFF;

    CMenuPopup contextPopup(CML_FILES_VIEWWITH);
    FillViewWithMenu(&contextPopup);
    DWORD cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                   menuPoint->x, menuPoint->y, hMenuParent, NULL);
    if (cmd >= CM_VIEWWITH_MIN && cmd <= CM_VIEWWITH_MAX)
    {
        // get the list of viewer indexes
        TDirectArray<CViewerMasksItem*> items(50, 10);
        if (!FillViewWithData(&items))
        {
            EndStopRefresh(); // snooper will start again now
            return;
        }

        int index = cmd - CM_VIEWWITH_MIN;
        if (handlerID == NULL)
            ViewFile(name, FALSE, items[index]->HandlerID, enumFileNamesSourceUID, enumFileNamesLastFileIndex);
        else
            *handlerID = items[index]->HandlerID;
    }

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::FillEditWithMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFilesWindow::FillEditWithMenu()");

    // remove existing items
    popup->RemoveAllItems();

    // retrieve the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    MENU_ITEM_INFO mii;
    char buff[1024];

    int i;
    for (i = 0; i < masks->Count; i++)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
        mii.Type = MENU_TYPE_STRING;
        mii.String = buff;
        mii.ID = CM_EDITWITH_MIN + i;
        if (mii.ID > CM_EDITWITH_MAX)
        {
            TRACE_E("mii.ID > CM_EDITWITH_MAX");
            break;
        }

        CEditorMasksItem* item = masks->At(i);

        // if users have multiple rows of masks associated with one viewer/editor,
        // insert the item into the list only once
        BOOL alreadyAdded = FALSE;
        int j;
        for (j = 0; j < i; j++)
        {
            CEditorMasksItem* oldItem = masks->At(j);
            if (stricmp(item->Command.c_str(), oldItem->Command.c_str()) == 0 &&
                stricmp(item->Arguments.c_str(), oldItem->Arguments.c_str()) == 0 &&
                stricmp(item->InitDir.c_str(), oldItem->InitDir.c_str()) == 0)
            {
                alreadyAdded = TRUE;
                break;
            }
        }
        if (!alreadyAdded)
        {
            sprintf(buff, LoadStr(IDS_EDITWITH_EXTERNAL), item->Command.c_str());
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
    if (popup->GetItemCount() == 0)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE;
        mii.Type = MENU_TYPE_STRING;
        mii.State = MENU_STATE_GRAYED;
        mii.String = LoadStr(IDS_EDITWITH_EMPTY);
        popup->InsertItem(-1, TRUE, &mii);
    }
    else
        popup->AssignHotKeys();
}

void CFilesWindow::OnEditFileWith(int index)
{
    BeginStopRefresh(); // snooper takes a break

    // get the list of viewer indexes
    // get the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    if (index < 0 || index >= masks->Count)
    {
        TRACE_E("index=" << index);
        EndStopRefresh(); // snooper will start again now
        return;
    }

    EditFile(NULL, masks->At(index)->HandlerID);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::EditFileWith(char* name, HWND hMenuParent, const POINT* menuPoint, DWORD* handlerID)
{
    CALL_STACK_MESSAGE3("CFilesWindow::EditFileWith(%s, , , %s)", name,
                        (handlerID == NULL ? "NULL" : "non-NULL"));
    BeginStopRefresh(); // snooper takes a break
    if (handlerID != NULL)
        *handlerID = 0xFFFFFFFF;

    // get the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    // create menu
    CMenuPopup contextPopup;
    FillEditWithMenu(&contextPopup);
    DWORD cmd = contextPopup.Track(MENU_TRACK_NONOTIFY | MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                   menuPoint->x, menuPoint->y, hMenuParent, NULL);
    if (cmd >= CM_EDITWITH_MIN && cmd <= CM_EDITWITH_MAX)
    {
        int index = cmd - CM_EDITWITH_MIN;
        if (handlerID == NULL)
            EditFile(name, masks->At(index)->HandlerID);
        else
            *handlerID = masks->At(index)->HandlerID;
    }

    EndStopRefresh(); // snooper will start again now
}

BOOL FileNameInvalidForManualCreate(const char* path)
{
    const char* name = strrchr(path, '\\');
    if (name != NULL)
    {
        name++;
        int nameLen = (int)strlen(name);
        return nameLen > 0 && (*name <= ' ' || name[nameLen - 1] <= ' ' || name[nameLen - 1] == '.');
    }
    return FALSE;
}

BOOL MakeValidFileName(char* path)
{
    // trim spaces at the beginning and spaces and dots at the end of the name; Explorer does it
    // and people wanted the same behavior, see https://forum.altap.cz/viewtopic.php?f=16&t=5891
    // and https://forum.altap.cz/viewtopic.php?f=2&t=4210
    BOOL ch = FALSE;
    char* n = path;
    while (*n != 0 && *n <= ' ')
        n++;
    if (n > path)
    {
        memmove(path, n, strlen(n) + 1);
        ch = TRUE;
    }
    n = path + strlen(path);
    while (n > path && (*(n - 1) <= ' ' || *(n - 1) == '.'))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

BOOL CutSpacesFromBothSides(char* path)
{
    // trim spaces at the beginning and end of the name
    BOOL ch = FALSE;
    char* n = path;
    while (*n != 0 && *n <= ' ')
        n++;
    if (n > path)
    {
        memmove(path, n, strlen(n) + 1);
        ch = TRUE;
    }
    n = path + strlen(path);
    while (n > path && (*(n - 1) <= ' '))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

BOOL CutSpacesFromBothSidesW(wchar_t* path)
{
    BOOL ch = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        ch = TRUE;
    }
    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' '))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

BOOL CutDoubleQuotesFromBothSides(char* path)
{
    int len = (int)strlen(path);
    if (len >= 2 && path[0] == '"' && path[len - 1] == '"')
    {
        memmove(path, path + 1, len - 2);
        path[len - 2] = 0;
        return TRUE;
    }
    return FALSE;
}

void CFilesWindow::CreateDir(CFilesWindow* target)
{
    CALL_STACK_MESSAGE1("CFilesWindow::CreateDir()");
    BeginStopRefresh(); // snooper takes a break

    CPathBuffer path, nextFocus;  // Heap-allocated for long path support
    *path = 0;
    *nextFocus = 0;
    std::wstring pathW;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    if (Is(ptDisk)) // create directory on disk
    {
        CTruncatedString subject;
        subject.Set(LoadStr(IDS_CREATEDIRECTORY_TEXT), NULL);
        CCopyMoveDialog dlg(HWindow, path, path.Size(), LoadStr(IDS_CREATEDIRECTORY_TITLE),
                            &subject, IDD_CREATEDIRDIALOG,
                            Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE,
                            FALSE,
                            Configuration.CreateDirHistoryW, CREATEDIR_HISTORY_SIZE);
        dlg.SetUnicodePath(pathW);

    CREATE_AGAIN:

        if (dlg.Execute() == IDOK)
        {
            UpdateWindow(MainWindow->HWindow);
            pathW = dlg.GetUnicodeResult();
            if (pathW.empty())
                pathW = AnsiToWide(path.Get());
            dlg.SetUnicodePath(pathW);

            sally::filesystem::CreateDirectoryPlan plan;
            sally::filesystem::CreateDirectoryFailure failure;
            if (!sally::filesystem::PrepareCreateDirectoryTargetW(pathW, GetPathW(), plan, &failure))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORCREATINGDIR), LoadStrW(failure.errorTextId));
                goto CREATE_AGAIN;
            }

            std::wstring rootPathW = GetRootPathW(plan.fullPath.c_str());
            if (SalCheckPathW(TRUE, rootPathW.c_str(), ERROR_SUCCESS, TRUE, HWindow) != ERROR_SUCCESS)
                goto CREATE_AGAIN;

            if (!sally::filesystem::DirectoryExistsW(plan.parentPath))
            {
                bool createParents = true;
                if (Configuration.CnfrmCreateDir)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRECTORY), plan.parentPath.c_str());
                    bool dontShow = !Configuration.CnfrmCreateDir;
                    PromptResult res = gPrompter->ConfirmWithCheckbox(LoadStrW(IDS_QUESTION), msg.c_str(),
                                                                      LoadStrW(IDS_DONTSHOWAGAINCD), &dontShow);
                    Configuration.CnfrmCreateDir = !dontShow;
                    createParents = res.type == PromptResult::kOk;
                }
                if (!createParents)
                    goto CREATE_AGAIN;
            }

            std::wstring firstCreatedDirW;
            while (!sally::filesystem::EnsureDirectoryTreeExistsW(plan.parentPath, true, &firstCreatedDirW, &failure))
            {
                std::wstring errorText = failure.errorTextId != 0 ? LoadStrW(failure.errorTextId) : GetErrorTextW(failure.errorCode);
                if (gPrompter->AskRetryCancel(LoadStrW(IDS_ERRORCREATINGDIR), errorText.c_str()).type != PromptResult::kRetry)
                    goto CREATE_AGAIN;
            }

            if (!firstCreatedDirW.empty())
            {
                std::wstring notifyPathW = firstCreatedDirW;
                CutDirectoryW(notifyPathW);
                MainWindow->PostChangeOnPathNotificationW(notifyPathW.c_str(), FALSE);
            }

            while (1)
            {
                HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                DWORD err = ERROR_SUCCESS;
                BOOL invalidName = sally::filesystem::IsManualCreateLeafInvalidW(plan.fullPath);
                if (!invalidName && SalCreateDirectoryExW(plan.fullPath.c_str(), &err))
                {
                    SetCursor(oldCur);

                    NextFocusNameW = plan.nextFocus;
                    WideToAnsi(NextFocusNameW, NextFocusName, NextFocusName.Size());
                    MainWindow->PostChangeOnPathNotificationW(plan.parentPath.c_str(), FALSE);

                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                if (invalidName)
                    err = ERROR_INVALID_NAME;
                SetCursor(oldCur);

                if (gPrompter->AskRetryCancel(LoadStrW(IDS_ERRORCREATINGDIR), GetErrorTextW(err)).type != PromptResult::kRetry)
                    goto CREATE_AGAIN;
            }
        }
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_CREATEDIR)) // FS is in the panel
        {
            // lower the thread priority to "normal" (so operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            CPathBuffer newName;  // Heap-allocated for long path support
            *newName = 0;
            BOOL cancel = FALSE;
            BOOL ret = GetPluginFS()->CreateDir(GetPluginFS()->GetPluginFSName(), 1, HWindow, newName, cancel);
            if (!cancel) // not a cancel of the operation
            {
                if (!ret)
                {
                    CTruncatedString subject;
                    subject.Set(LoadStr(IDS_CREATEDIRECTORY_TEXT), NULL);
                    CCopyMoveDialog dlg(HWindow, path, path.Size(), LoadStr(IDS_CREATEDIRECTORY_TITLE),
                                        &subject, IDD_CREATEDIRDIALOG,
                                        Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE,
                                        FALSE);
                    while (1)
                    {
                        // open the standard dialog
                        if (dlg.Execute() == IDOK)
                        {
                            strcpy(newName, path);
                            ret = GetPluginFS()->CreateDir(GetPluginFS()->GetPluginFSName(), 2, HWindow, newName, cancel);
                            if (ret || cancel)
                                break; // not an error (cancel or success)
                            strcpy(path, newName);
                        }
                        else
                        {
                            WaitForESCRelease();
                            cancel = TRUE;
                            break;
                        }
                    }
                }

                if (ret && !cancel) // operation completed successfully
                {
                    lstrcpyn(NextFocusName, newName, NextFocusName.Size()); // ensure focus of the new name after refresh
                }
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        }
    }

    UpdateWindow(MainWindow->HWindow);
    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::RenameFileInternal(CFileData* f, const char* formatedFileName, BOOL* mayChange, BOOL* tryAgain)
{
    *tryAgain = TRUE;
    const char* s = formatedFileName;
    while (*s != 0 && *s != '\\' && *s != '/' && *s != ':' &&
           *s >= 32 && *s != '<' && *s != '>' && *s != '|' && *s != '"')
        s++;
    if (formatedFileName[0] != 0 && *s == 0)
    {
        CPathBuffer finalName;  // Heap-allocated for long path support
        MaskName(finalName, finalName.Size(), f->Name, formatedFileName);

        // clean the name from undesirable characters at the beginning and end
        MakeValidFileName(finalName);

        int l = (int)strlen(GetPath());
        CPathBuffer tgtPath; // Heap-allocated for long path support
        if (l >= tgtPath.Size() - 1) // guard against buffer overflow
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TOOLONGNAME));
            *tryAgain = FALSE;
            return;
        }
        memmove(tgtPath.Get(), GetPath(), l);
        if (GetPath()[l - 1] != '\\')
            tgtPath[l++] = '\\';
        int tgtPathSize = (int)tgtPath.Size();
        if ((int)strlen(finalName) + l < tgtPathSize &&
            ((int)f->NameLen + l < tgtPathSize ||
             (f->DosName != NULL && (int)strlen(f->DosName) + l < tgtPathSize)))
        {
            strcpy(tgtPath + l, finalName);
            CPathBuffer path; // Heap-allocated for long path support
            strcpy(path, GetPath());
            char* end = path + l;
            if (*(end - 1) != '\\')
                *--end = '\\';
            if ((int)f->NameLen + l < (int)path.Size())
                strcpy(path + l, f->Name);
            else
                strcpy(path + l, f->DosName);

            const std::wstring emptyWidePath;
            if (sally::unicode::ArePathsExactlySame(path, tgtPath, emptyWidePath, emptyWidePath))
            {
                *tryAgain = FALSE;
                return; // no-op rename (same name)
            }

            BOOL ret = FALSE;

            BOOL handsOFF = FALSE;
            CFilesWindow* otherPanel = MainWindow->GetNonActivePanel();
            int otherPanelPathLen = (int)strlen(otherPanel->GetPath());
            int pathLen = (int)strlen(path);
            // are we changing the path of the other panel?
            if (otherPanelPathLen >= pathLen &&
                StrNICmp(path, otherPanel->GetPath(), pathLen) == 0 &&
                (otherPanelPathLen == pathLen ||
                 otherPanel->GetPath()[pathLen] == '\\'))
            {
                otherPanel->HandsOff(TRUE);
                handsOFF = TRUE;
            }

            *mayChange = TRUE;

            // try renaming from the long name first and if there is a problem then
            // from the DOS name (handles files/directories accessible only via Unicode or DOS names)
            std::wstring pathW = AnsiToWide(path);
            std::wstring tgtPathW = AnsiToWide(tgtPath);
            BOOL moveRet = MoveFileW(pathW.c_str(), tgtPathW.c_str());
            DWORD err = 0;
            if (!moveRet)
            {
                err = GetLastError();
                if ((err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME) &&
                    f->DosName != NULL)
                {
                    strcpy(path + l, f->DosName);
                    pathW = AnsiToWide(path);
                    moveRet = MoveFileW(pathW.c_str(), tgtPathW.c_str());
                    if (!moveRet)
                        err = GetLastError();
                    strcpy(path + l, f->Name);
                    pathW = AnsiToWide(path);
                }
            }

            if (moveRet)
            {

            REN_OPERATION_DONE:

                strcpy(NextFocusName, tgtPath + l);
                ret = TRUE;
            }
            else
            {
                if (StrICmp(path, tgtPath) != 0 && // if it isn't just change-case
                    (err == ERROR_FILE_EXISTS ||   // check whether it's only rewriting the DOS name of the file
                     err == ERROR_ALREADY_EXISTS))
                {
                    WIN32_FIND_DATAW data;
                    HANDLE find = SalFindFirstFileHW(tgtPath, &data);
                    if (find != INVALID_HANDLE_VALUE)
                    {
                        HANDLES(FindClose(find));
                        const char* tgtName = SalPathFindFileName(tgtPath);
                        char cAltNameA[14];
                        WideCharToMultiByte(CP_ACP, 0, data.cAlternateFileName, -1, cAltNameA, 14, NULL, NULL);
                        char cFileNameA[MAX_PATH];
                        WideCharToMultiByte(CP_ACP, 0, data.cFileName, -1, cFileNameA, MAX_PATH, NULL, NULL);
                        if (StrICmp(tgtName, cAltNameA) == 0 && // match only for DOS name
                            StrICmp(tgtName, cFileNameA) != 0)  // (full name differs)
                        {
                            // rename ("clean up") the file/directory with the conflicting DOS name to a temporary 8.3 name (no extra DOS name needed)
                            std::wstring tmpNameW = tgtPathW;
                            CutDirectoryW(tmpNameW);
                            SalPathAddBackslashW(tmpNameW);
                            size_t tmpNamePartPos = tmpNameW.size();
                            std::wstring origFullNameW = tmpNameW;
                            SalPathAppendW(origFullNameW, data.cFileName); // use wide cFileName directly
                            tmpNameW = origFullNameW;
                            {
                                DWORD num = (GetTickCount() / 10) % 0xFFF;
                                while (1)
                                {
                                    wchar_t tmpSuffix[8];
                                    swprintf(tmpSuffix, _countof(tmpSuffix), L"sal%03X", num++);
                                    tmpNameW.resize(tmpNamePartPos);
                                    tmpNameW += tmpSuffix;
                                    if (MoveFileW(origFullNameW.c_str(), tmpNameW.c_str()))
                                        break;
                                    DWORD e = GetLastError();
                                    if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                    {
                                        tmpNameW.clear();
                                        break;
                                    }
                                }
                                if (!tmpNameW.empty()) // if we successfully "cleaned" the conflicting file, try moving
                                {                      // the file again, then return the temporary file its original name
                                    BOOL moveDone = MoveFileW(pathW.c_str(), tgtPathW.c_str());
                                    if (!MoveFileW(tmpNameW.c_str(), origFullNameW.c_str()))
                                    { // this can apparently happen: Windows creates a file named origFullName instead of 'tgtPath' (DOS name)
                                        TRACE_I("CFilesWindow::RenameFileInternal(): Unexpected situation: unable to rename file from tmp-name to original long file name!");
                                        if (moveDone)
                                        {
                                            if (MoveFileW(tgtPathW.c_str(), pathW.c_str()))
                                                moveDone = FALSE;
                                            if (!MoveFileW(tmpNameW.c_str(), origFullNameW.c_str()))
                                                TRACE_E("CFilesWindow::RenameFileInternal(): Fatal unexpected situation: unable to rename file from tmp-name to original long file name!");
                                        }
                                    }

                                    if (moveDone)
                                        goto REN_OPERATION_DONE;
                                }
                            }
                        }
                    }
                }
                if ((err == ERROR_ALREADY_EXISTS ||
                     err == ERROR_FILE_EXISTS) &&
                    StrICmp(path, tgtPath) != 0) // overwrite the file?
                {
                    DWORD inAttr = GetFileAttributesW(pathW.c_str());
                    DWORD outAttr = GetFileAttributesW(tgtPathW.c_str());

                    if ((inAttr & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                        (outAttr & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    { // only if both are files
                        HANDLE in = CreateFileW(pathW.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, in, GetLastError(), TRUE);
                        HANDLE out = CreateFileW(tgtPathW.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, out, GetLastError(), TRUE);
                        if (in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE)
                        {
                            char iAttr[101], oAttr[101];
                            GetFileOverwriteInfo(iAttr, _countof(iAttr), in, path);
                            GetFileOverwriteInfo(oAttr, _countof(oAttr), out, tgtPath);
                            HANDLES(CloseHandle(in));
                            HANDLES(CloseHandle(out));

                            COverwriteDlg dlg(HWindow, tgtPath, oAttr, path, iAttr, TRUE);
                            int res = (int)dlg.Execute();

                            switch (res)
                            {
                            case IDCANCEL:
                                ret = TRUE;
                            case IDNO:
                                err = ERROR_SUCCESS;
                                break;

                            case IDYES:
                            {
                                ClearReadOnlyAttrW(tgtPathW.c_str()); // so it can be deleted ...
                                if (!gFileSystem->DeleteFile(tgtPathW.c_str()).success || !MoveFileW(pathW.c_str(), tgtPathW.c_str()))
                                    err = GetLastError();
                                else
                                {
                                    err = ERROR_SUCCESS;
                                    ret = TRUE;
                                    strcpy(NextFocusName, tgtPath + l);
                                }
                                break;
                            }
                            }
                        }
                        else
                        {
                            if (in == INVALID_HANDLE_VALUE)
                                TRACE_E("Unable to open file " << path);
                            else
                                HANDLES(CloseHandle(in));
                            if (out == INVALID_HANDLE_VALUE)
                                TRACE_E("Unable to open file " << tgtPath);
                            else
                                HANDLES(CloseHandle(out));
                        }
                    }
                }

                if (err != ERROR_SUCCESS)
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextW(err));
                }
            }
            if (handsOFF)
                otherPanel->HandsOff(FALSE);
            *tryAgain = !ret;
        }
        else
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), LoadStrW(IDS_TOOLONGNAME));
        }
    }
    else
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextW(ERROR_INVALID_NAME));
    }
}

void CFilesWindow::RenameFileInternalW(CFileData* f, const std::wstring& newName, BOOL* mayChange, BOOL* tryAgain)
{
    *tryAgain = TRUE;

    // Validate the new filename - check for invalid characters
    for (wchar_t c : newName)
    {
        if (c == L'?' || c == L'*' || c == L'\\' || c == L'/' || c == L':' || c < 32 ||
            c == L'<' || c == L'>' || c == L'|' || c == L'"')
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextW(ERROR_INVALID_NAME));
            return;
        }
    }

    if (newName.empty())
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextW(ERROR_INVALID_NAME));
        return;
    }

    std::wstring pathW = sally::unicode::EffectivePanelPathW(GetPath(), GetPathW());
    std::wstring srcPath = sally::unicode::BuildPanelChildPathW(pathW, f->Name, f->NameW);

    // Build target path
    std::wstring tgtPath = pathW;
    if (!tgtPath.empty() && tgtPath.back() != L'\\')
        tgtPath += L'\\';
    tgtPath += newName;

    if (sally::unicode::ArePathsExactlySame(NULL, NULL, srcPath, tgtPath))
    {
        *tryAgain = FALSE;
        return; // no-op rename (same name)
    }

    // Check if other panel needs to be notified
    BOOL handsOFF = FALSE;
    CFilesWindow* otherPanel = MainWindow->GetNonActivePanel();
    std::wstring otherPathW = sally::unicode::EffectivePanelPathW(otherPanel->GetPath(),
                                                                  otherPanel->GetPathW());

    if (otherPathW.length() >= srcPath.length() &&
        _wcsnicmp(srcPath.c_str(), otherPathW.c_str(), srcPath.length()) == 0 &&
        (otherPathW.length() == srcPath.length() || otherPathW[srcPath.length()] == L'\\'))
    {
        otherPanel->HandsOff(TRUE);
        handsOFF = TRUE;
    }

    *mayChange = TRUE;

    IFileSystem* fileSystem = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
    FileResult moveResult = fileSystem->MoveFile(srcPath.c_str(), tgtPath.c_str());

    if (moveResult.success)
    {
        NextFocusNameW = newName;
        WideToAnsi(newName, NextFocusName, NextFocusName.Size());
        *tryAgain = FALSE;
    }
    else
    {
        DWORD err = moveResult.errorCode;
        if (err != ERROR_SUCCESS)
            gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextW(err));
        *tryAgain = sally::unicode::ShouldRetryUnicodeRenameAfterError(err);
    }

    if (handsOFF)
        otherPanel->HandsOff(FALSE);
}

void CFilesWindow::RenameFile(int specialIndex)
{
    CALL_STACK_MESSAGE2("CFilesWindow::RenameFile(%d)", specialIndex);

    int i;
    if (specialIndex != -1)
        i = specialIndex;
    else
        i = GetCaretIndex();
    if (i < 0 || i >= Dirs->Count + Files->Count)
        return; // invalid index

    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (strcmp(Dirs->At(0).Name, "..") == 0);
    else
        subDir = FALSE;
    if (i == 0 && subDir)
        return; // we do not work with ".."

    CFileData* f = NULL;
    BOOL isDir = i < Dirs->Count;
    f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);

    BOOL useUnicode = f->UseWideName() || sally::unicode::WidePathNeedsExactPreservation(GetPathW());
    CPathBuffer formatedFileName; // Heap-allocated for long path support
    AlterFileName(formatedFileName, f->Name, -1, Configuration.FileNameFormat, 0, isDir);

    char buff[200];
    sprintf(buff, LoadStr(IDS_RENAME_TO), LoadStr(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE));
    CTruncatedString subject;
    subject.Set(buff, useUnicode ? "..." : formatedFileName.Get());
    std::wstring initialRenameNameW = (f->NameW != NULL && f->NameW[0] != L'\0') ? f->NameW : AnsiToWide(formatedFileName.Get());
    if (useUnicode && f->NameW != NULL && f->NameW[0] != L'\0')
    {
        RepairLossyQuickRenameHistoryForCurrentName(Configuration.QuickRenameHistoryW, QUICKRENAME_HISTORY_SIZE,
                                                    f->Name, f->NameW);
    }
    CCopyMoveDialog dlg(HWindow, formatedFileName, formatedFileName.Size(), LoadStr(IDS_RENAME_TITLE),
                        &subject, IDD_RENAMEDIALOG, Configuration.QuickRenameHistory,
                        QUICKRENAME_HISTORY_SIZE, FALSE,
                        Configuration.QuickRenameHistoryW, QUICKRENAME_HISTORY_SIZE);
    dlg.SetUnicodePath(initialRenameNameW);

    if (Is(ptDisk)) // rename on disk
    {
#ifndef _WIN64
        if (Windows64Bit && isDir)
        {
            CPathBuffer pathBuf(GetPath());
            SalPathAppend(pathBuf.Get(), f->Name, pathBuf.Size());
            if (IsWin64RedirectedDir(pathBuf, NULL, FALSE))
            {
                gPrompter->ShowError(LoadStr(IDS_ERRORTITLE), pathBuf.Get());
                return;
            }
        }
#endif // _WIN64

        BeginSuspendMode(); // snooper takes a break

        BOOL mayChange = FALSE;
        while (1)
        {
            // if no item is selected, select the one under focus and store its name
            CPathBuffer temporarySelected; // Heap-allocated for long path support
            SelectFocusedItemAndGetName(temporarySelected, temporarySelected.Size());

            // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
            // the same code appears here four times
            if (!Configuration.QuickRenameSelectAll)
            {
                int selectionEnd = -1;
                if (!isDir)
                {
                    if (useUnicode)
                    {
                        const wchar_t* dotW = wcsrchr(initialRenameNameW.c_str(), L'.');
                        if (dotW != NULL && dotW > initialRenameNameW.c_str())
                            selectionEnd = (int)(dotW - initialRenameNameW.c_str());
                    }
                    else
                    {
                        const char* dot = strrchr(formatedFileName, '.');
                        if (dot != NULL && dot > formatedFileName)
                            selectionEnd = (int)(dot - formatedFileName);
                    }
                }
                dlg.SetSelectionEnd(selectionEnd);
            }

            int dlgRes = (int)dlg.Execute();

            // if we selected an item, we deselect it again
            UnselectItemWithName(temporarySelected);

            if (dlgRes == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);

                BOOL tryAgain;
                // Use Unicode path if: 1) file has Unicode name, or 2) path is too long for ANSI
                std::wstring renameResultW = dlg.GetUnicodeResult();
                if (useUnicode && f->NameW != NULL && !renameResultW.empty())
                {
                    renameResultW = sally::unicode::RecoverWideCharsFromLossyInput(renameResultW,
                                                                                   AnsiToWide(f->Name),
                                                                                   f->NameW);
                }
                BOOL pathTooLong = (wcslen(GetPathW()) >= MAX_PATH || strlen(GetPath()) >= MAX_PATH);
                BOOL unicodeNeedsWidePath = !renameResultW.empty() && WideStringUsesAnsiFallback(renameResultW);
                BOOL panelPathNeedsWide = sally::unicode::WidePathNeedsExactPreservation(GetPathW());
                if ((!renameResultW.empty() && (useUnicode || unicodeNeedsWidePath || panelPathNeedsWide)) ||
                    pathTooLong)
                {
                    std::wstring newNameW;
                    if (!renameResultW.empty())
                    {
                        newNameW = renameResultW;
                    }
                    else
                    {
                        // Convert ANSI filename to Unicode for long path handling
                        newNameW = AnsiToWide(formatedFileName);
                    }
                    RenameFileInternalW(f, newNameW, &mayChange, &tryAgain);
                }
                else
                {
                    RenameFileInternal(f, formatedFileName, &mayChange, &tryAgain);
                }
                if (!tryAgain)
                    break;
            }
            else
                break;
        }

        // refresh of manually refreshed directories
        if (mayChange)
        {
            // change in the directory shown in the panel and, if a directory was renamed, then also in subdirectories
            MainWindow->PostChangeOnPathNotificationW(GetPathW(), isDir);
        }

        // if a Salamander window is active, end suspend mode
        EndSuspendMode();
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
        {
            BeginSuspendMode(); // snooper takes a break

            // lower the thread priority to "normal" (so operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            CPathBuffer newName; // Heap-allocated for long path support
            newName[0] = 0;
            BOOL cancel = FALSE;

            // if no item is selected, select the one under focus and store its name
            CPathBuffer temporarySelected; // Heap-allocated for long path support
            SelectFocusedItemAndGetName(temporarySelected, temporarySelected.Size());

            BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 1, HWindow, *f, isDir, newName, cancel);

            // if we selected an item, we deselect it again
            UnselectItemWithName(temporarySelected);

            if (!cancel) // not a cancel of the operation
            {
                if (!ret)
                {
                    while (1)
                    {
                        // open the standard dialog
                        // if no item is selected, select the one under focus and store its name
                        SelectFocusedItemAndGetName(temporarySelected, temporarySelected.Size());

                        // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
                        // the same code appears here four times
                        if (!Configuration.QuickRenameSelectAll)
                        {
                            int selectionEnd = -1;
                            if (!isDir)
                            {
                                const char* dot = strrchr(formatedFileName, '.');
                                if (dot != NULL && dot > formatedFileName) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                                                                           //        if (dot != NULL)
                                    selectionEnd = (int)(dot - formatedFileName);
                            }
                            dlg.SetSelectionEnd(selectionEnd);
                        }

                        int dlgRes = (int)dlg.Execute();

                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);

                        if (dlgRes == IDOK)
                        {
                            strcpy(newName, formatedFileName);
                            ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 2, HWindow, *f, isDir, newName, cancel);
                            if (ret || cancel)
                                break; // not an error (cancel or success)
                            strcpy(formatedFileName, newName);
                        }
                        else
                        {
                            WaitForESCRelease();
                            cancel = TRUE;
                            break;
                        }
                    }
                }

                if (ret && !cancel) // operation completed successfully
                {
                    strcpy(NextFocusName, newName); // ensure focus of the new name after refresh
                }
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            // if a Salamander window is active, end suspend mode
            EndSuspendMode();
        }
    }
}

void CFilesWindow::CancelUI()
{
    if (QuickSearchMode)
        EndQuickSearch();
    QuickRenameEnd();
}

BOOL CFilesWindow::IsQuickRenameActive()
{
    return QuickRenameWindow.HWindow != NULL;
}

void CFilesWindow::AdjustQuickRenameRect(const char* text, RECT* r)
{
    // measure the length of the text
    HDC hDC = HANDLES(GetDC(ListBox->HWindow));
    HFONT hOldFont = (HFONT)SelectObject(hDC, Font);
    SIZE sz;
    GetTextExtentPoint32(hDC, text, (int)strlen(text), &sz);
    TEXTMETRIC tm;
    GetTextMetrics(hDC, &tm);
    SelectObject(hDC, hOldFont);
    HANDLES(ReleaseDC(ListBox->HWindow, hDC));

    int minWidth = QuickRenameRect.right - QuickRenameRect.left + 2;
    int minHeight = QuickRenameRect.bottom - QuickRenameRect.top;

    int optimalWidth = sz.cx + 4 + tm.tmHeight;

    r->left--;

    r->right = r->left + optimalWidth;

    if (r->right - r->left < minWidth)
        r->right = r->left + minWidth;

    // we do not want to exceed the panel boundaries
    RECT maxR = ListBox->FilesRect;
    if (r->left < maxR.left)
        r->left = maxR.left;
    if (r->right > maxR.right)
        r->right = maxR.right;
}

void CFilesWindow::AdjustQuickRenameWindow()
{
    if (!IsQuickRenameActive())
    {
        //    TRACE_E("QuickRenameWindow is not active.");
        return;
    }

    RECT r;
    GetWindowRect(QuickRenameWindow.HWindow, &r);
    MapWindowPoints(NULL, HWindow, (POINT*)&r, 2);

    CPathBuffer buff; // Heap-allocated for long path support
    GetWindowText(QuickRenameWindow.HWindow, buff, buff.Size());
    AdjustQuickRenameRect(buff, &r);
    SetWindowPos(QuickRenameWindow.HWindow, NULL, 0, 0,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

/*
// I ran into a sorting issue: Vista keeps items in place, but Salamander needs to insert them
// so I'm shelving this for now
void
CFilesWindow::QuickRenameOnIndex(int index)
{
  if (index >= 0 && index < Dirs->Count + Files->Count)
  {
    QuickRenameIndex = index;
    SetCaretIndex(index, FALSE);

    RECT r;
    if (ListBox->GetItemRect(index, &r))
    {
      ListBox->GetIndex(r.left, r.top, FALSE, &QuickRenameRect);
      QuickRenameIndex = index;
      QuickRenameBegin(index, &QuickRenameRect);
    }
  }
}
*/

void CFilesWindow::QuickRenameBegin(int index, const RECT* labelRect)
{
    CALL_STACK_MESSAGE2("CFilesWindow::QuickRenameBegin(%d, )", index);

    if (!(Is(ptDisk) || Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
                            GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)))
        return;

    if (QuickRenameWindow.HWindow != NULL)
    {
        TRACE_E("Quick Rename is already active");
        return;
    }

    if (index < 0 || index >= Dirs->Count + Files->Count)
        return; // invalid index

    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (strcmp(Dirs->At(0).Name, "..") == 0);
    else
        subDir = FALSE;
    if (index == 0 && subDir)
        return; // we do not work with ".."

    CFileData* f = NULL;
    BOOL isDir = index < Dirs->Count;
    f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);

    CPathBuffer formatedFileName; // Heap-allocated for long path support
    AlterFileName(formatedFileName, f->Name, -1, Configuration.FileNameFormat, 0, isDir);

    // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
    // the same code appears here four times
    int selectionEnd = -1;
    if (!Configuration.QuickRenameSelectAll)
    {
        if (!isDir)
        {
            const char* dot = strrchr(formatedFileName, '.');
            if (dot != NULL && dot > formatedFileName.Get()) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                                                       //    if (dot != NULL)
                selectionEnd = (int)(dot - formatedFileName.Get());
        }
    }

    // if this is a FS, we must first call QuickRename with mode=1
    // allowing the file system to open its own rename dialog
    if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
        GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
    {
        BeginSuspendMode(); // snooper takes a break

        // lower the thread priority to "normal" (so operations don't overload the machine)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

        CPathBuffer newName; // Heap-allocated for long path support
        newName[0] = 0;
        BOOL cancel = FALSE;

        // if no item is selected, select the one under focus and store its name
        CPathBuffer temporarySelected; // Heap-allocated for long path support
        SelectFocusedItemAndGetName(temporarySelected, temporarySelected.Size());

        BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 1, HWindow, *f, isDir, newName, cancel);

        // if we selected an item, we deselect it again
        UnselectItemWithName(temporarySelected);

        if (ret && !cancel) // operation completed successfully
        {
            strcpy(NextFocusName, newName); // ensure focus of the new name after refresh
        }

        // raise the thread priority again, the operation has finished
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        // if a Salamander window is active, end suspend mode
        EndSuspendMode();

        if (cancel || ret)
            return;
    }

    RECT r = *labelRect;
    AdjustQuickRenameRect(formatedFileName, &r);

    HWND hWnd = QuickRenameWindow.CreateEx(0,
                                           "edit",
                                           formatedFileName,
                                           WS_BORDER | WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_LEFT,
                                           r.left, r.top, r.right - r.left, r.bottom - r.top,
                                           GetListBoxHWND(),
                                           NULL,
                                           HInstance,
                                           &QuickRenameWindow);
    if (hWnd == NULL)
    {
        TRACE_E("Cannot create QuickRenameWindow");
        return;
    }

    BeginSuspendMode(TRUE); // snooper takes a break

    // font the same as the panel
    SendMessage(hWnd, WM_SETFONT, (WPARAM)Font, 0);
    int leftMargin = LOWORD(SendMessage(hWnd, EM_GETMARGINS, 0, 0));
    if (leftMargin < 2)
        SendMessage(hWnd, EM_SETMARGINS, EC_LEFTMARGIN, 2);

    //SendMessage(hWnd, EM_SETSEL, 0, -1); // select all
    // we can select only the name without dot and extension
    SendMessage(hWnd, EM_SETSEL, 0, selectionEnd);

    ShowWindow(hWnd, SW_SHOW);
    SetFocus(hWnd);
    return;
}

void CFilesWindow::QuickRenameEnd()
{
    CALL_STACK_MESSAGE1("CFilesWindow::QuickRenameEnd()");
    if (QuickRenameWindow.HWindow != NULL && QuickRenameWindow.GetCloseEnabled())
    {
        // if a Salamander window is active, end suspend mode
        EndSuspendMode(TRUE);

        // avoid cycles caused by WM_KILLFOCUS and similar
        BOOL old = QuickRenameWindow.GetCloseEnabled();
        QuickRenameWindow.SetCloseEnabled(FALSE);

        DestroyWindow(QuickRenameWindow.HWindow);

        QuickRenameWindow.SetCloseEnabled(old);
    }
}

BOOL CFilesWindow::HandeQuickRenameWindowKey(WPARAM wParam)
{
    CALL_STACK_MESSAGE2("CFilesWindow::HandeQuickRenameWindowKey(0x%IX)", wParam);

    if (wParam == VK_ESCAPE)
    {
        QuickRenameEnd();
        return TRUE;
    }

    int index = GetCaretIndex();
    if (index < 0 || index >= Dirs->Count + Files->Count)
        return TRUE; // invalid index
    CFileData* f = NULL;
    BOOL isDir = index < Dirs->Count;
    f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);

    QuickRenameWindow.SetCloseEnabled(FALSE);

    HWND hWnd = QuickRenameWindow.HWindow;
    CPathBuffer newName; // Heap-allocated for long path support
    GetWindowText(hWnd, newName, newName.Size());

    // lower the thread priority to "normal" (so operations don't overload the machine)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    BOOL tryAgain = FALSE;
    BOOL mayChange = FALSE;
    if (Is(ptDisk))
    {
        // If this is an in-place rename and the user didn't change the name, we shouldn't
        // attempt to rename it because the user might be on a CD-ROM or other read-only disk
        // and we would display the "Access is denied" error. The user has no mouse option
        // to cancel the operation, so they would have to press Escape.
        // Explorer behaves this way now.
        if (strcmp(f->Name, newName) != 0)
            RenameFileInternal(f, newName, &mayChange, &tryAgain);
    }
    else if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
             GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
    {
        // open the standard dialog
        BOOL cancel;
        BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 2, HWindow, *f, isDir, newName, cancel);
        if (!ret && !cancel)
        {
            tryAgain = TRUE;
            SetWindowText(hWnd, newName);
        }
        else
        {
            if (ret && !cancel) // operation completed successfully
            {
                strcpy(NextFocusName, newName); // ensure focus of the new name after refresh
            }
        }
    }

    // raise the thread priority again, the operation has finished
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // refresh of manually refreshed directories
    if (mayChange)
    {
        // change in the directory shown in the panel and if a directory was renamed, then also in subdirectories
        MainWindow->PostChangeOnPathNotification(GetPath(), isDir);
    }

    QuickRenameWindow.SetCloseEnabled(TRUE);
    if (!tryAgain)
    {
        QuickRenameEnd();
        //    if (wParam == VK_TAB)
        //    {
        //      BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        //      PostMessage(HWindow, WM_USER_RENAME_NEXT_ITEM, !shiftPressed, 0);
        //    }
        return TRUE;
    }
    else
    {
        SetFocus(QuickRenameWindow.HWindow);
        return FALSE;
    }
}

void CFilesWindow::KillQuickRenameTimer()
{
    if (QuickRenameTimer != 0)
    {
        KillTimer(GetListBoxHWND(), QuickRenameTimer);
        QuickRenameTimer = 0;
    }
}

//****************************************************************************
//
// CQuickRenameWindow
//

CQuickRenameWindow::CQuickRenameWindow()
    : CWindow(ooStatic)
{
    FilesWindow = NULL;
    CloseEnabled = TRUE;
    SkipNextCharacter = FALSE;
}

void CQuickRenameWindow::SetPanel(CFilesWindow* filesWindow)
{
    FilesWindow = filesWindow;
}

void CQuickRenameWindow::SetCloseEnabled(BOOL closeEnabled)
{
    CloseEnabled = closeEnabled;
}

BOOL CQuickRenameWindow::GetCloseEnabled()
{
    return CloseEnabled;
}

LRESULT
CQuickRenameWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CHAR:
    {
        if (SkipNextCharacter)
        {
            SkipNextCharacter = FALSE; // prevent a beep
            return FALSE;
        }

        if (wParam == VK_ESCAPE || wParam == VK_RETURN /*|| wParam == VK_TAB*/)
        {
            FilesWindow->HandeQuickRenameWindowKey(wParam);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (wParam == 'A')
        {
            // since Windows Vista, SelectAll works by default, so we leave select-all to them
            if (!WindowsVistaAndLater)
            {
                BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (controlPressed && !shiftPressed && !altPressed)
                {
                    SendMessage(HWindow, EM_SETSEL, 0, -1);
                    SkipNextCharacter = TRUE; // prevent a beep
                    return 0;
                }
            }
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
