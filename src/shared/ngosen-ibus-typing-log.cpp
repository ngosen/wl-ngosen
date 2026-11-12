/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-typing-log.h"
#include "ngosen-ibus-i18n.h"

#include "ngosen-log.h"

#include <gio/gio.h>
#include <glib/gstdio.h>

namespace ngosen {

    namespace {

        std::string env(const char* name) {
            const char* value = g_getenv(name);
            return value != nullptr ? value : "";
        }

        std::string typingLogDir() {
            return std::string(g_get_user_state_dir()) + "/ngosen";
        }

        std::string writeTypingLog(const Recorder& recorder) {
            const std::string dir = typingLogDir();
            // The log holds what the user typed, possibly passwords; keep it from other users.
            if (g_mkdir_with_parents(dir.c_str(), 0700) != 0 || g_chmod(dir.c_str(), 0700) != 0)
                return {};
            GDateTime*        now   = g_date_time_new_now_local();
            gchar*            stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
            const std::string path  = dir + "/typing-" + stamp + ".log";
            g_free(stamp);
            g_date_time_unref(now);
            const std::string text = std::string("# Ngó Sen for Wayland ") + NGOSEN_IBUS_VERSION +
                " typing log. It holds what you typed just before saving; read it before sharing.\n# desktop=" + env("XDG_CURRENT_DESKTOP") +
                " session=" + env("XDG_SESSION_TYPE") + " wayland=" + env("WAYLAND_DISPLAY") + "\n" + recorder.dump();
            GError* error = nullptr;
            if (!g_file_set_contents_full(path.c_str(), text.data(), static_cast<gssize>(text.size()), G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error)) {
                NGOSEN_WARN("Cannot write " + path + ": " + error->message);
                g_error_free(error);
                return {};
            }
            return path;
        }

        // The click comes from the GNOME input source menu, so there is no field to show text next to.
        void notify(const std::string& text) {
            GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
            if (bus == nullptr)
                return;
            g_dbus_connection_call(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
                                   g_variant_new("(susssasa{sv}i)", "Ngó Sen", 0U, "", text.c_str(), "", nullptr, nullptr, -1), nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
                                   nullptr, nullptr);
            g_object_unref(bus);
        }

    } // namespace

    void saveTypingLog(const Recorder& recorder) {
        const std::string path    = writeTypingLog(recorder);
        const std::string message = !path.empty() ? _("Typing log saved: ") + path : _("Could not save the typing log to ") + typingLogDir();
        NGOSEN_INFO(message);
        notify(message);
    }

} // namespace ngosen
