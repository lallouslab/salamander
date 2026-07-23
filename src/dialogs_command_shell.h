// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/WindowsTerminalService.h"

bool ShowDefaultShellDialog(HWND parent, const WindowsTerminalCatalog& catalog,
                            const ShellTarget& current, ShellTarget& selected);
