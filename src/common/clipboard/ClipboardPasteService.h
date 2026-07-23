// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

class IClipboard;

namespace sally
{
    namespace clipboard
    {
        class IClipboardFileDropSource;

        enum class ClipboardFileEffect
        {
            Copy,
            Move
        };

        struct ClipboardFileTransfer
        {
            ClipboardFileEffect Effect = ClipboardFileEffect::Copy;
            std::vector<std::wstring> SourcePaths;
            bool MakeCopyOfName = false;
        };

        class IClipboardPasteExecutor
        {
        public:
            virtual ~IClipboardPasteExecutor() = default;
            virtual bool Execute(const ClipboardFileTransfer& transfer) = 0;
        };

        enum class ClipboardPasteRoute
        {
            NotFiles,
            ShellFallback,
            Executed,
            OwnedExecutionFailed
        };

        struct ClipboardPasteResult
        {
            ClipboardPasteRoute Route = ClipboardPasteRoute::NotFiles;
            bool FilesOnClipboard = false;
            bool OwnedFileTransfer = false;
            ClipboardFileEffect Effect = ClipboardFileEffect::Copy;
            bool ClipboardCleared = false;
            uint32_t ErrorCode = 0;
        };

        ClipboardPasteResult ExecuteSallyDiskClipboardPaste(IClipboard& clipboard,
                                                            IClipboardFileDropSource& fileDropSource,
                                                            bool onlyLinks,
                                                            bool onlyTest,
                                                            IClipboardPasteExecutor& executor);
    } // namespace clipboard
} // namespace sally
