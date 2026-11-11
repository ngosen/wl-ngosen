/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-notice.h"
#include "ngosen-log.h"

namespace ngosen {

    namespace {

        constexpr gint32 ShowMs = 1500;

        void             onReply(GObject* source, GAsyncResult* result, gpointer data) {
            auto*     shared = static_cast<std::shared_ptr<void>*>(data);
            GError*   error  = nullptr;
            GVariant* reply  = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (reply != nullptr) {
                guint32 id = 0;
                g_variant_get(reply, "(u)", &id);
                *static_cast<guint32*>(shared->get()) = id;
                g_variant_unref(reply);
            } else {
                NGOSEN_DEBUG(std::string("No notification: ") + error->message);
                g_error_free(error);
            }
            delete shared;
        }

    } // namespace

    Notice::Notice() {
        bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    }

    Notice::~Notice() {
        if (bus_ != nullptr)
            g_object_unref(bus_);
    }

    void Notice::show(const std::string& text, const char* icon) {
        if (bus_ == nullptr)
            return;
        GVariantBuilder hints;
        g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
        // Not kept in the notification history: it only says what the key just did.
        g_variant_builder_add(&hints, "{sv}", "transient", g_variant_new_boolean(TRUE));
        g_variant_builder_add(&hints, "{sv}", "urgency", g_variant_new_byte(0));
        const gchar* noActions[] = {nullptr};
        GVariant*    parameters  = g_variant_new("(susss^asa{sv}i)", "Ngó Sen", shared_->id, icon, text.c_str(), "", noActions, &hints, ShowMs);
        auto*        keep        = new std::shared_ptr<void>(shared_, &shared_->id);
        g_dbus_connection_call(bus_, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify", parameters,
                               G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, onReply, keep);
    }

} // namespace ngosen
