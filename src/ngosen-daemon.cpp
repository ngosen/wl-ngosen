/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-daemon.h"

#include "ngosen-ibus-config.h"
#include "ngosen-ibus-resources.h"
#include "ngosen-input-method.h"
#include "ngosen-log.h"
#include "ngosen-masks.h"
#include "ngosen-globals.h"
#include "ngosen-state.h"
#include "ngosen-toggle-key.h"
#include "ngosen-wayland-host.h"
#include "ngosen-wayland-key.h"

#include <glib-unix.h>
#include <glib.h>
#include <signal.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {

        struct Daemon {
            InputMethod      im;
            IBusResources    resources{SettingsPaths::fromEnvironment()};
            WaylandResources typing{resources};
            WaylandHost*     host = nullptr;
            // After im, so it goes first: its last requests still need the connection.
            std::unique_ptr<TypingState> state;
            ToggleKey                    toggle;
            unsigned                     chordsGeneration = ~0U;
            // What the toggle key last chose; shared with the IBus edition through its marker file.
            bool       englishOn = readEnglishOn(resources.paths());
            GMainLoop* loop      = nullptr;
            int        status    = 0;

            void       start() {
                auto wayland     = std::make_unique<WaylandHost>(im);
                host             = wayland.get();
                host->afterTimer = [this] {
                    host->flush();
                    im.flush();
                };
                applyMode();
                state = std::make_unique<TypingState>(&typing, std::move(wayland));
                readToggleKeys();
            }

            // Preedit for Vietnamese until the Sen mode comes to this edition.
            void applyMode() {
                realMode = englishOn ? Mode::Off : Mode::Preedit;
            }

            // The word ends in the mode it was typed in, and the core drops what it held for it.
            void switchMode() {
                state->commitBuffer();
                state->resetAfterAppCommit(false);
                applyMode();
            }

            void readToggleKeys() {
                if (chordsGeneration == resources.generation())
                    return;
                std::vector<KeyChord> chords;
                for (const auto& key : resources.settings().toggleKeys) {
                    if (const auto chord = parseToggleKey(key))
                        chords.push_back(*chord);
                }
                toggle.setChords(std::move(chords));
                chordsGeneration = resources.generation();
            }

            // Only between words: a new engine would drop the word being typed.
            void applyNewSettings() {
                if (host->hasPreedit() || state->holdsWord())
                    return;
                if (resources.takeEnglishChange()) {
                    if (const bool english = readEnglishOn(resources.paths()); english != englishOn) {
                        englishOn = english;
                        switchMode();
                    }
                }
                const unsigned before = resources.generation();
                resources.reloadIfChanged();
                if (resources.generation() != before)
                    state->setEngine();
                readToggleKeys();
            }

            void toggleLanguage() {
                englishOn = !englishOn;
                if (!saveEnglishOn(resources.paths(), englishOn))
                    NGOSEN_WARN("Cannot save the language");
                NGOSEN_INFO(std::string("Toggle key: ") + (englishOn ? "English" : "Vietnamese"));
                switchMode();
            }

            // As the IBus edition's focus-out: what the core held for the field is dropped, since
            // nothing may reach it now, and above all not the next field.
            void focusOut() {
                toggle.reset();
                state->resetAfterAppCommit(true);
                state->forgetKeysWhileOff();
                host->dropPending();
            }

            void focusIn() {
                applyNewSettings();
                applyMode();
            }

            // A change the app made itself, as on a click, is left to the core as fcitx5's
            // input-method-v2 frontend leaves it: GTK 4 keeps showing its preedit after such a
            // change, and resetting here would send a commit without it, which wipes the word.
            void fieldChanged() {
                state->surroundingUpdated();
            }

            void forward(const ImEvent& event) {
                im.sendKey(event.time, event.code, event.pressed);
            }

            void handleKey(const ImEvent& event) {
                if (!im.field().active)
                    return;
                applyNewSettings();
                xkb_state*     xkb  = im.xkbState();
                const uint32_t sym  = xkb != nullptr ? xkb_state_key_get_one_sym(xkb, event.code + 8) : XKB_KEY_NoSymbol;
                const uint32_t mods = xStateOf(xkb) | (event.pressed ? 0 : ReleaseMask);
                // A password is sent as typed, the way fcitx5 turns input methods off in password fields.
                if (host->takesPassword()) {
                    toggle.reset();
                    forward(event);
                    return;
                }
                const ToggleKey::Result result = toggle.feed(sym, mods, event.time);
                if (result.toggle)
                    toggleLanguage();
                if (result.swallow) {
                    host->flush();
                    return;
                }
                host->setKeyTime(event.time);
                WaylandKeyPress key(sym, event.code, mods, event.time);
                state->keyEvent(key);
                // A key that goes on goes after what the core committed for it.
                host->flush();
                if (key.accepted())
                    return;
                if (const uint32_t replaced = key.replacedSym()) {
                    host->forwardKeyPress(replaced, key.states(), key.code(), !event.pressed, event.time);
                    return;
                }
                forward(event);
            }

            void handle(const ImEvent& event) {
                const bool wasActive = im.field().active;
                im.apply(event);
                if (event.kind == ImEvent::Kind::Done) {
                    if (event.focusChanged && wasActive)
                        focusOut();
                    if (event.focusChanged && im.field().active)
                        focusIn();
                    else if (!event.focusChanged && im.field().active && event.field.hasText)
                        fieldChanged();
                    host->flush();
                }
                switch (event.kind) {
                    case ImEvent::Kind::Key: handleKey(event); break;
                    case ImEvent::Kind::Unavailable:
                        NGOSEN_WARN("The compositor gave the input method to another program, or the seat went away");
                        status = 1;
                        g_main_loop_quit(loop);
                        break;
                    case ImEvent::Kind::KeymapFailed:
                        status = 1;
                        g_main_loop_quit(loop);
                        break;
                    default: break;
                }
            }

            void drain() {
                while (auto event = im.takeEvent()) {
                    handle(*event);
                    if (!g_main_loop_is_running(loop))
                        return;
                }
                im.flush();
            }
        };

        // Reads the Wayland socket from the GLib main loop, so file monitors and D-Bus share one thread.
        struct WaylandSource {
            GSource  source;
            Daemon*  daemon;
            gpointer tag;
        };

        gboolean prepareSource(GSource* base, gint* timeout) {
            auto* source = reinterpret_cast<WaylandSource*>(base);
            *timeout     = -1;
            source->daemon->im.flush();
            return source->daemon->im.hasEvents();
        }

        gboolean checkSource(GSource* base) {
            auto* source = reinterpret_cast<WaylandSource*>(base);
            return source->daemon->im.hasEvents() || (g_source_query_unix_fd(base, source->tag) & (G_IO_IN | G_IO_ERR | G_IO_HUP)) != 0;
        }

        gboolean dispatchSource(GSource* base, GSourceFunc /*callback*/, gpointer /*data*/) {
            auto*              source = reinterpret_cast<WaylandSource*>(base);
            Daemon&            daemon = *source->daemon;
            const GIOCondition seen   = g_source_query_unix_fd(base, source->tag);
            if ((seen & (G_IO_ERR | G_IO_HUP)) != 0 || !daemon.im.readEvents()) {
                NGOSEN_WARN("Lost the connection to the compositor");
                daemon.status = 1;
                g_main_loop_quit(daemon.loop);
                return G_SOURCE_REMOVE;
            }
            daemon.drain();
            return G_SOURCE_CONTINUE;
        }

        GSourceFuncs sourceFuncs = {prepareSource, checkSource, dispatchSource, nullptr, nullptr, nullptr};

        gboolean     onSignal(gpointer data) {
            g_main_loop_quit(static_cast<Daemon*>(data)->loop);
            return G_SOURCE_CONTINUE;
        }

    } // namespace

    int runDaemon() {
        Daemon daemon;
        switch (daemon.im.connect()) {
            case InputMethod::Connection::NotOffered: return 0;
            case InputMethod::Connection::Failed: return 1;
            case InputMethod::Connection::Ready: break;
        }
        daemon.start();
        daemon.loop    = g_main_loop_new(nullptr, FALSE);
        auto* source   = reinterpret_cast<WaylandSource*>(g_source_new(&sourceFuncs, sizeof(WaylandSource)));
        source->daemon = &daemon;
        source->tag    = g_source_add_unix_fd(&source->source, daemon.im.fd(), static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP));
        g_source_attach(&source->source, nullptr);
        g_unix_signal_add(SIGINT, onSignal, &daemon);
        g_unix_signal_add(SIGTERM, onSignal, &daemon);
        NGOSEN_INFO("Typing on Wayland");
        g_main_loop_run(daemon.loop);
        g_source_destroy(&source->source);
        g_source_unref(&source->source);
        daemon.im.shutdown();
        g_main_loop_unref(daemon.loop);
        return daemon.status;
    }

} // namespace ngosen
