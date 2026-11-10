/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-toplevels.h"

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
            auto* window      = static_cast<Window*>(data);
            window->appId     = window->pendingAppId;
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

    std::string Toplevels::activeApp() const {
        for (const auto& window : windows_) {
            if (window->activated)
                return window->appId;
        }
        return {};
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
