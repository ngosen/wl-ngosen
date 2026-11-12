/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <glib.h>

namespace ngosen {

    // The modifier bits of X and IBus. The code copied from the IBus edition reads key states in this
    // form, so xkbcommon states are turned into it.
    constexpr guint ShiftMask   = 1U << 0;
    constexpr guint LockMask    = 1U << 1;
    constexpr guint ControlMask = 1U << 2;
    constexpr guint Mod1Mask    = 1U << 3;
    constexpr guint Mod2Mask    = 1U << 4;
    constexpr guint Mod3Mask    = 1U << 5;
    constexpr guint Mod4Mask    = 1U << 6;
    constexpr guint Mod5Mask    = 1U << 7;
    constexpr guint SuperMask   = 1U << 26;
    constexpr guint HyperMask   = 1U << 27;
    constexpr guint MetaMask    = 1U << 28;
    constexpr guint ReleaseMask = 1U << 30;

} // namespace ngosen
