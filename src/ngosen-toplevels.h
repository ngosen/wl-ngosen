/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace ngosen {

    // The compositor's windows, kept only to name the app of the focused field.
    class Toplevels {
      public:
        explicit Toplevels(zwlr_foreign_toplevel_manager_v1* manager);
        ~Toplevels();
        Toplevels(const Toplevels&)            = delete;
        Toplevels& operator=(const Toplevels&) = delete;

        // app_id of the window marked activated in its last done; empty when there is none, as for a
        // layer-shell launcher.
        std::string activeAppId() const;
        // The same app named as the IBus edition names it on GNOME, so a Flatpak app keeps its rules.
        std::string activeProgram() const;

      private:
        struct Window {
            Toplevels*                       owner  = nullptr;
            zwlr_foreign_toplevel_handle_v1* handle = nullptr;
            std::string                      appId, pendingAppId, program;
            bool                             activated = false, pendingActivated = false;
        };
        friend struct ToplevelListeners;

        void                                 addWindow(zwlr_foreign_toplevel_handle_v1* handle);
        void                                 removeWindow(Window* window);
        const Window*                        activeWindow() const;

        zwlr_foreign_toplevel_manager_v1*    manager_;
        std::vector<std::unique_ptr<Window>> windows_;
    };

} // namespace ngosen
