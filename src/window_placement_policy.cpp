// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_WINDOW_PLACEMENT_POLICY_STANDALONE lets the headless test compile this real
// translation unit without Sally's precompiled header.
#ifndef SALLY_WINDOW_PLACEMENT_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "window_placement_policy.h"

namespace
{
long Clamp(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

long OverlapArea(const RECT& a, const RECT& b)
{
    long ix = (a.right < b.right ? a.right : b.right) - (a.left > b.left ? a.left : b.left);
    long iy = (a.bottom < b.bottom ? a.bottom : b.bottom) - (a.top > b.top ? a.top : b.top);
    if (ix <= 0 || iy <= 0)
        return 0;
    return ix * iy;
}
} // namespace

RECT SanitizeWindowRect(RECT rect, const RECT* monitors, int monitorCount, int minW, int minH)
{
    // Normalize.
    if (rect.right < rect.left)
    {
        long t = rect.left;
        rect.left = rect.right;
        rect.right = t;
    }
    if (rect.bottom < rect.top)
    {
        long t = rect.top;
        rect.top = rect.bottom;
        rect.bottom = t;
    }

    long w = rect.right - rect.left;
    long h = rect.bottom - rect.top;
    const bool degenerate = (w < minW) || (h < minH);
    if (w < minW)
        w = minW;
    if (h < minH)
        h = minH;

    // No monitor info: just enforce the minimum size at the saved top-left.
    if (monitors == NULL || monitorCount <= 0)
    {
        RECT out = {rect.left, rect.top, rect.left + w, rect.top + h};
        return out;
    }

    // Best-overlapping monitor for the (min-sized) rect.
    RECT probe = {rect.left, rect.top, rect.left + w, rect.top + h};
    int best = -1;
    long bestArea = 0;
    for (int i = 0; i < monitorCount; i++)
    {
        long area = OverlapArea(probe, monitors[i]);
        if (area > bestArea)
        {
            bestArea = area;
            best = i;
        }
    }

    // Enough of the window is visible on some monitor if at least a quarter-min tile shows.
    const long minVisible = (long)(minW / 2) * (long)(minH / 2);
    const bool visible = best >= 0 && bestArea >= minVisible;

    const RECT& m = monitors[best >= 0 ? best : 0];
    const long mw = m.right - m.left;
    const long mh = m.bottom - m.top;
    const bool fits = (w <= mw) && (h <= mh);

    if (visible && !degenerate && fits)
        return probe; // already sane, on-screen and no bigger than its monitor - leave it.

    // Degenerate, off-screen or oversize: clamp to the monitor and move it on-screen.
    if (w > mw)
        w = mw;
    if (h > mh)
        h = mh;
    long left = Clamp(rect.left, m.left, m.right - w);
    long top = Clamp(rect.top, m.top, m.bottom - h);
    RECT out = {left, top, left + w, top + h};
    return out;
}
