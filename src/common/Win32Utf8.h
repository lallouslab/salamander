// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

bool Win32StrictUtf8ToWide(const char* text, size_t length, std::wstring& wide);
