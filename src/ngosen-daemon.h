/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

namespace ngosen {

    // Connects to the compositor and types until it says the input method is unavailable or a signal
    // arrives. Returns the exit status: 0 on a signal or a compositor without input methods.
    int runDaemon();

} // namespace ngosen
