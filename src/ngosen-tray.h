/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-tray-menu.h"

#include <functional>
#include <gio/gio.h>
#include <string>
#include <vector>

namespace ngosen {

    // A StatusNotifierItem with a dbusmenu, which bars such as Waybar, swaybar and Noctalia show.
    // Every call runs on the main loop, as the bus delivers to the context that registered.
    class Tray {
      public:
        struct Handlers {
            // A left click on the icon.
            std::function<void()> activate;
            // A click on a menu item with an action.
            std::function<void(const std::string& action)> click;
        };

        explicit Tray(Handlers handlers);
        ~Tray();
        Tray(const Tray&)            = delete;
        Tray& operator=(const Tray&) = delete;

        // False without a session bus; typing goes on without a tray.
        bool start();
        void show(std::vector<TrayItem> menu, bool english);

        // Called from the bus handlers.
        void      methodCall(const char* interface, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation);
        GVariant* property(const char* interface, const char* name);
        void      nameAcquired();
        void      watcherAppeared();
        void      watcherVanished();

      private:
        void                  registerWithWatcher();
        void                  menuCall(const char* method, GVariant* parameters, GDBusMethodInvocation* invocation);
        bool                  knownItem(gint32 id) const;
        void                  getLayout(GVariant* parameters, GDBusMethodInvocation* invocation);
        void                  getGroupProperties(GVariant* parameters, GDBusMethodInvocation* invocation);
        void                  getProperty(GVariant* parameters, GDBusMethodInvocation* invocation);
        void                  events(GVariant* parameters, GDBusMethodInvocation* invocation, bool group);
        void                  itemCall(const char* method, GDBusMethodInvocation* invocation);
        void                  clicked(int id, const char* event);
        GVariant*             layout(int id, int depth) const;
        GVariant*             itemProperties(int id) const;
        GVariant*             toolTip() const;
        void                  emitItem(const char* signal, GVariant* parameters);

        Handlers              handlers_;
        GDBusConnection*      bus_ = nullptr;
        std::string           name_;
        guint                 itemObject_ = 0, menuObject_ = 0, ownId_ = 0, watchId_ = 0;
        bool                  nameOwned_ = false, watcherUp_ = false;
        std::vector<TrayItem> menu_;
        // Every item by its dbusmenu id; 0 is the root.
        std::vector<const TrayItem*>  items_;
        std::vector<std::vector<int>> children_;
        guint                         revision_ = 0;
        bool                          english_  = false;
    };

} // namespace ngosen
