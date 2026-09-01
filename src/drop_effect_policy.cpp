// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_DROP_EFFECT_POLICY_STANDALONE lets the headless test compile this real
// translation unit without Sally's precompiled header.
#ifndef SALLY_DROP_EFFECT_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "drop_effect_policy.h"

// MK_ALT (0x20) is a valid IDropTarget grfKeyState flag but is not always defined by the
// SDK's <winuser.h> MK_* set.
#ifndef MK_ALT
#define MK_ALT 0x0020
#endif

DWORD ComputeOwnFolderDropEffect(DWORD allowedEffects, DWORD keyState,
                                 bool shellTargetAvailable)
{
    // Create-shortcut modifiers: Alt, or Ctrl+Shift (Explorer conventions).
    const bool wantLink = (keyState & MK_ALT) != 0 ||
                          (keyState & (MK_SHIFT | MK_CONTROL)) == (MK_SHIFT | MK_CONTROL);
    if (wantLink)
    {
        // Only offer it if something can actually carry it out. Without a shell drop target the
        // drop silently does nothing, so a shortcut cursor here is a lie.
        if (!shellTargetAvailable || (allowedEffects & DROPEFFECT_LINK) == 0)
            return DROPEFFECT_NONE;
        return DROPEFFECT_LINK;
    }

    allowedEffects &= DROPEFFECT_COPY | DROPEFFECT_MOVE;

    if ((keyState & MK_SHIFT) != 0 && (keyState & MK_CONTROL) == 0 &&
        (allowedEffects & DROPEFFECT_MOVE) != 0)
        return DROPEFFECT_MOVE;

    if ((keyState & MK_SHIFT) == 0 && (keyState & MK_CONTROL) != 0 &&
        (allowedEffects & DROPEFFECT_COPY) != 0)
        return DROPEFFECT_COPY;

    if ((allowedEffects & DROPEFFECT_COPY) != 0)
        return DROPEFFECT_COPY;
    if ((allowedEffects & DROPEFFECT_MOVE) != 0)
        return DROPEFFECT_MOVE;

    return DROPEFFECT_NONE;
}
