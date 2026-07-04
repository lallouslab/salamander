// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <windows.h>

// File attributes for IFileSystem operations
struct FileInfo
{
    std::wstring name;
    uint64_t size;
    FILETIME creationTime;
    FILETIME lastWriteTime;
    DWORD attributes;
    bool isDirectory;
};

// Volume capability/geometry info (P2-a). Portable subset the worker needs to
// decide ADS/compression/encryption/ACL support and cluster rounding, without
// callers touching GetVolumeInformation / DeviceIoControl directly.
struct VolumeCapabilities
{
    std::wstring fileSystemName;   // e.g. "NTFS", "FAT32", "exFAT"
    DWORD flags;                   // raw FILE_* volume flags (as reported by the OS)
    DWORD bytesPerCluster;         // 0 if unknown
    bool SupportsADS() const { return (flags & FILE_NAMED_STREAMS) != 0; }
    bool SupportsCompression() const { return (flags & FILE_FILE_COMPRESSION) != 0; }
    bool SupportsEncryption() const { return (flags & FILE_SUPPORTS_ENCRYPTION) != 0; }
    bool SupportsACLs() const { return (flags & FILE_PERSISTENT_ACLS) != 0; }
};

// Portable flags for MoveFileWithFlags (P2-a) — subset of MOVEFILE_*.
enum class MoveFlags : unsigned
{
    None = 0,
    ReplaceExisting = 1,
    CopyAllowed = 2,
    WriteThrough = 4,
};
inline MoveFlags operator|(MoveFlags a, MoveFlags b)
{
    return (MoveFlags)((unsigned)a | (unsigned)b);
}
inline bool HasFlag(MoveFlags v, MoveFlags f)
{
    return ((unsigned)v & (unsigned)f) != 0;
}

// Result of file operations
struct FileResult
{
    bool success;
    DWORD errorCode;  // Win32 error code on failure

    static FileResult Ok() { return {true, 0}; }
    static FileResult Error(DWORD err) { return {false, err}; }
};

// Abstract interface for file system operations
// Enables mocking for tests and potential future OS abstraction
class IFileSystem
{
public:
    virtual ~IFileSystem() {}

    // File existence and info
    virtual bool FileExists(const wchar_t* path) = 0;
    virtual bool DirectoryExists(const wchar_t* path) = 0;
    virtual FileResult GetFileInfo(const wchar_t* path, FileInfo& info) = 0;

    // File attributes
    virtual DWORD GetFileAttributes(const wchar_t* path) = 0;  // Returns INVALID_FILE_ATTRIBUTES on error
    virtual FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) = 0;

    // File operations
    virtual FileResult DeleteFile(const wchar_t* path) = 0;
    virtual FileResult MoveFile(const wchar_t* source, const wchar_t* target) = 0;
    virtual FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) = 0;

    // Directory operations
    virtual FileResult CreateDirectory(const wchar_t* path) = 0;
    virtual FileResult RemoveDirectory(const wchar_t* path) = 0;

    // Generic file open/create operation
    virtual HANDLE CreateFile(const wchar_t* path,
                              DWORD desiredAccess,
                              DWORD shareMode,
                              LPSECURITY_ATTRIBUTES securityAttributes,
                              DWORD creationDisposition,
                              DWORD flagsAndAttributes,
                              HANDLE templateFile) = 0;

    // File enumeration handle operations
    virtual HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) = 0;
    virtual BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) = 0;

    // File handle operations (for copy loops, etc.)
    virtual HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode = FILE_SHARE_READ) = 0;
    virtual HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) = 0;
    virtual void CloseHandle(HANDLE h) = 0;

    // --- P2-a handle I/O ops (kb/unicode Axis D) --------------------------
    // Named to avoid the Win32 A/W macro trick used above; new interface
    // surface prefers distinct names. Default impls fail with
    // ERROR_CALL_NOT_IMPLEMENTED so mocks need only override what they inject.

    // Read up to 'toRead' bytes; '*read' receives the count (0 at EOF).
    virtual FileResult ReadFromHandle(HANDLE h, void* buffer, DWORD toRead, DWORD* read)
    { (void)h; (void)buffer; (void)toRead; if (read) *read = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Write 'toWrite' bytes; '*written' receives the count.
    virtual FileResult WriteToHandle(HANDLE h, const void* buffer, DWORD toWrite, DWORD* written)
    { (void)h; (void)buffer; (void)toWrite; if (written) *written = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Move the file pointer. 'moveMethod' is FILE_BEGIN/CURRENT/END.
    // '*newPos' (optional) receives the resulting absolute position.
    virtual FileResult SeekHandle(HANDLE h, int64_t distance, DWORD moveMethod, uint64_t* newPos)
    { (void)h; (void)distance; (void)moveMethod; (void)newPos; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Set creation/access/write times (any pointer may be NULL to leave as-is).
    virtual FileResult SetHandleFileTime(HANDLE h, const FILETIME* creation,
                                         const FILETIME* lastAccess, const FILETIME* lastWrite)
    { (void)h; (void)creation; (void)lastAccess; (void)lastWrite; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // '*size' receives the file size in bytes.
    virtual FileResult GetHandleFileSize(HANDLE h, uint64_t* size)
    { (void)h; if (size) *size = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Flush buffered writes to disk.
    virtual FileResult FlushHandle(HANDLE h)
    { (void)h; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Truncate/extend the file to the current pointer position (SetEndOfFile).
    virtual FileResult SetHandleEnd(HANDLE h)
    { (void)h; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- P2-a semantic file attributes (replace raw DeviceIoControl) ------

    // NTFS transparent compression on an open handle.
    virtual FileResult SetHandleCompression(HANDLE h, bool compress)
    { (void)h; (void)compress; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Mark an open handle's file as sparse.
    virtual FileResult SetHandleSparse(HANDLE h, bool sparse)
    { (void)h; (void)sparse; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // EFS encrypt/decrypt by path.
    virtual FileResult EncryptPath(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult DecryptPath(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- P2-a security as opaque self-relative SD blob --------------------
    // Core never parses the descriptor — it just copies it source→target.
    virtual FileResult GetPathSecurity(const wchar_t* path, std::vector<BYTE>& sd)
    { (void)path; sd.clear(); return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult SetPathSecurity(const wchar_t* path, const BYTE* sd, size_t len)
    { (void)path; (void)sd; (void)len; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- P2-a move with flags + volume queries ----------------------------
    virtual FileResult MoveFileWithFlags(const wchar_t* source, const wchar_t* target, MoveFlags flags)
    { (void)source; (void)target; (void)flags; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // '*freeForCaller' / '*totalBytes' (either may be NULL) in bytes.
    virtual FileResult GetDiskFree(const wchar_t* path, uint64_t* freeForCaller, uint64_t* totalBytes)
    { (void)path; (void)freeForCaller; (void)totalBytes; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    virtual FileResult QueryVolumeCapabilities(const wchar_t* path, VolumeCapabilities& caps)
    { (void)path; (void)caps; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
};

// Global file system instance - default is Win32 implementation
extern IFileSystem* gFileSystem;

// Returns the default Win32 implementation
IFileSystem* GetWin32FileSystem();

// Helper to convert ANSI path to wide string
inline std::wstring AnsiPathToWide(const char* path)
{
    if (!path) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring widePath;
    widePath.resize(len);
    MultiByteToWideChar(CP_ACP, 0, path, -1, &widePath[0], len);
    widePath.resize(len - 1);  // Remove null terminator from string length
    return widePath;
}

// ANSI helpers - convert ANSI paths to wide and call wide versions
// Use when migrating ANSI code to IFileSystem

inline FileResult DeleteFileA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path) return FileResult::Error(GetLastError());
    return fs->DeleteFile(widePath.c_str());
}

inline FileResult MoveFileA(IFileSystem* fs, const char* source, const char* target)
{
    std::wstring wideSource = AnsiPathToWide(source);
    std::wstring wideTarget = AnsiPathToWide(target);
    if ((wideSource.empty() && source && *source) || (wideTarget.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->MoveFile(wideSource.c_str(), wideTarget.c_str());
}

// Wide-path-aware MoveFile: uses wideSource/wideTarget when non-empty, otherwise falls back to ANSI conversion
inline FileResult MoveFileAW(IFileSystem* fs, const char* source, const char* target,
                             const std::wstring& wideSource, const std::wstring& wideTarget)
{
    std::wstring srcFallback, tgtFallback;
    const std::wstring& src = !wideSource.empty() ? wideSource : (srcFallback = AnsiPathToWide(source));
    const std::wstring& tgt = !wideTarget.empty() ? wideTarget : (tgtFallback = AnsiPathToWide(target));
    if ((src.empty() && source && *source) || (tgt.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->MoveFile(src.c_str(), tgt.c_str());
}

inline FileResult CopyFileA(IFileSystem* fs, const char* source, const char* target, bool failIfExists)
{
    std::wstring wideSource = AnsiPathToWide(source);
    std::wstring wideTarget = AnsiPathToWide(target);
    if ((wideSource.empty() && source && *source) || (wideTarget.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->CopyFile(wideSource.c_str(), wideTarget.c_str(), failIfExists);
}

inline DWORD GetFileAttributesA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    return fs->GetFileAttributes(widePath.c_str());
}

inline FileResult SetFileAttributesA(IFileSystem* fs, const char* path, DWORD attributes)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->SetFileAttributes(widePath.c_str(), attributes);
}

inline FileResult GetFileInfoA(IFileSystem* fs, const char* path, FileInfo& info)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->GetFileInfo(widePath.c_str(), info);
}

inline FileResult CreateDirectoryA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->CreateDirectory(widePath.c_str());
}

inline FileResult RemoveDirectoryA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->RemoveDirectory(widePath.c_str());
}

inline HANDLE CreateFileA(IFileSystem* fs, const char* path,
                          DWORD desiredAccess,
                          DWORD shareMode,
                          LPSECURITY_ATTRIBUTES securityAttributes,
                          DWORD creationDisposition,
                          DWORD flagsAndAttributes,
                          HANDLE templateFile)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return fs->CreateFile(widePath.c_str(), desiredAccess, shareMode, securityAttributes,
                          creationDisposition, flagsAndAttributes, templateFile);
}

inline HANDLE FindFirstFilePathA(IFileSystem* fs, const char* path, WIN32_FIND_DATAW* findData)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    return fs->FindFirstFile(widePath.c_str(), findData);
}
