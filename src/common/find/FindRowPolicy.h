// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "../unicode/helpers.h"

namespace sally::find
{

// Until Find actions route wide (kb/unicode P0-b), commands that consume a
// result row's ANSI mirrors (focus, open, view/edit) must refuse rows whose
// names do not round-trip CP_ACP exactly: best-fit mapping (e.g. 'ā' -> 'a')
// can make the ANSI mirror silently target a DIFFERENT existing file, and
// replacement '?' at best fails and at worst matches as a wildcard.
// Delete and Save Results consume the wide fields and take all rows.
inline bool RowActionableViaAnsi(const std::wstring& pathW, const std::wstring& nameW)
{
    std::string probe;
    return sally::unicode::TryWideToAnsiRoundTripExact(pathW, probe) &&
           sally::unicode::TryWideToAnsiRoundTripExact(nameW, probe);
}

} // namespace sally::find
