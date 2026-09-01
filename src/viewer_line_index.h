// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

#include <vector>

// Sparse-checkpoint bookkeeping for the viewer's decoded (BOM-marked Unicode) line scan.
//
// Kept out of CViewerWindow so the two subtle parts can be exercised headlessly: the resume
// search, and the packing of the line policy into the cache key. Both failed silently when they
// were wrong - boundaries shifted rather than anything erroring - which is exactly the kind of
// logic that should not live only inside a window class.

struct DecodedLineRecord
{
    __int64 Begin;     // offset of the first raw byte of the line
    __int64 End;       // offset just past the last displayed byte
    __int64 NextBegin; // offset where the following line starts
    __int64 CellCount; // decoded cells on the line
};

// Packs the end-of-line policy into a comparable key.
//
// These come from Configuration and every one of them moves line boundaries. None was in the
// cache key originally; regex search flips EOL_NULL directly, so stale boundaries were reused in
// both directions - search on display-built ones, then painting on search-built ones.
int PackDecodedEolFlags(bool eolCRLF, bool eolCR, bool eolLF, bool eolNULL);

// The checkpoint to resume from when looking for the line preceding 'seek': the last one whose
// FOLLOWING line still starts before seek, so resuming there cannot overshoot. NULL means start
// from the beginning of the text.
//
// Requires 'checkpoints' sorted by Begin, which is how the scan appends them.
const DecodedLineRecord* FindDecodedResumeCheckpoint(const std::vector<DecodedLineRecord>& checkpoints,
                                                     __int64 seek);

// Whether the line at 'ordinal' should be kept as a checkpoint. Keeps every 'stride'th line and
// refuses to append out of order, since the search above depends on the ordering.
bool ShouldRecordDecodedCheckpoint(__int64 ordinal, int stride,
                                   const std::vector<DecodedLineRecord>& checkpoints,
                                   __int64 lineBegin);
