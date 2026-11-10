/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

struct wl_display;
struct wl_resource;
struct wl_global;
struct wl_event_source;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace test {

    // A compositor with only what an input method talks to: a seat, input-method-v2, the virtual
    // keyboard and foreign toplevels. It writes down every request the daemon makes, one line each.
    class TestCompositor {
      public:
        struct Options {
            bool inputMethod     = true;
            bool virtualKeyboard = true;
            bool toplevels       = true;
            // Sent when the daemon takes the grab, as compositors do.
            bool        keymapOnGrab = true;
            std::string layout       = "us";
        };

        explicit TestCompositor(Options options);
        ~TestCompositor();
        TestCompositor(const TestCompositor&)            = delete;
        TestCompositor& operator=(const TestCompositor&) = delete;

        // Starts wl-ngosen on this compositor's socket with a home of its own.
        void start(const std::string& program, const std::string& home, const std::vector<std::string>& env = {});
        // The daemon's exit status, or -1 if it is still running after waitMs.
        int  waitExit(int waitMs);
        void stop();
        // Stops the daemon while this compositor reads nothing for busyMs, as a busy compositor does.
        void  stopWhileBusy(int busyMs);
        pid_t pid() const {
            return pid_;
        }

        // Runs the event loop until the condition holds, false after timeoutMs.
        bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000);
        // Serves the daemon for the whole time; run() returns after the first event.
        void runFor(int ms);
        void run(int ms);
        bool hasClient() const {
            return im_ != nullptr;
        }

        // Events of zwp_input_method_v2.
        void activate();
        void deactivate();
        void surrounding(const std::string& text, uint32_t cursor, uint32_t anchor);
        void cause(uint32_t cause);
        void contentType(uint32_t hint, uint32_t purpose);
        // send false keeps the event queued, so the daemon reads it with what follows.
        void done(bool send = true);
        // done events sent so far, which a commit must name as its serial.
        uint32_t dones() const {
            return dones_;
        }
        void unavailable();

        // A text field that behaves as an app's: it applies what the input method commits and the
        // keys the virtual keyboard sends, and reports its text back if it reports surrounding text.
        // Opening it activates the input method. Offsets are bytes, as on the wire.
        void openField(const std::string& text, bool reports = true);
        // Reports arrive this late, as from an app that answers after a round trip of its own.
        void setReplyDelay(int ms) {
            replyDelayMs_ = ms;
        }
        // The app moves the cursor itself, as on a click, and says so with cause other.
        void               moveCursor(uint32_t cursor);
        const std::string& fieldText() const {
            return field_.text;
        }
        const std::string& fieldPreedit() const {
            return field_.preedit;
        }
        void removeInputMethodGlobal();

        // Events of the keyboard grab. Codes are evdev codes.
        bool grabbed() const {
            return grab_ != nullptr;
        }
        void sendKeymap(const std::string& layout);
        void sendEmptyKeymap();
        void sendBrokenKeymap();
        void key(uint32_t code, bool pressed, bool send = true);
        void tap(uint32_t code, bool send = true);
        void modifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
        void repeatInfo(int32_t rate, int32_t delay);

        // Foreign toplevels.
        void openToplevel(const std::string& appId, bool activated);
        void activateToplevel(const std::string& appId);
        void closeToplevel(const std::string& appId);

        // A key nothing types, sent through and back: everything the daemon got before it is handled.
        bool                            barrier();

        const std::vector<std::string>& log() const {
            return log_;
        }
        void clearLog() {
            log_.clear();
        }
        bool        logged(const std::string& line) const;
        std::size_t count(const std::string& prefix) const;
        // Lines starting with prefix, in order.
        std::vector<std::string> lines(const std::string& prefix) const;
        void                     dump() const;

        // Called from the protocol handlers.
        void                      note(const std::string& line);

        Options                   options_;
        wl_display*               display_ = nullptr;
        std::string               socket_;
        std::string               runtimeDir_;
        wl_resource*              im_   = nullptr;
        wl_resource*              grab_ = nullptr;
        std::vector<wl_resource*> vks_;
        std::vector<wl_resource*> toplevelManagers_;
        struct Toplevel {
            std::string               appId;
            bool                      activated = false;
            std::vector<wl_resource*> handles;
        };
        std::vector<Toplevel>    toplevels_;
        wl_global*               imGlobal_    = nullptr;
        uint32_t                 serial_      = 0;
        uint32_t                 dones_       = 0;
        uint32_t                 time_        = 1000;
        uint32_t                 barrierCode_ = 88; // F12
        pid_t                    pid_         = -1;
        std::vector<std::string> log_;

        void                     sendToplevel(wl_resource* manager, Toplevel& toplevel);
        void                     sendToplevelState(Toplevel& toplevel);

        struct Field {
            bool        open    = false;
            bool        reports = true;
            std::string text;
            uint32_t    cursor = 0;
            std::string preedit;
            // What the input method asked for since its last commit.
            uint32_t    deleteBefore = 0, deleteAfter = 0;
            std::string commitText;
            std::string nextPreedit;
        };
        Field        field_;
        int          replyDelayMs_ = 0;
        xkb_state*   vkState_      = nullptr;
        xkb_keymap*  vkKeymap_     = nullptr;
        xkb_context* xkb_          = nullptr;
        struct LateReport {
            std::string                           text;
            uint32_t                              cursor;
            uint32_t                              cause;
            std::chrono::steady_clock::time_point due;
        };
        // One timer for every pending report, so the destructor can free what is still waiting.
        std::deque<LateReport> lateReports_;
        wl_event_source*       lateTimer_ = nullptr;

        void                   sendLateReports();
        void                   fieldCommit();
        void                   fieldKey(uint32_t code, bool pressed);
        void                   fieldModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
        void                   reportField(uint32_t cause);
        void                   sendReport(const std::string& text, uint32_t cursor, uint32_t cause);
    };

} // namespace test
