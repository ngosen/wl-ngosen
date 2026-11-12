/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gio/gio.h>
#include <memory>
#include <string>

namespace ngosen {

    // A short desktop notification that replaces the last one, as fcitx5 shows the new input method
    // when it switches. Without a notification server nothing is shown and typing goes on.
    class Notice {
      public:
        Notice();
        ~Notice();
        Notice(const Notice&)            = delete;
        Notice& operator=(const Notice&) = delete;

        void    show(const std::string& text, const char* icon);

      private:
        struct Shared {
            guint32 id = 0;
        };
        GDBusConnection* bus_ = nullptr;
        // The reply can come after the daemon quit, so it writes the id into memory it shares.
        std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    };

} // namespace ngosen
