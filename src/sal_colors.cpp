// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SAL_COLORS_STANDALONE
#include "precomp.h"
#endif

#include "sal_colors.h"
#include "sal_color_ids.h"

// Canonical dark-mode palette for the base selection/focus colors that are stored flag-0 in
// the schemes. These values are identical to the ones formerly written in place by
// UpdateDefaultColors()'s dark block, so dark mode renders pixel-for-pixel the same; the
// difference is that resolving here (at paint time) keeps the schemes' light RGB intact, so
// a dark->light switch correctly restores the light selection colors.
COLORREF ResolveDarkBaseColor(int colorIndex, COLORREF lightValue, bool darkMode)
{
    if (!darkMode)
        return lightValue;

    switch (colorIndex)
    {
    case ITEM_FG_SELECTED:
    case ITEM_FG_FOCSEL:
        return RGB(255, 255, 255);
    case ITEM_BK_FOCUSED:
    case ITEM_BK_FOCSEL:
        return RGB(62, 125, 231);
    case ICON_BLEND_FOCUSED:
        return RGB(150, 150, 150);
    case ICON_BLEND_FOCSEL:
        return RGB(120, 170, 255);
    case FOCUS_FG_INACTIVE_NORMAL:
        return RGB(120, 120, 120);
    case FOCUS_FG_INACTIVE_SELECTED:
        return RGB(150, 150, 150);
    default:
        return lightValue;
    }
}
