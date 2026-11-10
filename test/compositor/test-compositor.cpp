/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "test-compositor.h"

#include "input-method-unstable-v2-server-protocol.h"
#include "virtual-keyboard-unstable-v1-server-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-server-protocol.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

namespace test {

    namespace {

        TestCompositor* of(wl_resource* resource) {
            return static_cast<TestCompositor*>(wl_resource_get_user_data(resource));
        }

        void destroyResource(wl_client*, wl_resource* resource) {
            wl_resource_destroy(resource);
        }

        // The seat is only an object to name; nothing asks it for devices.
        void                           seatGetDevice(wl_client*, wl_resource*, uint32_t) {}
        const struct wl_seat_interface seatImpl = {seatGetDevice, seatGetDevice, seatGetDevice, destroyResource};

        void                           bindSeat(wl_client* client, void* data, uint32_t version, uint32_t id) {
            wl_resource* resource = wl_resource_create(client, &wl_seat_interface, static_cast<int>(std::min(version, 5U)), id);
            wl_resource_set_implementation(resource, &seatImpl, data, nullptr);
            wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_KEYBOARD);
        }

        std::string keymapFor(const std::string& layout) {
            xkb_context*         context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            const xkb_rule_names names   = {"evdev", "pc105", layout.c_str(), "", ""};
            xkb_keymap*          keymap  = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
            std::string          text;
            if (keymap != nullptr) {
                char* string = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
                text         = string;
                free(string);
                xkb_keymap_unref(keymap);
            }
            xkb_context_unref(context);
            return text;
        }

        int memoryFile(const std::string& text) {
            const int fd = memfd_create("test-keymap", MFD_CLOEXEC);
            if (fd >= 0 && ::write(fd, text.c_str(), text.size() + 1) != static_cast<ssize_t>(text.size() + 1)) {
                ::close(fd);
                return -1;
            }
            return fd;
        }

        // Grab.
        void grabRelease(wl_client*, wl_resource* resource) {
            of(resource)->note("grab.release");
            wl_resource_destroy(resource);
        }
        const struct zwp_input_method_keyboard_grab_v2_interface grabImpl = {grabRelease};
        void                                                     grabDestroyed(wl_resource* resource) {
            TestCompositor* compositor = of(resource);
            if (compositor->grab_ == resource)
                compositor->grab_ = nullptr;
        }

        // Input method.
        void imCommitString(wl_client*, wl_resource* resource, const char* text) {
            of(resource)->note(std::string("im.commit_string ") + text);
        }
        void imSetPreedit(wl_client*, wl_resource* resource, const char* text, int32_t begin, int32_t end) {
            of(resource)->note(std::string("im.preedit ") + text + " " + std::to_string(begin) + " " + std::to_string(end));
        }
        void imDelete(wl_client*, wl_resource* resource, uint32_t before, uint32_t after) {
            of(resource)->note("im.delete " + std::to_string(before) + " " + std::to_string(after));
        }
        void imCommit(wl_client*, wl_resource* resource, uint32_t serial) {
            of(resource)->note("im.commit " + std::to_string(serial));
        }
        void imPopup(wl_client*, wl_resource*, uint32_t, wl_resource*) {}
        void imGrab(wl_client* client, wl_resource* resource, uint32_t id) {
            TestCompositor* compositor = of(resource);
            compositor->note("grab.take");
            wl_resource* grab = wl_resource_create(client, &zwp_input_method_keyboard_grab_v2_interface, 1, id);
            wl_resource_set_implementation(grab, &grabImpl, compositor, grabDestroyed);
            if (compositor->grab_ != nullptr)
                compositor->note("error: a second grab while one is held");
            compositor->grab_ = grab;
            if (compositor->options_.keymapOnGrab) {
                compositor->sendKeymap(compositor->options_.layout);
                compositor->repeatInfo(25, 600);
                compositor->modifiers(0, 0, 0, 0);
            }
        }
        void imDestroy(wl_client*, wl_resource* resource) {
            of(resource)->note("im.destroy");
            wl_resource_destroy(resource);
        }
        const struct zwp_input_method_v2_interface imImpl = {imCommitString, imSetPreedit, imDelete, imCommit, imPopup, imGrab, imDestroy};
        void                                       imDestroyed(wl_resource* resource) {
            TestCompositor* compositor = of(resource);
            if (compositor->im_ == resource)
                compositor->im_ = nullptr;
        }

        void managerGetInputMethod(wl_client* client, wl_resource* resource, wl_resource* /*seat*/, uint32_t id) {
            TestCompositor* compositor = of(resource);
            wl_resource*    im         = wl_resource_create(client, &zwp_input_method_v2_interface, 1, id);
            wl_resource_set_implementation(im, &imImpl, compositor, imDestroyed);
            compositor->im_ = im;
            compositor->note("im.create");
        }
        const struct zwp_input_method_manager_v2_interface managerImpl = {managerGetInputMethod, destroyResource};
        void                                               bindManager(wl_client* client, void* data, uint32_t /*version*/, uint32_t id) {
            wl_resource* resource = wl_resource_create(client, &zwp_input_method_manager_v2_interface, 1, id);
            wl_resource_set_implementation(resource, &managerImpl, data, nullptr);
        }

        // Virtual keyboard.
        void vkKeymap(wl_client*, wl_resource* resource, uint32_t format, int32_t fd, uint32_t size) {
            ::close(fd);
            of(resource)->note("vk.keymap " + std::to_string(format) + " " + std::to_string(size));
        }
        void vkKey(wl_client*, wl_resource* resource, uint32_t /*time*/, uint32_t key, uint32_t state) {
            of(resource)->note("vk.key " + std::to_string(key) + " " + std::to_string(state));
        }
        void vkModifiers(wl_client*, wl_resource* resource, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
            of(resource)->note("vk.mods " + std::to_string(depressed) + " " + std::to_string(latched) + " " + std::to_string(locked) + " " + std::to_string(group));
        }
        void vkDestroy(wl_client*, wl_resource* resource) {
            of(resource)->note("vk.destroy");
            wl_resource_destroy(resource);
        }
        const struct zwp_virtual_keyboard_v1_interface vkImpl = {vkKeymap, vkKey, vkModifiers, vkDestroy};
        void                                           vkDestroyed(wl_resource* resource) {
            auto& vks = of(resource)->vks_;
            vks.erase(std::remove(vks.begin(), vks.end(), resource), vks.end());
        }
        void vkCreate(wl_client* client, wl_resource* resource, wl_resource* /*seat*/, uint32_t id) {
            TestCompositor* compositor = of(resource);
            wl_resource*    vk         = wl_resource_create(client, &zwp_virtual_keyboard_v1_interface, 1, id);
            wl_resource_set_implementation(vk, &vkImpl, compositor, vkDestroyed);
            compositor->vks_.push_back(vk);
            compositor->note("vk.create");
        }
        const struct zwp_virtual_keyboard_manager_v1_interface vkManagerImpl = {vkCreate};
        void                                                   bindVkManager(wl_client* client, void* data, uint32_t /*version*/, uint32_t id) {
            wl_resource* resource = wl_resource_create(client, &zwp_virtual_keyboard_manager_v1_interface, 1, id);
            wl_resource_set_implementation(resource, &vkManagerImpl, data, nullptr);
        }

        // Foreign toplevels.
        void                                                   handleNoArg(wl_client*, wl_resource*) {}
        void                                                   handleSeat(wl_client*, wl_resource*, wl_resource*) {}
        void                                                   handleRect(wl_client*, wl_resource*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
        const struct zwlr_foreign_toplevel_handle_v1_interface handleImpl = {handleNoArg, handleNoArg, handleNoArg,     handleNoArg, handleSeat,
                                                                             handleNoArg, handleRect,  destroyResource, handleSeat,  handleNoArg};
        void                                                   handleDestroyed(wl_resource* resource) {
            for (auto& toplevel : of(resource)->toplevels_) {
                auto& handles = toplevel.handles;
                handles.erase(std::remove(handles.begin(), handles.end(), resource), handles.end());
            }
        }
        void toplevelStop(wl_client*, wl_resource* resource) {
            zwlr_foreign_toplevel_manager_v1_send_finished(resource);
        }
        const struct zwlr_foreign_toplevel_manager_v1_interface toplevelManagerImpl = {toplevelStop};
        void                                                    toplevelManagerDestroyed(wl_resource* resource) {
            auto& managers = of(resource)->toplevelManagers_;
            managers.erase(std::remove(managers.begin(), managers.end(), resource), managers.end());
        }
        void bindToplevelManager(wl_client* client, void* data, uint32_t version, uint32_t id) {
            auto*        compositor = static_cast<TestCompositor*>(data);
            wl_resource* resource   = wl_resource_create(client, &zwlr_foreign_toplevel_manager_v1_interface, static_cast<int>(std::min(version, 3U)), id);
            wl_resource_set_implementation(resource, &toplevelManagerImpl, data, toplevelManagerDestroyed);
            compositor->toplevelManagers_.push_back(resource);
            for (auto& toplevel : compositor->toplevels_)
                compositor->sendToplevel(resource, toplevel);
        }

    } // namespace

    TestCompositor::TestCompositor(Options options) : options_(std::move(options)) {
        char pattern[] = "/tmp/wl-ngosen-runtime-XXXXXX";
        runtimeDir_    = mkdtemp(pattern);
        setenv("XDG_RUNTIME_DIR", runtimeDir_.c_str(), 1);
        display_ = wl_display_create();
        socket_  = wl_display_add_socket_auto(display_);
        wl_global_create(display_, &wl_seat_interface, 5, this, bindSeat);
        if (options_.inputMethod)
            imGlobal_ = wl_global_create(display_, &zwp_input_method_manager_v2_interface, 1, this, bindManager);
        if (options_.virtualKeyboard)
            wl_global_create(display_, &zwp_virtual_keyboard_manager_v1_interface, 1, this, bindVkManager);
        if (options_.toplevels)
            wl_global_create(display_, &zwlr_foreign_toplevel_manager_v1_interface, 3, this, bindToplevelManager);
    }

    TestCompositor::~TestCompositor() {
        stop();
        wl_display_destroy_clients(display_);
        wl_display_destroy(display_);
        std::string command = "rm -rf '" + runtimeDir_ + "'";
        if (std::system(command.c_str()) != 0)
            std::cerr << "cannot remove " << runtimeDir_ << '\n';
    }

    void TestCompositor::start(const std::string& program, const std::string& home, const std::vector<std::string>& env) {
        pid_ = fork();
        if (pid_ != 0)
            return;
        setenv("WAYLAND_DISPLAY", socket_.c_str(), 1);
        setenv("HOME", home.c_str(), 1);
        setenv("XDG_CONFIG_HOME", (home + "/.config").c_str(), 1);
        setenv("XDG_DATA_HOME", (home + "/.local/share").c_str(), 1);
        setenv("XDG_STATE_HOME", (home + "/.local/state").c_str(), 1);
        setenv("NGOSEN_LOG_FILE", (home + "/daemon.log").c_str(), 1);
        setenv("NGOSEN_DEBUG", "1", 1);
        for (const auto& entry : env) {
            const auto eq = entry.find('=');
            setenv(entry.substr(0, eq).c_str(), entry.substr(eq + 1).c_str(), 1);
        }
        execl(program.c_str(), program.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    int TestCompositor::waitExit(int waitMs) {
        if (pid_ <= 0)
            return -1;
        int        status = 0;
        const auto end    = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
        while (std::chrono::steady_clock::now() < end) {
            if (waitpid(pid_, &status, WNOHANG) == pid_) {
                pid_ = -1;
                // What the daemon sent just before it exited is still in the socket.
                run(0);
                run(0);
                return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            run(10);
        }
        return -1;
    }

    void TestCompositor::stop() {
        if (pid_ <= 0)
            return;
        kill(pid_, SIGTERM);
        if (waitExit(2000) < 0) {
            kill(pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
    }

    void TestCompositor::run(int ms) {
        wl_display_flush_clients(display_);
        wl_event_loop_dispatch(wl_display_get_event_loop(display_), ms);
        wl_display_flush_clients(display_);
    }

    bool TestCompositor::waitFor(const std::function<bool()>& condition, int timeoutMs) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!condition()) {
            if (std::chrono::steady_clock::now() >= end)
                return false;
            run(10);
        }
        return true;
    }

    void TestCompositor::activate() {
        zwp_input_method_v2_send_activate(im_);
    }
    void TestCompositor::deactivate() {
        zwp_input_method_v2_send_deactivate(im_);
    }
    void TestCompositor::surrounding(const std::string& text, uint32_t cursor, uint32_t anchor) {
        zwp_input_method_v2_send_surrounding_text(im_, text.c_str(), cursor, anchor);
    }
    void TestCompositor::cause(uint32_t cause) {
        zwp_input_method_v2_send_text_change_cause(im_, cause);
    }
    void TestCompositor::contentType(uint32_t hint, uint32_t purpose) {
        zwp_input_method_v2_send_content_type(im_, hint, purpose);
    }
    void TestCompositor::done() {
        ++serial_;
        zwp_input_method_v2_send_done(im_);
        run(0);
    }
    void TestCompositor::unavailable() {
        zwp_input_method_v2_send_unavailable(im_);
        run(0);
    }
    void TestCompositor::removeInputMethodGlobal() {
        wl_global_destroy(imGlobal_);
        imGlobal_ = nullptr;
        run(0);
    }

    void TestCompositor::sendKeymap(const std::string& layout) {
        // Without a grab a compositor sends keys to the app, not to the input method.
        if (grab_ == nullptr)
            return;
        const std::string text = keymapFor(layout);
        const int         fd   = memoryFile(text);
        zwp_input_method_keyboard_grab_v2_send_keymap(grab_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, static_cast<uint32_t>(text.size() + 1));
        ::close(fd);
    }
    void TestCompositor::sendEmptyKeymap() {
        // Without a grab a compositor sends keys to the app, not to the input method.
        if (grab_ == nullptr)
            return;
        const int fd = memoryFile("");
        zwp_input_method_keyboard_grab_v2_send_keymap(grab_, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, 0);
        ::close(fd);
    }
    void TestCompositor::key(uint32_t code, bool pressed) {
        // Without a grab a compositor sends keys to the app, not to the input method.
        if (grab_ == nullptr)
            return;
        zwp_input_method_keyboard_grab_v2_send_key(grab_, ++serial_, time_ += 10, code, pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
        run(0);
    }
    void TestCompositor::tap(uint32_t code) {
        key(code, true);
        key(code, false);
    }
    void TestCompositor::modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
        // Without a grab a compositor sends keys to the app, not to the input method.
        if (grab_ == nullptr)
            return;
        zwp_input_method_keyboard_grab_v2_send_modifiers(grab_, ++serial_, depressed, latched, locked, group);
    }
    void TestCompositor::repeatInfo(int32_t rate, int32_t delay) {
        // Without a grab a compositor sends keys to the app, not to the input method.
        if (grab_ == nullptr)
            return;
        zwp_input_method_keyboard_grab_v2_send_repeat_info(grab_, rate, delay);
    }

    void TestCompositor::sendToplevelState(Toplevel& toplevel) {
        for (wl_resource* handle : toplevel.handles) {
            wl_array states;
            wl_array_init(&states);
            if (toplevel.activated)
                *static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))) = ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
            zwlr_foreign_toplevel_handle_v1_send_state(handle, &states);
            wl_array_release(&states);
            zwlr_foreign_toplevel_handle_v1_send_done(handle);
        }
    }

    void TestCompositor::sendToplevel(wl_resource* manager, Toplevel& toplevel) {
        wl_resource* handle = wl_resource_create(wl_resource_get_client(manager), &zwlr_foreign_toplevel_handle_v1_interface, wl_resource_get_version(manager), 0);
        wl_resource_set_implementation(handle, &handleImpl, this, handleDestroyed);
        zwlr_foreign_toplevel_manager_v1_send_toplevel(manager, handle);
        zwlr_foreign_toplevel_handle_v1_send_app_id(handle, toplevel.appId.c_str());
        zwlr_foreign_toplevel_handle_v1_send_title(handle, toplevel.appId.c_str());
        toplevel.handles.push_back(handle);
    }

    void TestCompositor::openToplevel(const std::string& appId, bool activated) {
        if (activated) {
            for (auto& other : toplevels_) {
                if (other.activated) {
                    other.activated = false;
                    sendToplevelState(other);
                }
            }
        }
        toplevels_.push_back({appId, activated, {}});
        for (wl_resource* manager : toplevelManagers_)
            sendToplevel(manager, toplevels_.back());
        sendToplevelState(toplevels_.back());
        run(0);
    }

    void TestCompositor::activateToplevel(const std::string& appId) {
        for (auto& toplevel : toplevels_) {
            const bool activated = toplevel.appId == appId;
            if (toplevel.activated != activated) {
                toplevel.activated = activated;
                sendToplevelState(toplevel);
            }
        }
        run(0);
    }

    bool TestCompositor::barrier() {
        const std::string up     = "vk.key " + std::to_string(barrierCode_) + " 0";
        const std::size_t before = count(up);
        tap(barrierCode_);
        return waitFor([&] { return count(up) > before; });
    }

    void TestCompositor::note(const std::string& line) {
        log_.push_back(line);
    }

    bool TestCompositor::logged(const std::string& line) const {
        return std::find(log_.begin(), log_.end(), line) != log_.end();
    }

    std::size_t TestCompositor::count(const std::string& prefix) const {
        return static_cast<std::size_t>(std::count_if(log_.begin(), log_.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; }));
    }

    std::vector<std::string> TestCompositor::lines(const std::string& prefix) const {
        std::vector<std::string> out;
        for (const auto& line : log_) {
            if (line.rfind(prefix, 0) == 0)
                out.push_back(line);
        }
        return out;
    }

    void TestCompositor::dump() const {
        for (const auto& line : log_)
            std::cout << "    " << line << '\n';
    }

} // namespace test
