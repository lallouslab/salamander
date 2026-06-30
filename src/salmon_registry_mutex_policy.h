// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "registry_names.h"

namespace sally::salmon
{
inline constexpr bool kBugReporterRegistryMutexIsGlobalByPolicy = true;

inline const char* BugReporterRegistryMutexName()
{
    return SAL_REG_MUTEX_GLOBAL_BUG_REPORTER_A;
}
} // namespace sally::salmon
