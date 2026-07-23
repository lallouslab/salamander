// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "worker.h"
#include "common/HeadlessScriptExecutor.h"
#include "common/unicode/helpers.h"

#include <aclapi.h>
#include <winioctl.h>

#include <algorithm>
#include <vector>

namespace sally::operation_executor
{

static std::wstring OperationSourcePathW(const COperation& op)
{
    if (op.HasWideSource())
        return op.SourceNameW;
    return op.SourceName != NULL ? AnsiToWide(op.SourceName) : std::wstring();
}

static std::wstring OperationTargetPathW(const COperation& op)
{
    if (op.HasWideTarget())
        return op.TargetNameW;
    return op.TargetName != NULL ? AnsiToWide(op.TargetName) : std::wstring();
}

static bool ScriptOperationCountsForProgress(COperationCode opcode)
{
    return opcode != ocLabelForSkipOfCreateDir;
}

static CFileOperationResult ExecuteMoveDirectoryW(IWorkerObserver& observer,
                                                  const std::wstring& sourcePath,
                                                  const std::wstring& targetPath,
                                                  CFileOperationExecutionState& state)
{
    while (true)
    {
        if (MoveFileExW(sourcePath.c_str(), targetPath.c_str(), MOVEFILE_COPY_ALLOWED))
            return SuccessResult();

        DWORD error = GetLastError();
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, "Error moving directory", sourcePath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

static CFileOperationResult ExecuteCopyDirectoryTimeW(IWorkerObserver& observer,
                                                      const std::wstring& targetPath,
                                                      FILETIME lastWrite,
                                                      CFileOperationExecutionState& state)
{
    while (true)
    {
        HANDLE directory = CreateFileW(targetPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (directory != INVALID_HANDLE_VALUE)
        {
            const BOOL ok = SetFileTime(directory, NULL, NULL, &lastWrite);
            const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            CloseHandle(directory);
            if (ok)
                return SuccessResult();

            if (state.SkipAllErrors)
                return SuccessResult(true);
            int response = AskFileError(observer, "Error setting directory time", targetPath, error);
            switch (response)
            {
            case IDRETRY:
                break;
            case IDB_SKIPALL:
                state.SkipAllErrors = true;
                [[fallthrough]];
            case IDB_SKIP:
            case IDB_IGNORE:
            case IDB_ALL:
                return SuccessResult(true);
            case IDCANCEL:
            default:
                return ErrorResult(error);
            }
        }
        else
        {
            DWORD error = GetLastError();
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllErrors)
                return SuccessResult(true);

            int response = AskFileError(observer, "Error opening directory", targetPath, error);
            switch (response)
            {
            case IDRETRY:
                break;
            case IDB_SKIPALL:
                state.SkipAllErrors = true;
                [[fallthrough]];
            case IDB_SKIP:
            case IDB_IGNORE:
            case IDB_ALL:
                return SuccessResult(true);
            case IDCANCEL:
            default:
                return ErrorResult(error);
            }
        }
    }
}

static std::wstring AttributeWritePathW(std::wstring path)
{
    if (!path.empty() && (path.back() <= L' ' || path.back() == L'.'))
        path.push_back(L'\\');
    return path;
}

static std::wstring ADSBasePathW(std::wstring path)
{
    if (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

static std::string ADSErrorTextA(DWORD error)
{
    char buffer[64] = {};
    wsprintfA(buffer, "Error code %lu", error);
    return std::string(buffer);
}

static bool EnumerateADSStreamsW(const std::wstring& path,
                                 std::vector<std::wstring>& streamNames,
                                 DWORD& error)
{
    streamNames.clear();
    error = ERROR_SUCCESS;

    WIN32_FIND_STREAM_DATA data = {};
    HANDLE find = FindFirstStreamW(ADSBasePathW(path).c_str(), FindStreamInfoStandard, &data, 0);
    if (find == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        if (error == ERROR_HANDLE_EOF || error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED)
        {
            error = ERROR_SUCCESS;
            return true;
        }
        return false;
    }

    do
    {
        if (wcscmp(data.cStreamName, L"::$DATA") != 0)
            streamNames.push_back(data.cStreamName);
    } while (FindNextStreamW(find, &data));

    error = GetLastError();
    FindClose(find);
    if (error == ERROR_HANDLE_EOF)
        error = ERROR_SUCCESS;
    return error == ERROR_SUCCESS;
}

static CFileOperationResult HandleADSReadError(IWorkerObserver& observer,
                                               const std::wstring& sourcePath,
                                               const std::wstring& streamName,
                                               CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllADSReadErrors)
        return SuccessResult();

    std::string sourceA = ObserverPathFallbackA(sourcePath);
    std::string streamA = ObserverPathFallbackA(streamName);
    int response = observer.AskADSReadError(sourceA.c_str(), streamA.c_str());
    switch (response)
    {
    case IDB_ALL:
    case IDB_IGNOREALL:
        state.IgnoreAllADSReadErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllADSCopyErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(ERROR_CANCELLED);
    }
}

static CFileOperationResult HandleADSOpenError(IWorkerObserver& observer,
                                               const std::wstring& filePath,
                                               const std::wstring& streamName,
                                               DWORD error,
                                               CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllADSOpenErrors)
        return SuccessResult();
    if (state.SkipAllADSOpenErrors)
        return SuccessResult(true);

    std::string fileA = ObserverPathFallbackA(filePath);
    std::string streamA = ObserverPathFallbackA(streamName);
    std::string errorText = ADSErrorTextA(error);
    int response = observer.AskADSOpenError(fileA.c_str(), streamA.c_str(), errorText.c_str());
    switch (response)
    {
    case IDRETRY:
        return ErrorResult(ERROR_RETRY);
    case IDB_ALL:
    case IDB_IGNOREALL:
        state.IgnoreAllADSOpenErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllADSOpenErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult HandleADSWriteError(IWorkerObserver& observer,
                                                const std::wstring& targetPath,
                                                DWORD error,
                                                CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllADSCopyErrors)
        return SuccessResult(true);

    int response = AskFileError(observer, "Error writing ADS", targetPath, error);
    switch (response)
    {
    case IDRETRY:
        return ErrorResult(ERROR_RETRY);
    case IDB_SKIPALL:
        state.SkipAllADSCopyErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static void CloseADSHandle(HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

static CFileOperationResult CopyOneADSStreamW(IWorkerObserver& observer,
                                              const std::wstring& sourcePath,
                                              const std::wstring& targetPath,
                                              const std::wstring& streamName,
                                              CFileOperationExecutionState& state)
{
    if (state.SkipAllADSCopyErrors)
        return SuccessResult(true);

    const std::wstring sourceStream = ADSBasePathW(sourcePath) + streamName;
    const std::wstring targetStream = ADSBasePathW(targetPath) + streamName;
    const DWORD streamFlags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS;

    while (true)
    {
        HANDLE input = CreateFileW(sourceStream.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, streamFlags, NULL);
        if (input == INVALID_HANDLE_VALUE)
        {
            CFileOperationResult openResult =
                HandleADSOpenError(observer, sourcePath, streamName, GetLastError(), state);
            if (!openResult.success && openResult.lastError == ERROR_RETRY)
                continue;
            return openResult;
        }

        HANDLE output = CreateFileW(targetStream.c_str(), GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, streamFlags, NULL);
        if (output == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            CloseADSHandle(input);
            CFileOperationResult openResult =
                HandleADSOpenError(observer, targetPath, streamName, error, state);
            if (!openResult.success && openResult.lastError == ERROR_RETRY)
                continue;
            return openResult;
        }

        bool retryStream = false;
        std::vector<char> buffer(64 * 1024);
        while (true)
        {
            DWORD read = 0;
            if (!ReadFile(input, buffer.data(), (DWORD)buffer.size(), &read, NULL))
            {
                DWORD error = GetLastError();
                CloseADSHandle(output);
                CloseADSHandle(input);
                CFileOperationResult readResult = HandleADSReadError(observer, sourcePath, streamName, state);
                if (!readResult.success || readResult.skipped)
                    DeleteFileW(targetStream.c_str());
                return readResult.success ? readResult : ErrorResult(error);
            }
            if (read == 0)
                break;

            observer.WaitIfSuspended();
            if (observer.IsCancelled())
            {
                CloseADSHandle(output);
                CloseADSHandle(input);
                DeleteFileW(targetStream.c_str());
                return ErrorResult(ERROR_CANCELLED);
            }

            DWORD written = 0;
            if (!WriteFile(output, buffer.data(), read, &written, NULL) || written != read)
            {
                DWORD error = GetLastError();
                if (error == ERROR_SUCCESS)
                    error = ERROR_WRITE_FAULT;
                CloseADSHandle(output);
                CloseADSHandle(input);
                DeleteFileW(targetStream.c_str());

                CFileOperationResult writeResult = HandleADSWriteError(observer, targetPath, error, state);
                if (!writeResult.success && writeResult.lastError == ERROR_RETRY)
                {
                    retryStream = true;
                    break;
                }
                return writeResult;
            }
        }

        if (retryStream)
            continue;

        CloseADSHandle(output);
        CloseADSHandle(input);
        return SuccessResult();
    }
}

static CFileOperationResult ExecuteCopyADSW(IWorkerObserver& observer,
                                            const std::wstring& sourcePath,
                                            const std::wstring& targetPath,
                                            CFileOperationExecutionState& state)
{
    DWORD error = ERROR_SUCCESS;
    std::vector<std::wstring> streams;
    if (!EnumerateADSStreamsW(sourcePath, streams, error))
    {
        CFileOperationResult readResult = HandleADSReadError(observer, sourcePath, L"", state);
        if (!readResult.success || readResult.skipped)
            return readResult;
        return SuccessResult();
    }

    for (const std::wstring& streamName : streams)
    {
        CFileOperationResult result = CopyOneADSStreamW(observer, sourcePath, targetPath, streamName, state);
        if (!result.success || result.skipped)
            return result;
    }
    return SuccessResult();
}

static bool HasUnsafeTrailingFileNameCharW(const std::wstring& path)
{
    return !path.empty() && (path.back() <= L' ' || path.back() == L'.');
}

static std::wstring ParentDirectoryW(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring();
    return path.substr(0, slash + 1);
}

static void TranslateConvertChunk(const char* source,
                                  DWORD sourceSize,
                                  const CConvertData& convertData,
                                  bool& crlfBreak,
                                  std::vector<char>& target)
{
    target.clear();
    target.reserve(sourceSize * 2);

    for (DWORD i = 0; i < sourceSize; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        const bool lastChar = i == sourceSize - 1;

        if (convertData.EOFType != 0)
        {
            if (crlfBreak && i == 0 && ch == '\n')
            {
                crlfBreak = false;
                continue;
            }

            if (ch == '\r' || ch == '\n')
            {
                switch (convertData.EOFType)
                {
                case 2:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\n')]);
                    break;
                case 3:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\r')]);
                    break;
                default:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\r')]);
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\n')]);
                    break;
                }

                if (lastChar && ch == '\r')
                    crlfBreak = true;
                if (!lastChar && ch == '\r' && source[i + 1] == '\n')
                    ++i;
                continue;
            }
        }

        target.push_back(convertData.CodeTable[ch]);
    }
}

static bool WriteAllW(HANDLE file, const std::vector<char>& bytes, DWORD& error)
{
    error = ERROR_SUCCESS;
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD chunkSize = (DWORD)std::min<size_t>(bytes.size() - offset, MAXDWORD);
        DWORD written = 0;
        if (!WriteFile(file, bytes.data() + offset, chunkSize, &written, NULL) ||
            written == 0)
        {
            error = GetLastError();
            if (error == ERROR_SUCCESS)
                error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    return true;
}

static HANDLE CreateConvertTempFileW(const std::wstring& directory,
                                     std::wstring& tempPath,
                                     DWORD& error)
{
    error = ERROR_SUCCESS;
    tempPath.clear();
    if (directory.empty())
    {
        error = ERROR_INVALID_NAME;
        return INVALID_HANDLE_VALUE;
    }

    std::wstring base = directory;
    if (base.back() != L'\\' && base.back() != L'/')
        base.push_back(L'\\');

    const DWORD pid = GetCurrentProcessId();
    const DWORD tick = GetTickCount();
    for (DWORD attempt = 0; attempt < 256; ++attempt)
    {
        tempPath = base + L"cnv-" + std::to_wstring(pid) + L"-" +
                   std::to_wstring(tick) + L"-" + std::to_wstring(attempt) + L".tmp";
        HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, NULL,
                                  CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
                                  NULL);
        if (file != INVALID_HANDLE_VALUE)
            return file;

        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return INVALID_HANDLE_VALUE;
    }

    error = ERROR_ALREADY_EXISTS;
    return INVALID_HANDLE_VALUE;
}

static CFileOperationResult AskConvertFileError(IWorkerObserver& observer,
                                                const char* title,
                                                const std::wstring& path,
                                                DWORD error,
                                                CFileOperationExecutionState& state,
                                                bool& retry)
{
    retry = false;
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllErrors)
        return SuccessResult(true);

    int response = AskFileError(observer, title, path, error);
    switch (response)
    {
    case IDRETRY:
        retry = true;
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
    case IDB_IGNORE:
    case IDB_ALL:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult AskConvertMoveError(IWorkerObserver& observer,
                                                const std::wstring& tempPath,
                                                const std::wstring& sourcePath,
                                                DWORD error,
                                                CFileOperationExecutionState& state,
                                                bool& retry)
{
    retry = false;
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllErrors)
        return SuccessResult(true);

    std::string tempA = ObserverPathFallbackA(tempPath);
    std::string sourceA = ObserverPathFallbackA(sourcePath);
    int response = observer.AskCannotMoveErrW(tempA.c_str(), tempPath.c_str(),
                                             sourceA.c_str(), sourcePath.c_str(),
                                             error, false);
    switch (response)
    {
    case IDRETRY:
        retry = true;
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult ExecuteConvertFileW(IWorkerObserver& observer,
                                                const std::wstring& sourcePath,
                                                const CConvertData* convertData,
                                                CFileOperationExecutionState& state)
{
    if (convertData == NULL)
        return ErrorResult(ERROR_INVALID_PARAMETER);

    if (HasUnsafeTrailingFileNameCharW(sourcePath))
    {
        bool retry = false;
        return AskConvertFileError(observer, "Error opening file", sourcePath,
                                   ERROR_INVALID_NAME, state, retry);
    }

    while (true)
    {
        HANDLE source = CreateFileW(sourcePath.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (source == INVALID_HANDLE_VALUE)
        {
            bool retry = false;
            CFileOperationResult openResult =
                AskConvertFileError(observer, "Error opening file", sourcePath, GetLastError(), state, retry);
            if (retry)
                continue;
            return openResult;
        }

        std::wstring tempPath;
        DWORD tempError = ERROR_SUCCESS;
        HANDLE target = CreateConvertTempFileW(ParentDirectoryW(sourcePath), tempPath, tempError);
        if (target == INVALID_HANDLE_VALUE)
        {
            CloseHandle(source);
            bool retry = false;
            CFileOperationResult tempResult =
                AskConvertFileError(observer, "Error creating temp file",
                                    tempPath.empty() ? sourcePath : tempPath,
                                    tempError, state, retry);
            if (retry)
                continue;
            return tempResult;
        }

        bool restart = false;
        bool crlfBreak = false;
        std::vector<char> sourceBuffer(OPERATION_BUFFER);
        std::vector<char> targetBuffer;
        while (true)
        {
            DWORD read = 0;
            if (!ReadFile(source, sourceBuffer.data(), (DWORD)sourceBuffer.size(), &read, NULL))
            {
                DWORD error = GetLastError();
                CloseHandle(target);
                CloseHandle(source);
                DeleteFileW(tempPath.c_str());

                bool retry = false;
                CFileOperationResult readResult =
                    AskConvertFileError(observer, "Error reading file", sourcePath, error, state, retry);
                if (retry)
                {
                    restart = true;
                    break;
                }
                return readResult;
            }
            if (read == 0)
                break;

            observer.WaitIfSuspended();
            if (observer.IsCancelled())
            {
                CloseHandle(target);
                CloseHandle(source);
                DeleteFileW(tempPath.c_str());
                return ErrorResult(ERROR_CANCELLED);
            }

            TranslateConvertChunk(sourceBuffer.data(), read, *convertData, crlfBreak, targetBuffer);
            DWORD writeError = ERROR_SUCCESS;
            if (!WriteAllW(target, targetBuffer, writeError))
            {
                CloseHandle(target);
                CloseHandle(source);
                DeleteFileW(tempPath.c_str());

                bool retry = false;
                CFileOperationResult writeResult =
                    AskConvertFileError(observer, "Error writing file", tempPath, writeError, state, retry);
                if (retry)
                {
                    restart = true;
                    break;
                }
                return writeResult;
            }
        }

        if (restart)
            continue;

        CloseHandle(source);
        if (!CloseHandle(target))
        {
            DWORD error = GetLastError();
            DeleteFileW(tempPath.c_str());

            bool retry = false;
            CFileOperationResult closeResult =
                AskConvertFileError(observer, "Error writing file", tempPath, error, state, retry);
            if (retry)
                continue;
            return closeResult;
        }

        const DWORD sourceAttrs = GetFileAttributesW(sourcePath.c_str());
        if (sourceAttrs != INVALID_FILE_ATTRIBUTES &&
            (sourceAttrs & FILE_ATTRIBUTE_READONLY) != 0)
        {
            SetFileAttributesW(sourcePath.c_str(), sourceAttrs & ~FILE_ATTRIBUTE_READONLY);
        }

        while (true)
        {
            if (MoveFileExW(tempPath.c_str(), sourcePath.c_str(), MOVEFILE_REPLACE_EXISTING))
            {
                if (sourceAttrs != INVALID_FILE_ATTRIBUTES)
                    SetFileAttributesW(sourcePath.c_str(), sourceAttrs);
                return SuccessResult();
            }

            DWORD error = GetLastError();
            bool retry = false;
            CFileOperationResult moveResult =
                AskConvertMoveError(observer, tempPath, sourcePath, error, state, retry);
            if (retry)
                continue;

            DeleteFileW(tempPath.c_str());
            if (sourceAttrs != INVALID_FILE_ATTRIBUTES)
                SetFileAttributesW(sourcePath.c_str(), sourceAttrs);
            return moveResult;
        }
    }
}

class CSecurityDescriptorHolder
{
public:
    ~CSecurityDescriptorHolder()
    {
        Reset();
    }

    CSecurityDescriptorHolder(const CSecurityDescriptorHolder&) = delete;
    CSecurityDescriptorHolder& operator=(const CSecurityDescriptorHolder&) = delete;

    CSecurityDescriptorHolder() = default;

    bool Read(const std::wstring& path)
    {
        Reset();
        Path = AttributeWritePathW(path);
        Error = GetNamedSecurityInfoW((LPWSTR)Path.c_str(), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                      &Owner, &Group, &Dacl, NULL, &Descriptor);
        return Error == ERROR_SUCCESS;
    }

    DWORD ApplyTo(const std::wstring& targetPath) const
    {
        if (Error != ERROR_SUCCESS)
            return Error;
        if (Descriptor == NULL)
            return ERROR_INVALID_SECURITY_DESCR;

        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!GetSecurityDescriptorControl(Descriptor, &control, &revision))
            return GetLastError();

        const bool inheritedDacl = (control & SE_DACL_PROTECTED) == 0;
        const std::wstring target = AttributeWritePathW(targetPath);
        const DWORD targetAttrs = GetFileAttributesW(target.c_str());
        const SECURITY_INFORMATION securityInfo =
            DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
            (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION);

        DWORD error = SetNamedSecurityInfoW((LPWSTR)target.c_str(), SE_FILE_OBJECT,
                                            securityInfo, Owner, Group, Dacl, NULL);
        if (error != ERROR_SUCCESS)
        {
            const SECURITY_INFORMATION daclOnlyInfo =
                DACL_SECURITY_INFORMATION |
                (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION);
            DWORD daclError = SetNamedSecurityInfoW((LPWSTR)target.c_str(), SE_FILE_OBJECT,
                                                    daclOnlyInfo, NULL, NULL, Dacl, NULL);
            if (daclError == ERROR_SUCCESS)
                error = ERROR_SUCCESS;
        }

        if (targetAttrs != INVALID_FILE_ATTRIBUTES)
            SetFileAttributesW(target.c_str(), targetAttrs);
        return error;
    }

    DWORD LastError() const { return Error; }

private:
    std::wstring Path;
    PSID Owner = NULL;
    PSID Group = NULL;
    PACL Dacl = NULL;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    DWORD Error = ERROR_SUCCESS;

    void Reset()
    {
        if (Descriptor != NULL)
            LocalFree(Descriptor);
        Owner = NULL;
        Group = NULL;
        Dacl = NULL;
        Descriptor = NULL;
        Error = ERROR_SUCCESS;
        Path.clear();
    }
};

static CFileOperationResult HandleCopySecurityError(IWorkerObserver& observer,
                                                    const std::wstring& sourcePath,
                                                    const std::wstring& targetPath,
                                                    DWORD error,
                                                    CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllCopySecurityErrors)
        return SuccessResult();

    std::string sourceA = ObserverPathFallbackA(sourcePath);
    std::string targetA = ObserverPathFallbackA(targetPath);
    char errorText[64] = {};
    wsprintfA(errorText, "Error code %lu", error);
    int response = observer.AskCopyPermErrorW(sourceA.c_str(), sourcePath.c_str(),
                                             targetA.c_str(), targetPath.c_str(),
                                             errorText);
    switch (response)
    {
    case IDB_IGNOREALL:
        state.IgnoreAllCopySecurityErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED);
    }
}

static CFileOperationResult ExecuteCopySecurityW(IWorkerObserver& observer,
                                                 const std::wstring& sourcePath,
                                                 const std::wstring& targetPath,
                                                 CFileOperationExecutionState& state,
                                                 const CSecurityDescriptorHolder* preReadSecurity = NULL)
{
    if (state.IgnoreAllCopySecurityErrors)
        return SuccessResult();

    CSecurityDescriptorHolder security;
    const CSecurityDescriptorHolder* effectiveSecurity = preReadSecurity;
    if (effectiveSecurity == NULL)
    {
        security.Read(sourcePath);
        effectiveSecurity = &security;
    }

    if (effectiveSecurity->LastError() != ERROR_SUCCESS)
    {
        return HandleCopySecurityError(observer, sourcePath, targetPath,
                                       effectiveSecurity->LastError(), state);
    }

    DWORD error = effectiveSecurity->ApplyTo(targetPath);
    if (error != ERROR_SUCCESS)
        return HandleCopySecurityError(observer, sourcePath, targetPath, error, state);
    return SuccessResult();
}

static bool IsCompressionUnsupportedError(DWORD error)
{
    return error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED;
}

static bool IsEncryptionUnsupportedError(DWORD error)
{
    return error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED ||
           error == ERROR_CALL_NOT_IMPLEMENTED;
}

static DWORD SetCompressionFormatW(const std::wstring& path, USHORT compressionFormat)
{
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_DATA | FILE_WRITE_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    DWORD error = ERROR_SUCCESS;
    DWORD returned = 0;
    if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &compressionFormat,
                         sizeof(compressionFormat), NULL, 0, &returned, NULL))
    {
        error = GetLastError();
    }
    CloseHandle(file);
    return error;
}

static DWORD EncryptPathW(const wchar_t* path)
{
    return EncryptFileW(path) ? ERROR_SUCCESS : GetLastError();
}

static DWORD DecryptPathW(const wchar_t* path)
{
    return DecryptFileW(path, 0) ? ERROR_SUCCESS : GetLastError();
}

static DWORD InvokeWithPreservedFileTimeW(const std::wstring& path,
                                          DWORD attrs,
                                          DWORD (*operationFn)(const wchar_t* path))
{
    DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, flags, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    FILETIME created = {};
    FILETIME modified = {};
    GetFileTime(file, &created, NULL, &modified);
    CloseHandle(file);

    DWORD result = operationFn(path.c_str());

    file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, flags, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        SetFileTime(file, &created, NULL, &modified);
        CloseHandle(file);
    }
    return result;
}

static CFileOperationResult ExecuteCompressionChangeW(IWorkerObserver& observer,
                                                      const std::wstring& path,
                                                      DWORD currentAttrs,
                                                      DWORD targetAttrs,
                                                      CFileOperationExecutionState& state)
{
    if (state.SkipCompressionChanges)
        return SuccessResult();

    const std::wstring attrPath = AttributeWritePathW(path);
    const bool targetCompressed = (targetAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0;

    while (true)
    {
        DWORD effectiveCurrentAttrs = GetFileAttributesW(attrPath.c_str());
        if (effectiveCurrentAttrs == INVALID_FILE_ATTRIBUTES)
            effectiveCurrentAttrs = currentAttrs;

        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (((effectiveCurrentAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0) == targetCompressed))
        {
            return SuccessResult();
        }

        const bool clearReadOnly = effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
                                   (effectiveCurrentAttrs & FILE_ATTRIBUTE_READONLY) != 0;
        if (clearReadOnly)
            SetFileAttributesW(attrPath.c_str(), effectiveCurrentAttrs & ~FILE_ATTRIBUTE_READONLY);

        DWORD error = SetCompressionFormatW(attrPath, targetCompressed ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE);

        if (clearReadOnly)
            SetFileAttributesW(attrPath.c_str(), effectiveCurrentAttrs);

        if (error == ERROR_SUCCESS)
            return SuccessResult();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);

        if (IsCompressionUnsupportedError(error))
        {
            state.SkipCompressionChanges = true;
            std::string pathA = ObserverPathFallbackA(path);
            observer.NotifyError("Error changing compression", pathA.c_str(), "Compression is not supported");
            return SuccessResult();
        }

        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, "Error changing compression", path, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

static CFileOperationResult ExecuteEncryptionChangeW(IWorkerObserver& observer,
                                                     const std::wstring& path,
                                                     DWORD currentAttrs,
                                                     DWORD targetAttrs,
                                                     CFileOperationExecutionState& state)
{
    if (state.SkipEncryptionChanges)
        return SuccessResult();

    const std::wstring attrPath = AttributeWritePathW(path);
    const bool targetEncrypted = (targetAttrs & FILE_ATTRIBUTE_ENCRYPTED) != 0;

    while (true)
    {
        DWORD effectiveCurrentAttrs = GetFileAttributesW(attrPath.c_str());
        if (effectiveCurrentAttrs == INVALID_FILE_ATTRIBUTES)
            effectiveCurrentAttrs = currentAttrs;

        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (((effectiveCurrentAttrs & FILE_ATTRIBUTE_ENCRYPTED) != 0) == targetEncrypted))
        {
            return SuccessResult();
        }

        if (targetEncrypted &&
            effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (effectiveCurrentAttrs & FILE_ATTRIBUTE_SYSTEM) != 0 &&
            (targetAttrs & FILE_ATTRIBUTE_SYSTEM) != 0 &&
            !state.EncryptSystemAll)
        {
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllEncryptSystem)
                return SuccessResult();

            std::string pathA = ObserverPathFallbackA(path);
            int response = observer.AskHiddenOrSystem("Confirm system file encryption",
                                                      pathA.c_str(),
                                                      "Encrypt system file");
            switch (response)
            {
            case IDB_ALL:
                state.EncryptSystemAll = true;
                [[fallthrough]];
            case IDYES:
                break;
            case IDB_SKIPALL:
                state.SkipAllEncryptSystem = true;
                [[fallthrough]];
            case IDB_SKIP:
                return SuccessResult();
            case IDCANCEL:
            default:
                return ErrorResult(ERROR_CANCELLED);
            }
        }

        DWORD attrsToRestore = INVALID_FILE_ATTRIBUTES;
        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES)
        {
            DWORD attrsForOperation = effectiveCurrentAttrs;
            if (targetEncrypted)
                attrsForOperation &= ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);
            else
                attrsForOperation &= ~FILE_ATTRIBUTE_READONLY;

            if (attrsForOperation != effectiveCurrentAttrs)
            {
                attrsToRestore = effectiveCurrentAttrs;
                SetFileAttributesW(attrPath.c_str(), attrsForOperation);
            }
        }

        DWORD error = InvokeWithPreservedFileTimeW(attrPath, effectiveCurrentAttrs,
                                                   targetEncrypted ? EncryptPathW : DecryptPathW);

        if (attrsToRestore != INVALID_FILE_ATTRIBUTES)
            SetFileAttributesW(attrPath.c_str(), attrsToRestore);

        if (error == ERROR_SUCCESS)
            return SuccessResult();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);

        if (IsEncryptionUnsupportedError(error))
        {
            state.SkipEncryptionChanges = true;
            std::string pathA = ObserverPathFallbackA(path);
            observer.NotifyError("Error changing encryption", pathA.c_str(), "Encryption is not supported");
            return SuccessResult();
        }

        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, "Error changing encryption", path, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

static CFileOperationResult ExecuteChangeAttributesW(IWorkerObserver& observer,
                                                     const std::wstring& path,
                                                     DWORD attrs,
                                                     DWORD currentAttrs,
                                                     const CChangeAttrsData* attrsData,
                                                     CFileOperationExecutionState& state)
{
    const std::wstring attrPath = AttributeWritePathW(path);
    while (true)
    {
        if (attrsData != NULL && attrsData->ChangeCompression &&
            (attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            CFileOperationResult compression = ExecuteCompressionChangeW(observer, path, currentAttrs, attrs, state);
            if (!compression.success || compression.skipped)
                return compression;
        }
        if (attrsData != NULL && attrsData->ChangeEncryption &&
            (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        {
            CFileOperationResult encryption = ExecuteEncryptionChangeW(observer, path, currentAttrs, attrs, state);
            if (!encryption.success || encryption.skipped)
                return encryption;
        }
        if (attrsData != NULL && attrsData->ChangeCompression &&
            (attrs & FILE_ATTRIBUTE_COMPRESSED) != 0)
        {
            CFileOperationResult compression = ExecuteCompressionChangeW(observer, path, currentAttrs, attrs, state);
            if (!compression.success || compression.skipped)
                return compression;
        }
        if (attrsData != NULL && attrsData->ChangeEncryption &&
            (attrs & FILE_ATTRIBUTE_ENCRYPTED) != 0)
        {
            CFileOperationResult encryption = ExecuteEncryptionChangeW(observer, path, currentAttrs, attrs, state);
            if (!encryption.success || encryption.skipped)
                return encryption;
        }

        if (SetFileAttributesW(attrPath.c_str(), attrs))
        {
            if (attrsData == NULL ||
                (!attrsData->ChangeTimeModified &&
                 !attrsData->ChangeTimeCreated &&
                 !attrsData->ChangeTimeAccessed))
            {
                return SuccessResult();
            }

            const bool isReadOnly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
            if (isReadOnly)
                SetFileAttributesW(attrPath.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);

            DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0;
            HANDLE file = CreateFileW(attrPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, flags, NULL);
            if (file != INVALID_HANDLE_VALUE)
            {
                const FILETIME* created = attrsData->ChangeTimeCreated ? &attrsData->TimeCreated : NULL;
                const FILETIME* accessed = attrsData->ChangeTimeAccessed ? &attrsData->TimeAccessed : NULL;
                const FILETIME* modified = attrsData->ChangeTimeModified ? &attrsData->TimeModified : NULL;
                const BOOL ok = SetFileTime(file, created, accessed, modified);
                const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
                CloseHandle(file);
                if (isReadOnly)
                    SetFileAttributesW(attrPath.c_str(), attrs);
                if (ok)
                    return SuccessResult();

                if (state.SkipAllErrors)
                    return SuccessResult(true);
                int response = AskFileError(observer, "Error changing attributes", path, error);
                switch (response)
                {
                case IDRETRY:
                    break;
                case IDB_SKIPALL:
                    state.SkipAllErrors = true;
                    [[fallthrough]];
                case IDB_SKIP:
                    return SuccessResult(true);
                case IDCANCEL:
                default:
                    return ErrorResult(error);
                }
            }
            else
            {
                DWORD error = GetLastError();
                if (isReadOnly)
                    SetFileAttributesW(attrPath.c_str(), attrs);
                if (state.SkipAllErrors)
                    return SuccessResult(true);
                int response = AskFileError(observer, "Error changing attributes", path, error);
                switch (response)
                {
                case IDRETRY:
                    break;
                case IDB_SKIPALL:
                    state.SkipAllErrors = true;
                    [[fallthrough]];
                case IDB_SKIP:
                    return SuccessResult(true);
                case IDCANCEL:
                default:
                    return ErrorResult(error);
                }
            }
        }
        else
        {
            DWORD error = GetLastError();
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllErrors)
                return SuccessResult(true);

            int response = AskFileError(observer, "Error changing attributes", path, error);
            switch (response)
            {
            case IDRETRY:
                break;
            case IDB_SKIPALL:
                state.SkipAllErrors = true;
                [[fallthrough]];
            case IDB_SKIP:
                return SuccessResult(true);
            case IDCANCEL:
            default:
                return ErrorResult(error);
            }
        }
    }
}

static CFileOperationResult ExecuteScriptOperation(IWorkerObserver& observer,
                                                   const COperation& op,
                                                   CFileOperationExecutionState& state,
                                                   const CScriptExecutionOptions& options)
{
    if (op.Opcode == ocLabelForSkipOfCreateDir)
        return SuccessResult(true);

    if (op.Opcode == ocCountSize)
        return SuccessResult();

    if (op.Opcode == ocChangeAttrs)
    {
        const std::wstring sourcePath = OperationSourcePathW(op);
        CProgressData progressData = {};
        std::string sourceA = sourcePath.empty() ? std::string() : ObserverPathFallbackA(sourcePath);
        progressData.Source = sourceA.c_str();
        progressData.SourceW = sourcePath.empty() ? NULL : sourcePath.c_str();
        observer.SetOperationInfo(&progressData);

        return ExecuteChangeAttributesW(observer, sourcePath, (DWORD)(DWORD_PTR)op.TargetName, op.Attr,
                                        options.AttrsData, state);
    }

    if (op.Opcode == ocConvert)
    {
        if ((op.Attr & (FILE_ATTRIBUTE_DIRECTORY |
                        FILE_ATTRIBUTE_REPARSE_POINT |
                        FILE_ATTRIBUTE_COMPRESSED |
                        FILE_ATTRIBUTE_ENCRYPTED |
                        FILE_ATTRIBUTE_SPARSE_FILE)) != 0)
        {
            return ErrorResult(ERROR_NOT_SUPPORTED);
        }

        const std::wstring sourcePath = OperationSourcePathW(op);
        CProgressData progressData = {};
        std::string sourceA = sourcePath.empty() ? std::string() : ObserverPathFallbackA(sourcePath);
        progressData.Source = sourceA.c_str();
        progressData.SourceW = sourcePath.empty() ? NULL : sourcePath.c_str();
        observer.SetOperationInfo(&progressData);

        return ExecuteConvertFileW(observer, sourcePath, options.ConvertData, state);
    }

    if (op.Opcode == ocCopyDirTime)
    {
        const std::wstring targetPath = OperationTargetPathW(op);
        CProgressData progressData = {};
        std::string targetA = targetPath.empty() ? std::string() : ObserverPathFallbackA(targetPath);
        progressData.Target = targetA.c_str();
        progressData.TargetW = targetPath.empty() ? NULL : targetPath.c_str();
        observer.SetOperationInfo(&progressData);

        FILETIME lastWrite = {};
        lastWrite.dwLowDateTime = (DWORD)(DWORD_PTR)op.SourceName;
        lastWrite.dwHighDateTime = op.Attr;
        return ExecuteCopyDirectoryTimeW(observer, targetPath, lastWrite, state);
    }

    const std::wstring sourcePath = OperationSourcePathW(op);
    const std::wstring targetPath = OperationTargetPathW(op);

    CProgressData progressData = {};
    std::string sourceA = sourcePath.empty() ? std::string() : ObserverPathFallbackA(sourcePath);
    std::string targetA = targetPath.empty() ? std::string() : ObserverPathFallbackA(targetPath);
    progressData.Source = sourceA.c_str();
    progressData.Target = targetA.c_str();
    progressData.SourceW = sourcePath.empty() ? NULL : sourcePath.c_str();
    progressData.TargetW = targetPath.empty() ? NULL : targetPath.c_str();
    observer.SetOperationInfo(&progressData);

    CSecurityDescriptorHolder moveSecurity;
    bool hasMoveSecurity = false;
    bool isMoveWithSecurity = options.CopySecurity &&
                              (op.Opcode == ocMoveFile || op.Opcode == ocMoveDir);
    if (isMoveWithSecurity && !state.IgnoreAllCopySecurityErrors)
    {
        if (moveSecurity.Read(sourcePath))
        {
            hasMoveSecurity = true;
        }
        else
        {
            CFileOperationResult securityResult =
                HandleCopySecurityError(observer, sourcePath, targetPath, moveSecurity.LastError(), state);
            if (!securityResult.success)
                return securityResult;
        }
    }

    CFileOperationResult result;
    switch (op.Opcode)
    {
    case ocCopyFile:
        result = ExecuteCopyFileW(observer, sourcePath, targetPath, state);
        break;
    case ocMoveFile:
        result = ExecuteMoveFileW(observer, sourcePath, targetPath, state);
        break;
    case ocDeleteFile:
        return ExecuteDeleteFileW(observer, sourcePath, op.Attr, state);
    case ocCreateDir:
        result = ExecuteCreateDirectoryW(observer, targetPath, state);
        break;
    case ocMoveDir:
        result = ExecuteMoveDirectoryW(observer, sourcePath, targetPath, state);
        break;
    case ocDeleteDir:
    case ocDeleteDirLink:
        return ExecuteRemoveDirectoryW(observer, sourcePath, state);
    default:
        return ErrorResult(ERROR_NOT_SUPPORTED);
    }

    if (!result.success || result.skipped)
        return result;

    if ((op.OpFlags & OPFL_COPY_ADS) != 0)
    {
        switch (op.Opcode)
        {
        case ocCopyFile:
        case ocCreateDir:
        {
            CFileOperationResult adsResult = ExecuteCopyADSW(observer, sourcePath, targetPath, state);
            if (!adsResult.success || adsResult.skipped)
                return adsResult;
            break;
        }
        default:
            break;
        }
    }

    if (!options.CopySecurity)
        return result;

    switch (op.Opcode)
    {
    case ocCopyFile:
    case ocCreateDir:
        return ExecuteCopySecurityW(observer, sourcePath, targetPath, state);
    case ocMoveFile:
    case ocMoveDir:
        if (hasMoveSecurity)
            return ExecuteCopySecurityW(observer, sourcePath, targetPath, state, &moveSecurity);
        return result;
    default:
        return result;
    }
}

CScriptExecutionResult ExecuteOperationsHeadless(IWorkerObserver& observer,
                                                 COperations& script,
                                                 CFileOperationExecutionState& state,
                                                 bool signalDone)
{
    CScriptExecutionOptions options;
    return ExecuteOperationsHeadlessWithOptions(observer, script, state, options, signalDone);
}

CScriptExecutionResult ExecuteOperationsHeadlessWithOptions(IWorkerObserver& observer,
                                                            COperations& script,
                                                            CFileOperationExecutionState& state,
                                                            const CScriptExecutionOptions& options,
                                                            bool signalDone)
{
    CScriptExecutionResult result;
    CScriptExecutionOptions effectiveOptions = options;
    if (script.CopySecurity)
        effectiveOptions.CopySecurity = true;

    int totalProgressOps = 0;
    for (int i = 0; i < script.Count; ++i)
    {
        if (ScriptOperationCountsForProgress(script.At(i).Opcode))
            ++totalProgressOps;
    }

    observer.SetProgress(0, 0);
    int processedProgressOps = 0;
    for (int i = 0; i < script.Count; ++i)
    {
        if (observer.IsCancelled())
        {
            result.lastError = ERROR_CANCELLED;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        const COperation& op = script.At(i);
        CFileOperationResult opResult = ExecuteScriptOperation(observer, op, state, effectiveOptions);
        if (!opResult.success)
        {
            result.lastError = opResult.lastError != ERROR_SUCCESS ? opResult.lastError : ERROR_INVALID_PARAMETER;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        if (ScriptOperationCountsForProgress(op.Opcode))
        {
            ++processedProgressOps;
            ++result.completedOperations;
            const int progress = totalProgressOps == 0
                                     ? 1000
                                     : (processedProgressOps * 1000) / totalProgressOps;
            observer.SetProgress(0, progress);
        }
    }

    result.success = true;
    observer.SetProgress(0, 1000);
    observer.SetError(false);
    if (signalDone)
        observer.NotifyDone();
    return result;
}

} // namespace sally::operation_executor
