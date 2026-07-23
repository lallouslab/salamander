// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IExecutableLocator.h"

#include <cstddef>

bool ParseAppExecutionAliasReparseData(const void* data, size_t size,
                                       std::wstring& packageFamilyName,
                                       std::wstring& targetPath);
