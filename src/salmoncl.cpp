// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "salmoncl.h"
#include "common/unicode/helpers.h"

#define MAX_ENV_PATH 32766

CSalmonSharedMemory* SalmonSharedMemory = NULL;
HANDLE SalmonFileMapping = NULL;
HANDLE HSalmonProcess = NULL;

//****************************************************************************

// WARNING: we are running from entry point, before RTL initialization, global objects, etc.
// do not call TRACE, HANDLES, RTL, ...

HANDLE GetBugReporterRegistryMutex()
{
    // permissions fully open for all processes
    SECURITY_ATTRIBUTES secAttr;
    char secDesc[SECURITY_DESCRIPTOR_MIN_LENGTH];
    secAttr.nLength = sizeof(secAttr);
    secAttr.bInheritHandle = FALSE;
    secAttr.lpSecurityDescriptor = &secDesc;
    InitializeSecurityDescriptor(secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(secAttr.lpSecurityDescriptor, TRUE, 0, FALSE);
    // it would be convenient to add SID to the mutex name, because processes with different SID run with a different HKCU tree
    // but for simplicity we skip that and the mutex will be truly global
    const char* MUTEX_NAME = SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_A;
    HANDLE hMutex = NOHANDLES(CreateMutex(&secAttr, FALSE, MUTEX_NAME));
    if (hMutex == NULL) // create can already open an existing mutex, but it can fail, so we try open afterwards
        hMutex = NOHANDLES(OpenMutex(SYNCHRONIZE, FALSE, MUTEX_NAME));
    return hMutex;
}

BOOL SalmonGetBugReportUID(DWORD64* uid)
{
    const char* BUG_REPORTER_KEY = SAL_REG_KEY_BUG_REPORTER_A;
    const char* BUG_REPORTER_UID = SAL_REG_VALUE_BUG_REPORTER_UID_A;

    // this section runs at Salamander startup and theoretically concurrent registry read/write can occur
    // therefore we will guard access with a global mutex
    HANDLE hMutex = GetBugReporterRegistryMutex();
    if (hMutex != NULL)
        WaitForSingleObject(hMutex, INFINITE);
    *uid = 0;
    HKEY hKey;
    LONG res = NOHANDLES(RegOpenKeyEx(HKEY_CURRENT_USER, BUG_REPORTER_KEY, 0, KEY_READ, &hKey));
    if (res == ERROR_SUCCESS)
    {
        // try to load the old value if it exists
        DWORD gettedType;
        DWORD bufferSize = sizeof(*uid);
        res = RegQueryValueEx(hKey, BUG_REPORTER_UID, 0, &gettedType, (BYTE*)uid, &bufferSize);
        if (res != ERROR_SUCCESS || gettedType != REG_QWORD)
            *uid = 0;
        NOHANDLES(RegCloseKey(hKey));
    }
    // if UID does not exist yet, we create and save it
    if (*uid == 0)
    {
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK)
        {
            // we won't store and send the entire GUID, half of it XORed with the other half is enough
            DWORD64* dw64 = (DWORD64*)&guid;
            *uid = dw64[0] ^ dw64[1];

            // try to save it
            DWORD createType;
            LONG res2 = NOHANDLES(RegCreateKeyEx(HKEY_CURRENT_USER, BUG_REPORTER_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                                                 KEY_READ | KEY_WRITE, NULL, &hKey, &createType));
            if (res2 == ERROR_SUCCESS)
            {
                res2 = RegSetValueEx(hKey, BUG_REPORTER_UID, 0, REG_QWORD, (BYTE*)uid, sizeof(*uid));
                if (res2 != ERROR_SUCCESS)
                    *uid = 0; // on failure we want zero
                NOHANDLES(RegCloseKey(hKey));
            }
        }
    }
    if (hMutex != NULL)
    {
        ReleaseMutex(hMutex);
        NOHANDLES(CloseHandle(hMutex));
    }

    return TRUE;
}

BOOL SalmonSharedMemInit(CSalmonSharedMemory* mem)
{
    SECURITY_ATTRIBUTES sa; // allow handle inheritance to child process (they can then work directly with our event)
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    RtlFillMemory(mem, sizeof(CSalmonSharedMemory), 0);

    mem->Version = SALMON_SHARED_MEMORY_VERSION;
    // salmon will be started as a child process with bInheritHandles==TRUE, so it can access these handles directly
    mem->ProcessId = GetCurrentProcessId();
    mem->Process = NOHANDLES(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, TRUE, mem->ProcessId));
    mem->Fire = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));      // "nonsignaled" state, manual
    mem->Done = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));      // "nonsignaled" state, manual
    mem->SetSLG = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));    // "nonsignaled" state, manual
    mem->CheckBugs = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL)); // "nonsignaled" state, manual

    // we now put the path for bug reports into LOCAL_APPDATA, where Windows WER stores minidumps by default
    // we don't create the path immediately, we take care of that at the moment of the crash
    if (SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, mem->BugPath) == S_OK)
    {
        int len = lstrlen(mem->BugPath);
        if (len > 0 && mem->BugPath[len - 1] == '\\') // better verify the trailing backslash at the end of the path
            mem->BugPath[len - 1] = 0;
        lstrcat(mem->BugPath, "\\Open Salamander");
    }

    // base name for bug reports
    strcpy(mem->BugName, "AS" VERSINFO_SAL_SHORT_VERSION);

    return (mem->Process != NULL && mem->Fire != NULL && mem->Done != NULL && mem->SetSLG != NULL &&
            mem->CheckBugs != NULL && mem->BugPath[0] != 0);
}

void GetStartupSLGName(char* slgName, DWORD slgNameMax)
{
    // extract from registry the SLG name that will probably be used
    // later during Salamander runtime a different one may be selected, which will be changed afterwards
    // this serves only as a default; if the record is not found, we pass an empty string
    slgName[0] = 0;

    CPathBuffer keyName; // Heap-allocated for long path support
    sprintf(keyName.Get(), "%s\\%s", SalamanderConfigurationRoots[0], SALAMANDER_CONFIG_REG);
    HKEY hKey;
    LONG res = NOHANDLES(RegOpenKeyEx(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &hKey));
    if (res == ERROR_SUCCESS)
    {
        DWORD gettedType;
        res = SalRegQueryValueEx(hKey, CONFIG_LANGUAGE_REG, 0, &gettedType, (BYTE*)slgName, &slgNameMax);
        if (res != ERROR_SUCCESS || gettedType != REG_SZ)
            slgName[0] = 0;
        RegCloseKey(hKey);
    }
}

BOOL SalmonStartProcess(const char* fileMappingName) //Configuration.LoadedSLGName
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    CPathBuffer slgName; // Heap-allocated for long path support (registry value, ASCII filename)

    HSalmonProcess = NULL;

    // issue #63: wide install paths so non-ASCII directories (Cyrillic, etc.)
    // don't get mangled by ANSI GetModuleFileName -> CreateProcess.
    std::wstring exePath(SAL_MAX_LONG_PATH, L'\0');
    DWORD moduleLen = GetModuleFileNameW(NULL, &exePath[0], SAL_MAX_LONG_PATH);
    if (moduleLen == 0 || moduleLen >= SAL_MAX_LONG_PATH)
        return FALSE;
    exePath.resize(moduleLen);
    size_t slash = exePath.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return FALSE;
    std::wstring rtlDirW(exePath, 0, slash + 1);
    std::wstring salmonExeW = rtlDirW + L"utils\\salmon.exe";

    GetStartupSLGName(slgName, slgName.Size());

    std::wstring cmdW;
    cmdW.reserve(salmonExeW.size() + lstrlenA(fileMappingName) + 64);
    cmdW.append(L"\"").append(salmonExeW).append(L"\"");
    cmdW.append(L" \"").append(AnsiToWide(fileMappingName)).append(L"\"");
    cmdW.append(L" \"").append(AnsiToWide(slgName.Get())).append(L"\"");

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.wShowWindow = SW_SHOWNORMAL;

    std::wstring oldCurDirW(SAL_MAX_LONG_PATH, L'\0');
    DWORD oldLen = GetCurrentDirectoryW(SAL_MAX_LONG_PATH, &oldCurDirW[0]);
    if (oldLen == 0 || oldLen >= SAL_MAX_LONG_PATH)
        oldCurDirW.clear();
    else
        oldCurDirW.resize(oldLen);

    // another attempt to solve the problem before we split SALMON.EXE into EXE+DLL:
    // extend PATH for the child so it can see the install-dir-local RTL.
    std::wstring envPATHW(MAX_ENV_PATH, L'\0');
    DWORD envLen = GetEnvironmentVariableW(L"PATH", &envPATHW[0], MAX_ENV_PATH);
    BOOL envExtended = FALSE;
    if (envLen != 0 && envLen < MAX_ENV_PATH)
    {
        envPATHW.resize(envLen);
        if (envPATHW.size() + 1 + rtlDirW.size() < MAX_ENV_PATH)
        {
            std::wstring newPATH = envPATHW;
            newPATH.append(L";").append(rtlDirW);
            SetEnvironmentVariableW(L"PATH", newPATH.c_str());
            envExtended = TRUE;
        }
    }
    else
    {
        envPATHW.clear();
    }

    SetCurrentDirectoryW(rtlDirW.c_str());

    // CreateProcessW takes a writable command-line buffer.
    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(L'\0');

    BOOL ret = FALSE;
    if (NOHANDLES(CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                                 CREATE_DEFAULT_ERROR_MODE | HIGH_PRIORITY_CLASS, NULL,
                                 rtlDirW.c_str(), &si, &pi)))
    {
        HSalmonProcess = pi.hProcess;
        AllowSetForegroundWindow(SalGetProcessId(pi.hProcess));
        ret = TRUE;
    }

    if (!oldCurDirW.empty())
        SetCurrentDirectoryW(oldCurDirW.c_str());
    if (envExtended)
        SetEnvironmentVariableW(L"PATH", envPATHW.c_str());
    return ret;
}

//BOOL IsSalmonRunning()
//{
//  if (HSalmonProcess != NULL)
//  {
//    DWORD waitRet = WaitForSingleObject(HSalmonProcess, 0);
//    return waitRet == WAIT_TIMEOUT;
//  }
//  return FALSE;
//}

// We want to learn about SEH Exceptions also on x64 Windows 7 SP1 and later
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibrary("KERNEL32.DLL");
    if (hDLL != NULL)
    {
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");                                                      // Min: XP SP2
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        if (isWow64 != NULL && set != NULL && get != NULL)
        {
            BOOL bIsWow64;
            if (isWow64(GetCurrentProcess(), &bIsWow64) && bIsWow64)
            {
                DWORD dwFlags;
                if (get(&dwFlags))
                    set(dwFlags & ~PROCESS_CALLBACK_FILTER_ENABLED);
            }
        }
        FreeLibrary(hDLL);
    }
}

BOOL SalmonInit()
{
    EnableExceptionsOn64();

    SalmonSharedMemory = NULL;
    char salmonFileMappingName[SALMON_FILEMAPPIN_NAME_SIZE];
    // allocation of shared space in pagefile.sys
    DWORD ti = (GetTickCount() >> 3) & 0xFFF;
    while (TRUE) // looking for a unique name for file-mapping
    {
        wsprintf(salmonFileMappingName, "Salmon%X", ti++);
        SalmonFileMapping = NOHANDLES(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, // FIXME_X64 aren't we passing x86/x64 incompatible data?
                                                        sizeof(CSalmonSharedMemory), salmonFileMappingName));
        if (SalmonFileMapping == NULL || GetLastError() != ERROR_ALREADY_EXISTS)
            break;
        NOHANDLES(CloseHandle(SalmonFileMapping));
    }
    if (SalmonFileMapping != NULL)
    {
        SalmonSharedMemory = (CSalmonSharedMemory*)NOHANDLES(MapViewOfFile(SalmonFileMapping, FILE_MAP_WRITE, 0, 0, 0)); // FIXME_X64 aren't we passing x86/x64 incompatible data?
        if (SalmonSharedMemory != NULL)
        {
            ZeroMemory(SalmonSharedMemory, sizeof(CSalmonSharedMemory));
            if (SalmonSharedMemInit(SalmonSharedMemory))
            {
                SalmonGetBugReportUID(&SalmonSharedMemory->UID);

                // if salmon fails to start, we still return TRUE - problem will be reported later after SLG is loaded
                SalmonStartProcess(salmonFileMappingName);
                return TRUE;
            }
        }
    }
    // a serious (and unexpected) error occurred, we block Salamander startup, message will be in English (we don't have slg)
    return FALSE;
}

// info that salmon is not running needs to be displayed only once
static BOOL SalmonNotRunningReported = FALSE;

void SalmonSetSLG(const char* slgName)
{
    ResetEvent(SalmonSharedMemory->Done);

    strcpy(SalmonSharedMemory->SLGName, slgName);
    SetEvent(SalmonSharedMemory->SetSLG);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    DWORD waitRet = WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    if (waitRet != WAIT_OBJECT_0 + 1) // someone killed salmon or something went wrong in communication
    {
        if (!SalmonNotRunningReported && HLanguage != NULL)
        {
#ifdef _DEBUG
            TRACE_E("Salmon is not running (debug build, suppressing dialog)");
#else
            MessageBox(NULL, LoadStr(IDS_SALMON_NOT_RUNNING), SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
#endif
            SalmonNotRunningReported = TRUE;
        }
    }
    ResetEvent(SalmonSharedMemory->Done);
}

void SalmonCheckBugs()
{
    ResetEvent(SalmonSharedMemory->Done);
    SetEvent(SalmonSharedMemory->CheckBugs);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    DWORD waitRet = WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    if (waitRet != WAIT_OBJECT_0 + 1) // someone killed salmon or something went wrong in communication
    {
        if (!SalmonNotRunningReported && HLanguage != NULL)
        {
#ifdef _DEBUG
            TRACE_E("Salmon is not running (debug build, suppressing dialog)");
#else
            MessageBox(NULL, LoadStr(IDS_SALMON_NOT_RUNNING), SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
#endif
            SalmonNotRunningReported = TRUE;
        }
    }
    ResetEvent(SalmonSharedMemory->Done);
}

BOOL SalmonFireAndWait(const EXCEPTION_POINTERS* e, char* bugReportPath)
{
    SalmonSharedMemory->ThreadId = GetCurrentThreadId();
    SalmonSharedMemory->ExceptionRecord = *e->ExceptionRecord;
    SalmonSharedMemory->ContextRecord = *e->ContextRecord;
    SetEvent(SalmonSharedMemory->Fire);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    ResetEvent(SalmonSharedMemory->Done);

    strcpy(bugReportPath, SalmonSharedMemory->BugPath);
    SalPathAppend(bugReportPath, SalmonSharedMemory->BaseName, MAX_PATH);
    strcat(bugReportPath, ".TXT");

    return TRUE;
}
