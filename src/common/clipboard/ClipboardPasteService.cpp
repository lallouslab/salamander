// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_CLIPBOARD_STANDALONE
#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#else
#include "precomp.h"
#endif

#include "ClipboardPasteService.h"
#include "common/IClipboard.h"
#include "IClipboardFileDropSource.h"

#include <shlobj.h>
#include <utility>

namespace sally
{
    namespace clipboard
    {
        namespace
        {
            bool ReadDropEffect(IClipboard& clipboard, DWORD& effect)
            {
                effect = 0;
                const uint32_t format = clipboard.RegisterFormat(L"Preferred DropEffect");
                std::vector<uint8_t> data;
                if (format == 0)
                    return false;
                ClipboardResult result = clipboard.GetRawData(format, data);
                if (!result.success || data.size() < sizeof(DWORD))
                    return false;
                memcpy(&effect, data.data(), sizeof(effect));
                effect &= DROPEFFECT_COPY | DROPEFFECT_MOVE;
                return effect == DROPEFFECT_COPY || effect == DROPEFFECT_MOVE;
            }
        } // namespace

        ClipboardPasteResult ExecuteSallyDiskClipboardPaste(IClipboard& clipboard,
                                                            IClipboardFileDropSource& fileDropSource,
                                                            bool onlyLinks,
                                                            bool onlyTest,
                                                            IClipboardPasteExecutor& executor)
        {
            ClipboardPasteResult result;
            if (!fileDropSource.HasFileDrop())
                return result;

            result.FilesOnClipboard = true;
            result.Route = ClipboardPasteRoute::ShellFallback;
            if (onlyLinks || onlyTest)
                return result;

            const uint32_t ownerFormat = clipboard.RegisterFormat(L"SalIDataObject");
            if (ownerFormat == 0 || !clipboard.HasFormat(ownerFormat))
                return result;

            result.OwnedFileTransfer = true;
            result.Route = ClipboardPasteRoute::OwnedExecutionFailed;

            ClipboardFileDrop fileDrop;
            ClipboardResult fileDropResult = fileDropSource.ReadFileDrop(fileDrop);
            if (!fileDropResult.success)
            {
                result.ErrorCode = fileDropResult.errorCode;
                return result;
            }
            if (fileDrop.Encoding == ClipboardFileDropEncoding::Ansi)
            {
                result.Route = ClipboardPasteRoute::ShellFallback;
                return result;
            }

            DWORD dropEffect = 0;
            if (!ReadDropEffect(clipboard, dropEffect))
            {
                result.ErrorCode = ERROR_INVALID_DATA;
                return result;
            }

            ClipboardFileTransfer transfer;
            transfer.Effect = dropEffect == DROPEFFECT_COPY ? ClipboardFileEffect::Copy
                                                            : ClipboardFileEffect::Move;
            transfer.MakeCopyOfName = transfer.Effect == ClipboardFileEffect::Copy;
            result.Effect = transfer.Effect;
            transfer.SourcePaths = std::move(fileDrop.SourcePaths);
            if (transfer.SourcePaths.empty())
            {
                result.ErrorCode = ERROR_INVALID_DATA;
                return result;
            }

            if (!executor.Execute(transfer))
            {
                result.ErrorCode = ERROR_OPERATION_ABORTED;
                return result;
            }

            result.Route = ClipboardPasteRoute::Executed;
            if (transfer.Effect == ClipboardFileEffect::Move)
            {
                ClipboardResult clearResult = clipboard.Clear();
                result.ClipboardCleared = clearResult.success;
                result.ErrorCode = clearResult.errorCode;
            }
            return result;
        }
    } // namespace clipboard
} // namespace sally
