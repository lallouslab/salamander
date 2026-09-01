// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// No precomp.h: this is plain logic over plain structs, and staying self-contained is what lets
// the headless suite compile the real translation unit.
#include "viewer_line_index.h"

int PackDecodedEolFlags(bool eolCRLF, bool eolCR, bool eolLF, bool eolNULL)
{
    return (eolCRLF ? 1 : 0) | (eolCR ? 2 : 0) | (eolLF ? 4 : 0) | (eolNULL ? 8 : 0);
}

const DecodedLineRecord* FindDecodedResumeCheckpoint(const std::vector<DecodedLineRecord>& checkpoints,
                                                     __int64 seek)
{
    size_t lo = 0;
    size_t hi = checkpoints.size();
    const DecodedLineRecord* best = NULL;
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (checkpoints[mid].NextBegin < seek)
        {
            best = &checkpoints[mid];
            lo = mid + 1;
        }
        else
            hi = mid;
    }
    return best;
}

bool ShouldRecordDecodedCheckpoint(__int64 ordinal, int stride,
                                   const std::vector<DecodedLineRecord>& checkpoints,
                                   __int64 lineBegin)
{
    if (stride <= 0)
        return false;
    if ((ordinal % stride) != 0)
        return false;
    // Never append out of order: the resume search binary-searches this vector.
    return checkpoints.empty() || checkpoints.back().Begin < lineBegin;
}
