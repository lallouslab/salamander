// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "clipboard/IClipboardFileDropSource.h"

struct IDataObject;

namespace sally
{
    namespace clipboard
    {
        class Win32ClipboardFileDropSource : public IClipboardFileDropSource
        {
        public:
            // Borrows dataObject for the lifetime of this adapter.
            explicit Win32ClipboardFileDropSource(IDataObject* dataObject);

            bool HasFileDrop() override;
            ClipboardResult ReadFileDrop(ClipboardFileDrop& fileDrop) override;

        private:
            IDataObject* DataObject;
        };
    } // namespace clipboard
} // namespace sally
