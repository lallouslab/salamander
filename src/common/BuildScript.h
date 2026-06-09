// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// BuildScript — standalone script builder from CSelectionSnapshot.
//
// Builds a COperations script without depending on CFilesWindow,
// Configuration globals, gEnvironment, or any UI. The current hardened
// surface handles file-only Delete, Copy, and Move snapshots.
//
// Designed for headless / integration test use.

#pragma once

#include "CSelectionSnapshot.h"
#include "CBuildConfig.h"
#include "CBuildScriptState.h"

class COperations; // forward declare

// Build a COperations script from a snapshot + config.
// Handles Delete, Copy, Move operations.
// Returns TRUE on success, FALSE on error (e.g. out of memory).
BOOL BuildScriptFromSnapshot(
    const CSelectionSnapshot& snapshot,
    const CBuildConfig& config,
    CBuildScriptState& state,
    COperations* script);
