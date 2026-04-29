// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace sally
{
namespace copy
{
constexpr std::uint64_t kFastLocalAsyncMinSize = 64ull * 1024ull * 1024ull;

enum class Strategy
{
    SyncLoop,
    AsyncLoop,
    AsyncLoopWithoutLocalBufferingDisable,
    CopyFileExCandidate,
};

struct StrategyInput
{
    std::uint64_t FileSize = 0;
    bool SourceIsNetwork = false;
    bool SourceIsFast = false;
    bool SourceIsRemovable = false;
    bool TargetIsNetwork = false;
    bool TargetIsFast = false;
    bool TargetIsRemovable = false;
    bool AsyncEnabled = false;
    bool SimpleCopyEligible = false;
    bool FastLocalAsyncEnabled = false;
    bool DisableNetworkLocalBuffering = true;
    bool CopyFileExCandidateEnabled = false;
};

inline bool IsNetworkCopy(const StrategyInput& input)
{
    return input.SourceIsNetwork || input.TargetIsNetwork;
}

inline bool IsLegacyNetworkAsyncEligible(const StrategyInput& input)
{
    return (input.SourceIsNetwork && (input.TargetIsNetwork || input.TargetIsFast)) ||
           (input.TargetIsNetwork && input.SourceIsFast);
}

inline bool IsLargeFastLocalAsyncEligible(const StrategyInput& input)
{
    return input.FastLocalAsyncEnabled &&
           input.SimpleCopyEligible &&
           input.SourceIsFast &&
           input.TargetIsFast &&
           !input.SourceIsNetwork &&
           !input.TargetIsNetwork &&
           !input.SourceIsRemovable &&
           !input.TargetIsRemovable &&
           input.FileSize >= kFastLocalAsyncMinSize;
}

inline Strategy SelectStrategy(const StrategyInput& input)
{
    if (input.FileSize == 0)
        return Strategy::SyncLoop;

    if (input.CopyFileExCandidateEnabled && input.SimpleCopyEligible)
        return Strategy::CopyFileExCandidate;

    if (!input.AsyncEnabled)
        return Strategy::SyncLoop;

    const bool asyncEligible = IsLegacyNetworkAsyncEligible(input) ||
                               IsLargeFastLocalAsyncEligible(input);
    if (!asyncEligible)
        return Strategy::SyncLoop;

    if (IsNetworkCopy(input) && !input.DisableNetworkLocalBuffering)
        return Strategy::AsyncLoopWithoutLocalBufferingDisable;

    return Strategy::AsyncLoop;
}

inline bool UsesAsyncLoop(Strategy strategy)
{
    return strategy == Strategy::AsyncLoop ||
           strategy == Strategy::AsyncLoopWithoutLocalBufferingDisable;
}

inline bool DisablesNetworkLocalBuffering(Strategy strategy)
{
    return strategy == Strategy::AsyncLoop;
}
} // namespace copy
} // namespace sally
