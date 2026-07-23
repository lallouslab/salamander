// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_CLIPBOARD_STANDALONE
#include <windows.h>
#include <objidl.h>
#include <shellapi.h>
#else
#include "precomp.h"
#endif

#include "Win32ClipboardFileDropSource.h"

#include <shlobj.h>

namespace sally
{
    namespace clipboard
    {
        namespace
        {
            bool ValidateAnsiMultiString(const BYTE* bytes, SIZE_T size, SIZE_T offset)
            {
                const char* cursor = reinterpret_cast<const char*>(bytes + offset);
                const char* end = reinterpret_cast<const char*>(bytes + size);
                bool foundPath = false;
                while (cursor < end && *cursor != '\0')
                {
                    const char* terminator = cursor;
                    while (terminator < end && *terminator != '\0')
                        ++terminator;
                    if (terminator == end)
                        return false;
                    foundPath = true;
                    cursor = terminator + 1;
                }
                return foundPath && cursor < end && *cursor == '\0';
            }

            bool ReadUnicodeMultiString(const BYTE* bytes, SIZE_T size, SIZE_T offset,
                                        std::vector<std::wstring>& paths)
            {
                if ((offset % alignof(wchar_t)) != 0 ||
                    ((size - offset) % sizeof(wchar_t)) != 0)
                    return false;

                const wchar_t* cursor = reinterpret_cast<const wchar_t*>(bytes + offset);
                const wchar_t* end = reinterpret_cast<const wchar_t*>(bytes + size);
                while (cursor < end && *cursor != L'\0')
                {
                    const wchar_t* terminator = cursor;
                    while (terminator < end && *terminator != L'\0')
                        ++terminator;
                    if (terminator == end)
                        return false;
                    paths.emplace_back(cursor, terminator);
                    cursor = terminator + 1;
                }
                return !paths.empty() && cursor < end && *cursor == L'\0';
            }
        } // namespace

        Win32ClipboardFileDropSource::Win32ClipboardFileDropSource(IDataObject* dataObject)
            : DataObject(dataObject)
        {
        }

        bool Win32ClipboardFileDropSource::HasFileDrop()
        {
            if (DataObject == nullptr)
                return false;

            FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            return SUCCEEDED(DataObject->QueryGetData(&format));
        }

        ClipboardResult Win32ClipboardFileDropSource::ReadFileDrop(ClipboardFileDrop& fileDrop)
        {
            fileDrop = ClipboardFileDrop{};
            if (DataObject == nullptr)
                return ClipboardResult::Error(ERROR_INVALID_PARAMETER);

            FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium = {};
            const HRESULT getResult = DataObject->GetData(&format, &medium);
            if (FAILED(getResult))
                return ClipboardResult::Error(static_cast<uint32_t>(getResult));

            ClipboardResult result = ClipboardResult::Error(ERROR_INVALID_DATA);
            const SIZE_T size = medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr
                                    ? GlobalSize(medium.hGlobal)
                                    : 0;
            const BYTE* bytes = size >= sizeof(DROPFILES)
                                    ? static_cast<const BYTE*>(GlobalLock(medium.hGlobal))
                                    : nullptr;
            if (bytes != nullptr)
            {
                const DROPFILES* drop = reinterpret_cast<const DROPFILES*>(bytes);
                const SIZE_T offset = drop->pFiles;
                if (offset >= sizeof(DROPFILES) && offset < size)
                {
                    if (drop->fWide)
                    {
                        fileDrop.Encoding = ClipboardFileDropEncoding::Unicode;
                        if (ReadUnicodeMultiString(bytes, size, offset,
                                                   fileDrop.SourcePaths))
                            result = ClipboardResult::Ok();
                    }
                    else
                    {
                        fileDrop.Encoding = ClipboardFileDropEncoding::Ansi;
                        if (ValidateAnsiMultiString(bytes, size, offset))
                            result = ClipboardResult::Ok();
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);

            if (!result.success)
                fileDrop = ClipboardFileDrop{};
            return result;
        }
    } // namespace clipboard
} // namespace sally
