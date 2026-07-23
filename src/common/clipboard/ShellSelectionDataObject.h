// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <objidl.h>

#include <string>
#include <vector>

namespace sally
{
namespace clipboard
{
HRESULT CreateShellSelectionDataObject(const std::wstring& parentPath,
                                       const std::vector<std::wstring>& itemPaths,
                                       IDataObject** dataObject);
} // namespace clipboard
} // namespace sally
