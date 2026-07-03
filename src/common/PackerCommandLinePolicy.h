// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

namespace sally::pack
{

constexpr std::size_t kLegacyDosCommandLineLimit = 128;

inline bool ShouldRejectLegacyCommandLine(bool supportsLongNames, std::size_t commandLineLength, bool usesListFile)
{
    return !supportsLongNames &&
           !usesListFile &&
           commandLineLength >= kLegacyDosCommandLineLimit;
}

} // namespace sally::pack
