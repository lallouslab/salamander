// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DeletePromptPolicy.h"

namespace DeletePromptPolicy
{
UINT ConfirmDeleteMessageBoxFlags()
{
    // Intentional product choice: Enter confirms delete by default.
    return MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1;
}

PromptResult MapConfirmDeleteResult(int dialogResult)
{
    if (dialogResult == IDYES)
        return {PromptResult::kYes};
    return {PromptResult::kNo};
}
} // namespace DeletePromptPolicy
