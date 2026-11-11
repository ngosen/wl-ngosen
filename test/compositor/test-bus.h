/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <string>
#include <vector>

typedef struct _GTestDBus       GTestDBus;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GVariant        GVariant;

namespace test {

    // A session bus of the test's own, never the desktop's, with a notification server on it and,
    // once started, the watcher a bar runs for tray icons. It also plays the bar: it reads the icon
    // and its menu and clicks them.
    class TestBus {
      public:
        TestBus();
        ~TestBus();
        TestBus(const TestBus&)            = delete;
        TestBus& operator=(const TestBus&) = delete;

        bool     up() const {
            return connection_ != nullptr;
        }
        std::string address() const;
        void        startWatcher();
        // Stops the bus under the daemon, as a restarted dbus-daemon would.
        void down();
        // Runs what arrived on the bus; call it while waiting.
        void pump();

        // Bus names the tray icons gave the watcher.
        const std::vector<std::string>& registered() const {
            return registered_;
        }
        // How many times a menu said its layout changed.
        int layoutUpdates() const {
            return layoutUpdates_;
        }
        void noteLayoutUpdate() {
            ++layoutUpdates_;
        }
        // Summaries of the notifications shown.
        const std::vector<std::string>& notices() const {
            return notices_;
        }

        std::string iconName(const std::string& item);
        void        activate(const std::string& item);
        // The id of the first menu item with this label, or -1.
        int menuId(const std::string& item, const std::string& label);
        // toggle-state of that item: 1 checked, 0 not, -1 no such item.
        int  toggleState(const std::string& item, const std::string& label);
        void click(const std::string& item, int id);

        // Called from the bus handlers.
        void noteRegistered(const std::string& name);
        void noteNotice(const std::string& summary);

      private:
        GVariant*                call(const std::string& item, const char* path, const char* interface, const char* method, GVariant* parameters);
        GVariant*                menuLayout(const std::string& item);

        GTestDBus*               bus_        = nullptr;
        GDBusConnection*         connection_ = nullptr;
        unsigned                 watcherOwn_ = 0, watcherObject_ = 0, noticeOwn_ = 0, noticeObject_ = 0, layoutSignal_ = 0;
        int                      layoutUpdates_ = 0;
        std::vector<std::string> registered_;
        std::vector<std::string> notices_;
    };

} // namespace test
