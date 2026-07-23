// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include "IPrompter.h"

namespace DeletePromptPolicy
{
UINT ConfirmDeleteMessageBoxFlags();
PromptResult MapConfirmDeleteResult(int dialogResult);
} // namespace DeletePromptPolicy

