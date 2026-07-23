// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Render-time resolution of the dark-mode "base" (non-mask) panel selection/focus colors.
//
// A handful of panel color indices are stored flag-0 in the color schemes, meaning their
// RGB value IS the source of truth (they are not SCF_DEFAULT-derived from system colors).
// UpdateDefaultColors() must therefore NOT overwrite them in place for dark mode: doing so
// destroys the light-mode RGB and leaves the selection colors dark after a dark->light
// switch (issue #81 follow-up). Instead the canonical dark value is applied here, at paint
// time, exactly mirroring how the highlight-mask dark contrast is already handled in
// files_window_paint.cpp (see commit 063325a0).
//
// Affected indices: FOCUS_FG_INACTIVE_NORMAL/SELECTED, ITEM_FG_SELECTED/FOCSEL,
// ITEM_BK_FOCUSED/FOCSEL, ICON_BLEND_FOCUSED/FOCSEL. Every other index passes through.
//
// The dark-mode decision is passed in (rather than queried here) so this stays a pure
// function with no dependency on the dark-mode subsystem — trivially unit-testable.
COLORREF ResolveDarkBaseColor(int colorIndex, COLORREF lightValue, bool darkMode);
