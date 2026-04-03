// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CHeadlessWorkerObserver — production headless IWorkerObserver implementation.
//
// Auto-answers all Ask* dialogs with configurable response codes.
// Thread-safe: cancel/error/progress use atomics, event log uses a mutex.
// No HWND, no SendMessage, no message pump, no UI dependencies.
//
// Usage:
//   CHeadlessWorkerObserver obs;
//   obs.OverwriteResponse = IDYES;        // overwrite all
//   obs.FileErrorResponse = IDB_SKIPALL;  // skip errors
//   RunWorkerDirect(script, obs, NULL, NULL, /*headless=*/true);
//   obs.WaitForCompletion(30000);

#pragma once

#include "IWorkerObserver.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

// Dialog response constants — use the real resource IDs from lang.rh
#include "lang/lang.rh"

class CHeadlessWorkerObserver : public IWorkerObserver
{
public:
    // --- Configuration (set before starting the worker) ---

    int FileErrorResponse = IDB_SKIP;      // AskFileError* default (skip/skipall/retry/cancel)
    int IgnoreErrorResponse = IDB_IGNORE;  // AskSetAttrsError, AskCopyPermError, AskCopyDirTimeError,
                                           // AskADSReadError, AskADSOpenError* (ignore/ignoreall/retry/cancel)
    int OverwriteResponse = IDB_SKIP;      // AskOverwrite / AskADSOverwrite default
    int HiddenSystemResponse = IDYES;      // AskHiddenOrSystem* default
    int CannotMoveResponse = IDB_SKIP;     // AskCannotMove* default
    int EncryptionLossResponse = IDYES;    // AskEncryptionLoss default

    CHeadlessWorkerObserver()
        : m_completionEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
    {
    }

    ~CHeadlessWorkerObserver()
    {
        if (m_completionEvent)
            CloseHandle(m_completionEvent);
    }

    // Non-copyable
    CHeadlessWorkerObserver(const CHeadlessWorkerObserver&) = delete;
    CHeadlessWorkerObserver& operator=(const CHeadlessWorkerObserver&) = delete;

    // --- Control ---

    void Cancel() { m_cancelled.store(true, std::memory_order_release); }

    // --- Results ---

    bool WaitForCompletion(DWORD timeoutMs = 30000)
    {
        return WaitForSingleObject(m_completionEvent, timeoutMs) == WAIT_OBJECT_0;
    }

    HANDLE GetCompletionEvent() const { return m_completionEvent; }
    bool IsDone() const { return m_done.load(std::memory_order_acquire); }
    bool HasError() const { return m_error.load(std::memory_order_acquire); }
    int GetLastOperationPercent() const { return m_lastOpPercent.load(std::memory_order_relaxed); }
    int GetLastSummaryPercent() const { return m_lastSumPercent.load(std::memory_order_relaxed); }

    // Event log for diagnostics/assertions
    struct LogEntry
    {
        std::string method;
        std::string detail;
    };

    std::vector<LogEntry> GetLog() const
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        return m_log;
    }

    int CountLogEntriesOfType(const char* method) const
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        int count = 0;
        for (const auto& e : m_log)
            if (e.method == method)
                count++;
        return count;
    }

    // --- IWorkerObserver implementation ---

    void SetOperationInfo(CProgressData* /*data*/) override
    {
        Log("SetOperationInfo", "");
    }

    void SetProgress(int operationPercent, int summaryPercent) override
    {
        m_lastOpPercent.store(operationPercent, std::memory_order_relaxed);
        m_lastSumPercent.store(summaryPercent, std::memory_order_relaxed);
    }

    void SetProgressWithoutSuspend(int operationPercent, int summaryPercent) override
    {
        m_lastOpPercent.store(operationPercent, std::memory_order_relaxed);
        m_lastSumPercent.store(summaryPercent, std::memory_order_relaxed);
    }

    void WaitIfSuspended() override { /* no-op in headless mode */ }

    bool IsCancelled() const override
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

    void SetError(bool error) override
    {
        m_error.store(error, std::memory_order_release);
    }

    void NotifyDone() override
    {
        m_done.store(true, std::memory_order_release);
        SetEvent(m_completionEvent);
    }

    HWND GetParentWindow() const override { return NULL; }

    // --- Error dialogs ---

    int AskFileError(const char* /*title*/, const char* fileName, const char* /*errorText*/) override
    {
        Log("AskFileError", fileName ? fileName : "");
        return FileErrorResponse;
    }

    int AskFileErrorById(int /*titleId*/, const char* fileName, DWORD /*win32Error*/) override
    {
        Log("AskFileErrorById", fileName ? fileName : "");
        return FileErrorResponse;
    }

    int AskFileErrorByIds(int /*titleId*/, const char* fileName, int /*errorTextId*/) override
    {
        Log("AskFileErrorByIds", fileName ? fileName : "");
        return FileErrorResponse;
    }

    // --- Overwrite ---

    int AskOverwrite(const char* sourceName, const char* /*sourceInfo*/,
                     const char* /*targetName*/, const char* /*targetInfo*/) override
    {
        Log("AskOverwrite", sourceName ? sourceName : "");
        return OverwriteResponse;
    }

    // --- Hidden/system ---

    int AskHiddenOrSystem(const char* /*title*/, const char* fileName,
                          const char* /*actionText*/) override
    {
        Log("AskHiddenOrSystem", fileName ? fileName : "");
        return HiddenSystemResponse;
    }

    int AskHiddenOrSystemById(int /*titleId*/, const char* fileName, int /*actionId*/) override
    {
        Log("AskHiddenOrSystemById", fileName ? fileName : "");
        return HiddenSystemResponse;
    }

    // --- Cannot move ---

    int AskCannotMove(const char* /*errorText*/, const char* fileName,
                      const char* /*destPath*/, bool /*isDirectory*/) override
    {
        Log("AskCannotMove", fileName ? fileName : "");
        return CannotMoveResponse;
    }

    int AskCannotMoveErr(const char* sourceName, const char* /*targetName*/,
                         DWORD /*win32Error*/, bool /*isDirectory*/) override
    {
        Log("AskCannotMoveErr", sourceName ? sourceName : "");
        return CannotMoveResponse;
    }

    // --- Notifications ---

    void NotifyError(const char* /*title*/, const char* fileName,
                     const char* /*errorText*/) override
    {
        Log("NotifyError", fileName ? fileName : "");
    }

    void NotifyErrorById(int /*titleId*/, const char* fileName, int /*detailId*/) override
    {
        Log("NotifyErrorById", fileName ? fileName : "");
    }

    // --- ADS ---

    int AskADSReadError(const char* fileName, const char* /*adsName*/) override
    {
        Log("AskADSReadError", fileName ? fileName : "");
        return IgnoreErrorResponse;
    }

    int AskADSOverwrite(const char* sourceName, const char* /*sourceInfo*/,
                        const char* /*targetName*/, const char* /*targetInfo*/) override
    {
        Log("AskADSOverwrite", sourceName ? sourceName : "");
        return OverwriteResponse;
    }

    int AskADSOpenError(const char* fileName, const char* /*adsName*/,
                        const char* /*errorText*/) override
    {
        Log("AskADSOpenError", fileName ? fileName : "");
        return IgnoreErrorResponse;
    }

    int AskADSOpenErrorById(int /*titleId*/, const char* fileName, DWORD /*win32Error*/) override
    {
        Log("AskADSOpenErrorById", fileName ? fileName : "");
        return IgnoreErrorResponse;
    }

    // --- Attributes / permissions / time ---

    int AskSetAttrsError(const char* fileName, DWORD /*failedAttrs*/,
                         DWORD /*currentAttrs*/) override
    {
        Log("AskSetAttrsError", fileName ? fileName : "");
        return IgnoreErrorResponse;
    }

    int AskCopyPermError(const char* sourceFile, const char* /*targetFile*/,
                         const char* /*errorText*/) override
    {
        Log("AskCopyPermError", sourceFile ? sourceFile : "");
        return IgnoreErrorResponse;
    }

    int AskCopyDirTimeError(const char* dirName, DWORD /*errorCode*/) override
    {
        Log("AskCopyDirTimeError", dirName ? dirName : "");
        return IgnoreErrorResponse;
    }

    // --- Encryption ---

    int AskEncryptionLoss(bool /*isEncrypted*/, const char* fileName, bool /*isDir*/) override
    {
        Log("AskEncryptionLoss", fileName ? fileName : "");
        return EncryptionLossResponse;
    }

private:
    HANDLE m_completionEvent;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_error{false};
    std::atomic<bool> m_done{false};
    std::atomic<int> m_lastOpPercent{0};
    std::atomic<int> m_lastSumPercent{0};

    mutable std::mutex m_logMutex;
    std::vector<LogEntry> m_log;

    void Log(const char* method, const char* detail)
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_log.push_back({method, detail});
    }
};
