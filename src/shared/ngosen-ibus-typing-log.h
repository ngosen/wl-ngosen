/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-recorder.h"

namespace ngosen {

    // Saves the last typing events to a file to attach to a bug report, and tells the user where.
    void saveTypingLog(const Recorder& recorder);

} // namespace ngosen
