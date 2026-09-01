// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

// Shared dark-mode combobox painting bits.
//
// There were three near-identical combo painters - darkmode.cpp, plugins/shared/plugindarkmode.cpp
// and find_dialog_results.cpp - and all three had the same defect: neither drew the selected item.
// A CBS_DROPDOWN combo has an edit child that covers for that, so it went unnoticed since v1.0.14
// on Find's one CBS_DROPDOWNLIST control, and was then copied into the general painter, blanking
// every drop-list combo in the application including the theme selector on the Appearance page.
//
// One implementation now, so a fix cannot land in one copy and be forgotten in the others. Colours
// are passed in rather than read from a global, which also makes this testable against a real
// control without pulling in Sally.

struct ComboDarkColors
{
    COLORREF InputText;
    COLORREF DisabledText;
    COLORREF Highlight;
    COLORREF HighlightText;
};

// TRUE when the combo has a genuine edit child, with its rect in parent coordinates.
//
// Rejects hwndItem == hwnd explicitly: a drop-list combo reports either NULL or itself, and
// treating the control as its own child yields a rect covering the entire client, which callers
// then hand to ExcludeClipRect - erasing everything they were about to draw.
BOOL ComboDarkGetEditChildRect(HWND combo, RECT* editRectInParent);

// Draws the current selection into the area left of the drop button. No-op when nothing is
// selected. Uses the control's own font, and the highlight colours when it has focus.
void ComboDarkDrawSelectedItem(HWND combo, HDC dc, const RECT& client, const RECT& dropButton,
                               const ComboDarkColors& colors);
