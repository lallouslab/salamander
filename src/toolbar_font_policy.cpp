// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_TOOLBAR_FONT_POLICY_STANDALONE lets the headless test compile this real
// translation unit without Sally's precompiled header.
#ifndef SALLY_TOOLBAR_FONT_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "toolbar_font_policy.h"

LOGFONT MakeBoldToolbarLogFont(const LOGFONT& base)
{
    LOGFONT lf = base;
    lf.lfWeight = FW_BOLD;
    return lf;
}
