// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
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
