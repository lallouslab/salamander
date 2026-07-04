// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace sally::find
{

enum class FindResultsFormat
{
    Csv,
    Text,
};

struct FindResultRecord
{
    std::wstring Path;
    std::wstring Name;
    unsigned __int64 Size = 0;
    FILETIME LastWrite = {};
    DWORD Attr = 0;
    bool IsDir = false;

    std::wstring FullPath() const;
};

std::wstring SerializeFindResultsCsv(const std::vector<FindResultRecord>& records);
std::wstring SerializeFindResultsText(const std::vector<FindResultRecord>& records);

bool ParseFindResultsCsv(const std::wstring& text, std::vector<FindResultRecord>& records,
                         std::wstring* error);
bool ParseFindResultsText(const std::wstring& text, std::vector<FindResultRecord>& records,
                          size_t* skippedRows, std::wstring* error);

bool SaveFindResultsFile(const std::wstring& fileName, FindResultsFormat format,
                         const std::vector<FindResultRecord>& records, std::wstring* error);
bool LoadFindResultsFile(const std::wstring& fileName, FindResultsFormat format,
                         std::vector<FindResultRecord>& records, size_t* skippedRows,
                         std::wstring* error);

bool TryFindResultsFormatFromPathOrFilter(const std::wstring& fileName, DWORD filterIndex,
                                          FindResultsFormat* format);
void AppendFindResultsDefaultExtension(std::wstring& fileName, FindResultsFormat format);

} // namespace sally::find
