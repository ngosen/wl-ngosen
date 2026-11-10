/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-toplevels.h"

#include "ngosen-ibus-program-name.h"

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <algorithm>

namespace ngosen {

    struct ToplevelListeners {
        using Window = Toplevels::Window;

        static void title(void*, zwlr_foreign_toplevel_handle_v1*, const char*) {}
        static void appId(void* data, zwlr_foreign_toplevel_handle_v1*, const char* appId) {
            static_cast<Window*>(data)->pendingAppId = appId != nullptr ? appId : "";
        }
        static void outputEnter(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
        static void outputLeave(void*, zwlr_foreign_toplevel_handle_v1*, wl_output*) {}
        static void state(void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* states) {
            auto*       window       = static_cast<Window*>(data);
            const auto* begin        = static_cast<const uint32_t*>(states->data);
            const auto* end          = begin + states->size / sizeof(uint32_t);
            window->pendingActivated = std::find(begin, end, ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) != end;
        }
        static void done(void* data, zwlr_foreign_toplevel_handle_v1*) {
            auto* window = static_cast<Window*>(data);
            // Named once per app_id: finding the desktop file reads the disk.
            if (window->appId != window->pendingAppId) {
                window->appId   = window->pendingAppId;
                window->program = window->appId.empty() ? std::string() : gnomeProgramName(window->appId);
            }
            window->activated = window->pendingActivated;
        }
        static void closed(void* data, zwlr_foreign_toplevel_handle_v1*) {
            auto* window = static_cast<Window*>(data);
            window->owner->removeWindow(window);
        }
        static void                                               parent(void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) {}

        static constexpr zwlr_foreign_toplevel_handle_v1_listener handle = {title, appId, outputEnter, outputLeave, state, done, closed, parent};

        static void                                               toplevel(void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle) {
            static_cast<Toplevels*>(data)->addWindow(handle);
        }
        static void                                                finished(void*, zwlr_foreign_toplevel_manager_v1*) {}

        static constexpr zwlr_foreign_toplevel_manager_v1_listener manager = {toplevel, finished};
    };

    Toplevels::Toplevels(zwlr_foreign_toplevel_manager_v1* manager) : manager_(manager) {
        zwlr_foreign_toplevel_manager_v1_add_listener(manager_, &ToplevelListeners::manager, this);
    }

    Toplevels::~Toplevels() {
        for (const auto& window : windows_)
            zwlr_foreign_toplevel_handle_v1_destroy(window->handle);
    }

    const Toplevels::Window* Toplevels::activeWindow() const {
        for (const auto& window : windows_) {
            if (window->activated)
                return window.get();
        }
        return nullptr;
    }

    std::string Toplevels::activeAppId() const {
        const Window* window = activeWindow();
        return window != nullptr ? window->appId : std::string();
    }

    std::string Toplevels::activeProgram() const {
        const Window* window = activeWindow();
        return window != nullptr ? window->program : std::string();
    }

    void Toplevels::addWindow(zwlr_foreign_toplevel_handle_v1* handle) {
        auto window    = std::make_unique<Window>();
        window->owner  = this;
        window->handle = handle;
        zwlr_foreign_toplevel_handle_v1_add_listener(handle, &ToplevelListeners::handle, window.get());
        windows_.push_back(std::move(window));
    }

    void Toplevels::removeWindow(Window* window) {
        zwlr_foreign_toplevel_handle_v1_destroy(window->handle);
        windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [window](const auto& w) { return w.get() == window; }), windows_.end());
    }

} // namespace ngosen
