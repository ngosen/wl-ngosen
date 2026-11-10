/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct zwp_input_method_manager_v2;
struct zwp_input_method_v2;
struct zwp_input_method_keyboard_grab_v2;
struct zwp_virtual_keyboard_manager_v1;
struct zwp_virtual_keyboard_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace ngosen {

    // What the app told the compositor about the focused field, applied on each done.
    struct WaylandField {
        bool        active  = false;
        bool        hasText = false; // surrounding_text arrived since activate
        std::string text;            // bytes, as the protocol sends them
        uint32_t    cursor     = 0;
        uint32_t    anchor     = 0;
        bool        causeOther = false; // the last text change was not ours
        uint32_t    hint       = 0;
        uint32_t    purpose    = 0;
    };

    struct ImEvent {
        enum class Kind {
            Done,
            Key,
            Modifiers,
            Keymap,
            RepeatInfo,
            Unavailable
        } kind;
        WaylandField field;         // Done
        uint32_t     time      = 0; // Key
        uint32_t     code      = 0; // Key: evdev code
        bool         pressed   = false;
        uint32_t     depressed = 0, latched = 0, locked = 0, group = 0; // Modifiers
        int32_t      rate = 0, delay = 0;                               // RepeatInfo
        bool         focusChanged = false;                              // Done: activate or deactivate came before it
    };

    // One seat's input method and virtual keyboard. Wayland callbacks only record what arrived; the
    // daemon takes the events in order from the main loop, so a request can check, right before it
    // goes out, that no focus change is waiting.
    class InputMethod {
      public:
        InputMethod();
        ~InputMethod();
        InputMethod(const InputMethod&)            = delete;
        InputMethod& operator=(const InputMethod&) = delete;

        // False when the compositor offers no input method (GNOME, KDE) or no virtual keyboard.
        bool connect();
        int  fd() const;
        // Reads what the socket holds without blocking, then runs the Wayland callbacks.
        bool                   readEvents();
        bool                   flush();
        std::optional<ImEvent> takeEvent();
        bool                   hasEvents() const {
            return !events_.empty();
        }

        // Applies one event taken from takeEvent.
        void                apply(const ImEvent& event);

        const WaylandField& field() const {
            return field_;
        }
        xkb_state* xkbState() const {
            return state_;
        }
        bool hasKeymap() const {
            return keymap_ != nullptr;
        }
        int32_t repeatRate() const {
            return repeatRate_;
        }
        int32_t repeatDelay() const {
            return repeatDelay_;
        }
        uint32_t depressedMods() const {
            return depressed_;
        }
        wl_display* display() const {
            return display_;
        }
        zwlr_foreign_toplevel_manager_v1* toplevelManager() const {
            return toplevelManager_;
        }

        // False when nothing may go to the field: it is not focused, or a focus change is waiting.
        bool maySend();
        void commitString(const std::string& text);
        void setPreedit(const std::string& text);
        void deleteSurrounding(uint32_t beforeBytes, uint32_t afterBytes);
        void commit();

        // A key on the virtual keyboard; extraMods are OR-ed onto the grab's modifiers for this key.
        void sendKey(uint32_t time, uint32_t code, bool pressed, uint32_t extraMods = 0);
        void releasePressedKeys(uint32_t time);
        // Keycode that types sym with the current layout and level, or 0.
        uint32_t codeForSym(uint32_t sym) const;
        uint32_t modIndexMask(const char* name) const;

        // Gives everything back: pressed keys, modifiers, the grab.
        void shutdown();

      private:
        static void onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
        static void onGlobalRemove(void* data, wl_registry* registry, uint32_t name);
        void        bindInputMethod();
        void        grab();
        void        ungrab(uint32_t time);
        void        setKeymap(int fd, uint32_t format, uint32_t size);
        void        sendModifiers(uint32_t extra);

        friend struct ImListeners;

        wl_display*                        display_         = nullptr;
        wl_registry*                       registry_        = nullptr;
        wl_seat*                           seat_            = nullptr;
        uint32_t                           seatName_        = 0;
        int                                seats_           = 0;
        zwp_input_method_manager_v2*       manager_         = nullptr;
        uint32_t                           managerName_     = 0;
        zwp_virtual_keyboard_manager_v1*   vkManager_       = nullptr;
        uint32_t                           vkManagerName_   = 0;
        zwlr_foreign_toplevel_manager_v1*  toplevelManager_ = nullptr;
        zwp_input_method_v2*               im_              = nullptr;
        zwp_input_method_keyboard_grab_v2* grab_            = nullptr;
        zwp_virtual_keyboard_v1*           vk_              = nullptr;
        bool                               vkHasKeymap_     = false;
        bool                               globalLost_      = false;

        // What arrived since the last done, applied on the next one.
        WaylandField pending_;
        bool         pendingActive_ = false;
        WaylandField field_;
        uint32_t     serial_ = 0;
        // activate or deactivate events read since the last done.
        int                 focusEventsSinceDone_ = 0;
        std::deque<ImEvent> events_;

        xkb_context*        context_ = nullptr;
        xkb_keymap*         keymap_  = nullptr;
        xkb_state*          state_   = nullptr;
        std::string         keymapText_;
        uint32_t            depressed_ = 0, latched_ = 0, locked_ = 0, group_ = 0;
        int32_t             repeatRate_ = 25, repeatDelay_ = 600;
        // Virtual keys pressed and not yet released, with the time of the press.
        std::map<uint32_t, uint32_t> pressed_;
    };

} // namespace ngosen
