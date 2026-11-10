/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-input-method.h"

#include "input-method-unstable-v2-client-protocol.h"
#include "ngosen-log.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {
        constexpr uint32_t SeatVersion     = 1;
        constexpr uint32_t ToplevelVersion = 3;

        // A file descriptor holding text and its NUL, as keymap requests count it: the compositor maps
        // that many bytes.
        int memoryFile(const std::string& text) {
            const int fd = memfd_create("wl-ngosen-keymap", MFD_CLOEXEC);
            if (fd < 0)
                return -1;
            const std::size_t size = text.size() + 1;
            std::size_t       done = 0;
            while (done < size) {
                const ssize_t n = ::write(fd, text.c_str() + done, size - done);
                if (n <= 0) {
                    ::close(fd);
                    return -1;
                }
                done += static_cast<std::size_t>(n);
            }
            return fd;
        }
    } // namespace

    struct ImListeners {
        static void activate(void* data, zwp_input_method_v2*) {
            auto* im = static_cast<InputMethod*>(data);
            // A new activation starts from a blank field, as the protocol says.
            im->pending_       = WaylandField{};
            im->pendingActive_ = true;
            ++im->focusEventsSinceDone_;
        }
        static void deactivate(void* data, zwp_input_method_v2*) {
            auto* im           = static_cast<InputMethod*>(data);
            im->pendingActive_ = false;
            ++im->focusEventsSinceDone_;
        }
        static void surroundingText(void* data, zwp_input_method_v2*, const char* text, uint32_t cursor, uint32_t anchor) {
            auto* im             = static_cast<InputMethod*>(data);
            im->pending_.hasText = true;
            im->pending_.text    = text != nullptr ? text : "";
            im->pending_.cursor  = cursor;
            im->pending_.anchor  = anchor;
        }
        static void textChangeCause(void* data, zwp_input_method_v2*, uint32_t cause) {
            // text-input-v3 change_cause: 0 is input_method, 1 other.
            static_cast<InputMethod*>(data)->pending_.causeOther = cause != 0;
        }
        static void contentType(void* data, zwp_input_method_v2*, uint32_t hint, uint32_t purpose) {
            auto* im             = static_cast<InputMethod*>(data);
            im->pending_.hint    = hint;
            im->pending_.purpose = purpose;
        }
        static void done(void* data, zwp_input_method_v2*) {
            auto* im = static_cast<InputMethod*>(data);
            ++im->serial_;
            ImEvent event{ImEvent::Kind::Done};
            event.field               = im->pending_;
            event.field.active        = im->pendingActive_;
            event.focusChanged        = im->focusEventsSinceDone_ > 0;
            im->focusEventsSinceDone_ = 0;
            im->events_.push_back(event);
            // Text and cause are sent again before each done that changes them; the content type stays.
            im->pending_.causeOther = false;
            im->pending_.hasText    = false;
        }
        static void unavailable(void* data, zwp_input_method_v2*) {
            static_cast<InputMethod*>(data)->events_.push_back(ImEvent{ImEvent::Kind::Unavailable});
        }

        static void keymap(void* data, zwp_input_method_keyboard_grab_v2*, uint32_t format, int32_t fd, uint32_t size) {
            static_cast<InputMethod*>(data)->setKeymap(fd, format, size);
        }
        static void key(void* data, zwp_input_method_keyboard_grab_v2*, uint32_t /*serial*/, uint32_t time, uint32_t key, uint32_t state) {
            ImEvent event{ImEvent::Kind::Key};
            event.time    = time;
            event.code    = key;
            event.pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
            static_cast<InputMethod*>(data)->events_.push_back(event);
        }
        static void modifiers(void* data, zwp_input_method_keyboard_grab_v2*, uint32_t /*serial*/, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            ImEvent event{ImEvent::Kind::Modifiers};
            event.depressed = depressed;
            event.latched   = latched;
            event.locked    = locked;
            event.group     = group;
            static_cast<InputMethod*>(data)->events_.push_back(event);
        }
        static void repeatInfo(void* data, zwp_input_method_keyboard_grab_v2*, int32_t rate, int32_t delay) {
            ImEvent event{ImEvent::Kind::RepeatInfo};
            event.rate  = rate;
            event.delay = delay;
            static_cast<InputMethod*>(data)->events_.push_back(event);
        }
    };

    namespace {
        const zwp_input_method_v2_listener imListener = {
            ImListeners::activate,    ImListeners::deactivate, ImListeners::surroundingText, ImListeners::textChangeCause,
            ImListeners::contentType, ImListeners::done,       ImListeners::unavailable,
        };
        const zwp_input_method_keyboard_grab_v2_listener grabListener = {
            ImListeners::keymap,
            ImListeners::key,
            ImListeners::modifiers,
            ImListeners::repeatInfo,
        };
    } // namespace

    InputMethod::InputMethod() = default;

    InputMethod::~InputMethod() {
        shutdown();
        if (state_ != nullptr)
            xkb_state_unref(state_);
        if (keymap_ != nullptr)
            xkb_keymap_unref(keymap_);
        if (context_ != nullptr)
            xkb_context_unref(context_);
    }

    void InputMethod::onGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        auto* im = static_cast<InputMethod*>(data);
        if (std::strcmp(interface, wl_seat_interface.name) == 0) {
            // One seat: the first. Others are only counted.
            if (++im->seats_ == 1) {
                im->seat_     = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, SeatVersion));
                im->seatName_ = name;
            }
        } else if (std::strcmp(interface, zwp_input_method_manager_v2_interface.name) == 0) {
            im->manager_     = static_cast<zwp_input_method_manager_v2*>(wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1));
            im->managerName_ = name;
        } else if (std::strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
            im->vkManager_     = static_cast<zwp_virtual_keyboard_manager_v1*>(wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1));
            im->vkManagerName_ = name;
        } else if (std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0 && im->toplevelManager_ == nullptr) {
            const uint32_t bound = version < ToplevelVersion ? version : ToplevelVersion;
            im->toplevelManager_ = static_cast<zwlr_foreign_toplevel_manager_v1*>(wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, bound));
        }
    }

    void InputMethod::onGlobalRemove(void* data, wl_registry* /*registry*/, uint32_t name) {
        auto* im = static_cast<InputMethod*>(data);
        if (name == im->seatName_ || name == im->managerName_ || name == im->vkManagerName_) {
            im->globalLost_ = true;
            im->events_.push_back(ImEvent{ImEvent::Kind::Unavailable});
        }
    }

    InputMethod::Connection InputMethod::connect() {
        display_ = wl_display_connect(nullptr);
        if (display_ == nullptr) {
            NGOSEN_ERROR("Cannot connect to the Wayland compositor");
            return Connection::Failed;
        }
        static const wl_registry_listener listener = {onGlobal, onGlobalRemove};
        registry_                                  = wl_display_get_registry(display_);
        wl_registry_add_listener(registry_, &listener, this);
        if (wl_display_roundtrip(display_) < 0) {
            NGOSEN_ERROR("Lost the connection to the compositor");
            return Connection::Failed;
        }
        if (seats_ > 1)
            NGOSEN_INFO("The compositor has " + std::to_string(seats_) + " seats; typing on the first one only");
        if (seat_ == nullptr || manager_ == nullptr || vkManager_ == nullptr) {
            NGOSEN_INFO(std::string("The compositor offers no ") +
                        (seat_ == nullptr        ? "seat" :
                             manager_ == nullptr ? "input method (zwp_input_method_manager_v2)" :
                                                   "virtual keyboard (zwp_virtual_keyboard_manager_v1)"));
            return Connection::NotOffered;
        }
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        im_      = zwp_input_method_manager_v2_get_input_method(manager_, seat_);
        zwp_input_method_v2_add_listener(im_, &imListener, this);
        vk_ = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkManager_, seat_);
        if (wl_display_roundtrip(display_) < 0) {
            NGOSEN_ERROR("Lost the connection to the compositor");
            return Connection::Failed;
        }
        return Connection::Ready;
    }

    int InputMethod::fd() const {
        return display_ != nullptr ? wl_display_get_fd(display_) : -1;
    }

    bool InputMethod::readEvents() {
        if (wl_display_dispatch_pending(display_) < 0)
            return false;
        while (wl_display_prepare_read(display_) != 0) {
            if (wl_display_dispatch_pending(display_) < 0)
                return false;
        }
        pollfd p{fd(), POLLIN, 0};
        if (::poll(&p, 1, 0) > 0 && (p.revents & POLLIN) != 0) {
            if (wl_display_read_events(display_) < 0)
                return false;
        } else {
            wl_display_cancel_read(display_);
        }
        return wl_display_dispatch_pending(display_) >= 0;
    }

    bool InputMethod::flush() {
        return wl_display_flush(display_) >= 0 || errno == EAGAIN;
    }

    std::optional<ImEvent> InputMethod::takeEvent() {
        if (events_.empty())
            return std::nullopt;
        ImEvent event = std::move(events_.front());
        events_.pop_front();
        return event;
    }

    void InputMethod::apply(const ImEvent& event) {
        switch (event.kind) {
            case ImEvent::Kind::Done:
                if (event.focusChanged && field_.active)
                    ungrab(0);
                if (event.focusChanged || !field_.active) {
                    field_ = event.field;
                } else {
                    // The text stays as last reported until the app reports it again.
                    const WaylandField before = field_;
                    field_                    = event.field;
                    // A field that sent no text before its first done does not report text, as the protocol says.
                    if (!event.field.hasText || !before.hasText) {
                        field_.hasText = before.hasText;
                        field_.text    = before.text;
                        field_.cursor  = before.cursor;
                        field_.anchor  = before.anchor;
                    }
                }
                // There can be one grab only, so a new field releases the old one first.
                if (field_.active && (event.focusChanged || grab_ == nullptr))
                    grab();
                break;
            case ImEvent::Kind::Modifiers:
                depressed_ = event.depressed;
                latched_   = event.latched;
                locked_    = event.locked;
                group_     = event.group;
                if (state_ != nullptr)
                    xkb_state_update_mask(state_, depressed_, latched_, locked_, 0, 0, group_);
                sendModifiers(0);
                break;
            case ImEvent::Kind::RepeatInfo:
                repeatRate_  = event.rate;
                repeatDelay_ = event.delay;
                break;
            default: break;
        }
    }

    void InputMethod::grab() {
        if (grab_ != nullptr)
            zwp_input_method_keyboard_grab_v2_release(grab_);
        grab_ = zwp_input_method_v2_grab_keyboard(im_);
        zwp_input_method_keyboard_grab_v2_add_listener(grab_, &grabListener, this);
    }

    void InputMethod::ungrab(uint32_t time) {
        // Keys read along with the focus change belong to the old grab; the app gets the next ones
        // itself, so a press typed now would never see its release.
        events_.erase(
            std::remove_if(events_.begin(), events_.end(), [](const ImEvent& event) { return event.kind == ImEvent::Kind::Key || event.kind == ImEvent::Kind::Modifiers; }),
            events_.end());
        releasePressedKeys(time);
        letGoOfModifiers();
        if (grab_ != nullptr) {
            zwp_input_method_keyboard_grab_v2_release(grab_);
            grab_ = nullptr;
        }
    }

    void InputMethod::setKeymap(int fd, uint32_t format, uint32_t size) {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
            NGOSEN_WARN("Ignoring a keymap of format " + std::to_string(format) + ", size " + std::to_string(size));
            ::close(fd);
            return;
        }
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED) {
            keymapFailed("Cannot map the keymap from the compositor");
            return;
        }
        // The size counts the terminating NUL.
        std::string text(static_cast<const char*>(map), strnlen(static_cast<const char*>(map), size));
        munmap(map, size);
        if (text == keymapText_ && keymap_ != nullptr)
            return;
        xkb_keymap* keymap = xkb_keymap_new_from_string(context_, text.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap == nullptr) {
            keymapFailed("Cannot read the keymap from the compositor");
            return;
        }
        if (state_ != nullptr)
            xkb_state_unref(state_);
        if (keymap_ != nullptr)
            xkb_keymap_unref(keymap_);
        keymap_     = keymap;
        state_      = xkb_state_new(keymap_);
        keymapText_ = std::move(text);
        xkb_state_update_mask(state_, depressed_, latched_, locked_, 0, 0, group_);
        // The virtual keyboard types with the same keymap, set before any key it sends.
        const int copy = memoryFile(keymapText_);
        if (copy < 0) {
            keymapFailed("Cannot hand the keymap to the virtual keyboard");
            return;
        }
        zwp_virtual_keyboard_v1_keymap(vk_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, copy, static_cast<uint32_t>(keymapText_.size() + 1));
        ::close(copy);
        vkHasKeymap_ = true;
        NGOSEN_DEBUG("Keymap of " + std::to_string(keymapText_.size()) + " bytes");
    }

    void InputMethod::keymapFailed(const std::string& why) {
        NGOSEN_ERROR(why);
        // The grab would swallow every key with nothing to type them with.
        events_.push_back(ImEvent{ImEvent::Kind::KeymapFailed});
    }

    void InputMethod::letGoOfModifiers() {
        // Modifiers still held belong to the physical keyboard now; locks such as Caps Lock stay.
        depressed_ = latched_ = 0;
        if (vkHasKeymap_)
            zwp_virtual_keyboard_v1_modifiers(vk_, 0, 0, locked_, group_);
    }

    bool InputMethod::maySend() {
        if (!readEvents())
            return false;
        if (!field_.active || focusEventsSinceDone_ > 0)
            return false;
        for (const ImEvent& event : events_) {
            if (event.kind == ImEvent::Kind::Done && event.focusChanged)
                return false;
        }
        return true;
    }

    void InputMethod::commitString(const std::string& text) {
        zwp_input_method_v2_commit_string(im_, text.c_str());
    }

    void InputMethod::setPreedit(const std::string& text) {
        const auto end = static_cast<int32_t>(text.size());
        zwp_input_method_v2_set_preedit_string(im_, text.c_str(), end, end);
    }

    void InputMethod::deleteSurrounding(uint32_t beforeBytes, uint32_t afterBytes) {
        zwp_input_method_v2_delete_surrounding_text(im_, beforeBytes, afterBytes);
    }

    void InputMethod::commit() {
        zwp_input_method_v2_commit(im_, serial_);
        flush();
    }

    void InputMethod::sendModifiers(uint32_t extra) {
        if (vkHasKeymap_)
            zwp_virtual_keyboard_v1_modifiers(vk_, depressed_ | extra, latched_, locked_, group_);
    }

    void InputMethod::sendKey(uint32_t time, uint32_t code, bool pressed, uint32_t extraMods) {
        if (!vkHasKeymap_)
            return;
        const bool changesMods = extraMods != 0 && (depressed_ & extraMods) != extraMods;
        if (changesMods)
            sendModifiers(extraMods);
        zwp_virtual_keyboard_v1_key(vk_, time, code, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        if (changesMods)
            sendModifiers(0);
        if (pressed)
            pressed_[code] = time;
        else
            pressed_.erase(code);
        flush();
    }

    void InputMethod::releasePressedKeys(uint32_t time) {
        while (!pressed_.empty()) {
            const auto [code, pressedAt] = *pressed_.begin();
            sendKey(time != 0 ? time : pressedAt, code, false);
        }
    }

    uint32_t InputMethod::codeForSym(uint32_t sym) const {
        if (keymap_ == nullptr || state_ == nullptr)
            return 0;
        const xkb_layout_index_t layout = xkb_state_key_get_layout(state_, xkb_keymap_min_keycode(keymap_));
        for (xkb_keycode_t code = xkb_keymap_min_keycode(keymap_); code <= xkb_keymap_max_keycode(keymap_); ++code) {
            const xkb_keysym_t* syms  = nullptr;
            const int           count = xkb_keymap_key_get_syms_by_level(keymap_, code, layout == XKB_LAYOUT_INVALID ? 0 : layout, 0, &syms);
            for (int i = 0; i < count; ++i) {
                if (syms[i] == sym)
                    return code - 8;
            }
        }
        return 0;
    }

    uint32_t InputMethod::modIndexMask(const char* name) const {
        if (keymap_ == nullptr)
            return 0;
        const xkb_mod_index_t index = xkb_keymap_mod_get_index(keymap_, name);
        return index == XKB_MOD_INVALID ? 0 : 1U << index;
    }

    void InputMethod::shutdown() {
        if (display_ == nullptr)
            return;
        if (im_ != nullptr && !globalLost_) {
            releasePressedKeys(0);
            letGoOfModifiers();
        }
        if (grab_ != nullptr)
            zwp_input_method_keyboard_grab_v2_release(grab_);
        if (vk_ != nullptr)
            zwp_virtual_keyboard_v1_destroy(vk_);
        if (im_ != nullptr)
            zwp_input_method_v2_destroy(im_);
        if (toplevelManager_ != nullptr)
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(toplevelManager_));
        if (vkManager_ != nullptr)
            wl_proxy_destroy(reinterpret_cast<wl_proxy*>(vkManager_));
        if (manager_ != nullptr)
            zwp_input_method_manager_v2_destroy(manager_);
        if (seat_ != nullptr)
            wl_seat_destroy(seat_);
        if (registry_ != nullptr)
            wl_registry_destroy(registry_);
        grab_            = nullptr;
        vk_              = nullptr;
        im_              = nullptr;
        toplevelManager_ = nullptr;
        vkManager_       = nullptr;
        manager_         = nullptr;
        seat_            = nullptr;
        registry_        = nullptr;
        // A compositor drops what it has not read once the socket closes, and a release lost there
        // leaves the key held in the app.
        if (!globalLost_)
            wl_display_roundtrip(display_);
        wl_display_flush(display_);
        wl_display_disconnect(display_);
        display_ = nullptr;
    }

} // namespace ngosen
