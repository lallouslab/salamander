// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/IClipboard.h"

#include <string>
#include <vector>

namespace sally
{
    namespace clipboard
    {
        enum class ClipboardFileDropEncoding
        {
            Ansi,
            Unicode
        };

        struct ClipboardFileDrop
        {
            ClipboardFileDropEncoding Encoding = ClipboardFileDropEncoding::Unicode;
            std::vector<std::wstring> SourcePaths;
        };

        class IClipboardFileDropSource
        {
        public:
            virtual ~IClipboardFileDropSource() = default;
            virtual bool HasFileDrop() = 0;
            virtual ClipboardResult ReadFileDrop(ClipboardFileDrop& fileDrop) = 0;
        };
    } // namespace clipboard
} // namespace sally
