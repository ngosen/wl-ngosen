/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-daemon.h"

#include "ngosen-ibus-app-modes.h"
#include "ngosen-ibus-config.h"
#include "ngosen-ibus-resources.h"
#include "ngosen-keysym.h"
#include "ngosen-input-method.h"
#include "ngosen-log.h"
#include "ngosen-ibus-i18n.h"
#include "ngosen-ibus-typing-log.h"
#include "ngosen-masks.h"
#include "ngosen-globals.h"
#include "ngosen-notice.h"
#include "ngosen-state.h"
#include "ngosen-toggle-key.h"
#include "ngosen-tray.h"
#include "ngosen-wayland-host.h"
#include "ngosen-wayland-key.h"

#include <algorithm>
#include <glib-unix.h>
#include <glib.h>
#include <signal.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {

        void openSettingsWindow() {
            // Tests point this at a stand-in, so no window opens on the desktop running them.
            const char* program = g_getenv("NGOSEN_WL_SETTINGS");
            gchar*      argv[]  = {const_cast<gchar*>(program != nullptr ? program : NGOSEN_SETTINGS_EXEC), nullptr}; //NOLINT
            GError*     error   = nullptr;
            if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &error)) {
                NGOSEN_WARN(std::string("Cannot open the settings window: ") + error->message);
                g_error_free(error);
            }
        }

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
            bool        englishOn = readEnglishOn(resources.paths());
            AppModes    appModes{resources.paths().appModesFile()};
            AppSwitches appSwitches{resources.paths().appSwitchesFile()};
            // The app the mode was picked for.
            std::string modeApp;
            GMainLoop*  loop   = nullptr;
            int         status = 0;
            // Null without a session bus.
            std::unique_ptr<Tray> tray;
            Notice                notice;
            // The held key. The grab sends a key once; as in fcitx5, the input method repeats it.
            struct {
                uint32_t code   = 0;
                uint32_t time   = 0;
                guint    source = 0;
            } repeat;

            void start() {
                auto wayland     = std::make_unique<WaylandHost>(im);
                host             = wayland.get();
                host->afterTimer = [this] {
                    host->flush();
                    im.flush();
                };
                applyMode();
                state = std::make_unique<TypingState>(&typing, std::move(wayland));
                readToggleKeys();
                tray = std::make_unique<Tray>(
                    Tray::Handlers{[this] { fromTray([this] { toggleLanguage(); }); }, [this](const std::string& action) { fromTray([this, action] { trayClick(action); }); }});
                if (!tray->start())
                    tray.reset();
                // The settings window and the IBus edition write the files while no key comes.
                resources.onChange([this] { showTray(); });
                showTray();
            }

            void showTray() {
                const IBusSettings saved = resources.savedSettings();
                setUiEnglish(saved.uiLanguage == "English");
                if (tray)
                    tray->show(trayMenu(saved, {englishOn, modeApp, realMode}), englishOn);
            }

            // A click can end the word being typed, which goes out at once, as after a key.
            void fromTray(const std::function<void()>& action) {
                action();
                host->flush();
                im.flush();
                showTray();
            }

            void trayClick(const std::string& action) {
                if (action == LanguageAction) {
                    toggleLanguage();
                    return;
                }
                if (action == SaveTypingLogAction) {
                    saveTypingLog(resources.recorder());
                    return;
                }
                if (action == SettingsAction) {
                    openSettingsWindow();
                    return;
                }
                if (const auto mode = modeOfAction(action)) {
                    pickMode(*mode);
                    return;
                }
                std::string  key, value;
                IBusSettings saved;
                if (settingForClick(action, resources.savedSettings(), key, value) && !resources.saveSetting(key, value, saved))
                    NGOSEN_WARN("Cannot save " + key);
            }

            // As the IBus edition's mode menu: kept for the app in use; an app without a name changes
            // the shared language and mode instead.
            void pickMode(MenuMode mode) {
                const std::string app = currentApp();
                if (!app.empty()) {
                    const Mode shared = resources.settings().directTyping ? Mode::Sen : Mode::Preedit;
                    if (!appModes.set(app, keptAppMode(mode, englishOn, shared)) || !appSwitches.set(app, std::nullopt))
                        NGOSEN_WARN("Cannot save the typing mode for " + app);
                    switchMode();
                    return;
                }
                if (const bool english = mode == MenuMode::Off; english != englishOn) {
                    englishOn = english;
                    if (!saveEnglishOn(resources.paths(), englishOn))
                        NGOSEN_WARN("Cannot save the language");
                }
                IBusSettings saved;
                const bool   direct = mode == MenuMode::Sen;
                // Applied at once, since the next key may come before the file monitor reports the write.
                if (mode != MenuMode::Off && resources.settings().directTyping != direct && resources.saveSetting("DirectTyping", direct ? "True" : "False", saved))
                    resources.noticeChange();
                switchMode();
            }

            std::string currentApp() const {
                return modeAppName(im.activeProgram());
            }

            // As the IBus edition: the toggle key's last choice in the app decides the language, else
            // the mode picked for it, else the last switch anywhere. A mode picked for the app wins over
            // the shared one: Sen when the shared setting types straight into fields, else Preedit.
            Mode typingMode(const std::string& app) {
                const auto mode       = appModes.find(app);
                const bool vietnamese = appSwitches.find(app).value_or(mode ? *mode != AppMode::Off : !englishOn);
                if (!vietnamese)
                    return Mode::Off;
                if (mode == AppMode::Sen)
                    return Mode::Sen;
                if (mode == AppMode::Preedit)
                    return Mode::Preedit;
                return resources.settings().directTyping ? Mode::Sen : Mode::Preedit;
            }

            void applyMode() {
                const std::string app     = currentApp();
                const bool        changed = app != modeApp;
                if (changed) {
                    modeApp = app;
                    NGOSEN_INFO("App: " + (app.empty() ? std::string("(none)") : app));
                }
                const Mode was = realMode;
                realMode       = typingMode(modeApp);
                // The menu names the app and checks its mode.
                if (changed || realMode != was)
                    showTray();
            }

            // The word ends in the mode it was typed in, and the core drops what it held for it.
            void switchMode() {
                state->commitBuffer();
                state->resetAfterAppCommit(false);
                applyMode();
                // Still the same field, so Sen keeps what it learned of it.
                if (realMode == Mode::Sen)
                    state->activate(Mode::Sen, false);
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
                if (resources.generation() != before) {
                    const Mode was = realMode;
                    applyMode();
                    state->setEngine();
                    // A field focused in Preedit never got Sen's per-field checks.
                    if (realMode == Mode::Sen && was != Mode::Sen)
                        state->activate(Mode::Sen, false);
                }
                readToggleKeys();
            }

            void toggleLanguage() {
                // Read again: the app or its saved language may have changed since the last key.
                const std::string app        = currentApp();
                const bool        vietnamese = typingMode(app) == Mode::Off;
                if (!app.empty() && !appSwitches.set(app, vietnamese))
                    NGOSEN_WARN("Cannot save the language for " + app);
                englishOn = !vietnamese;
                if (!saveEnglishOn(resources.paths(), englishOn))
                    NGOSEN_WARN("Cannot save the language");
                NGOSEN_INFO(std::string("Toggle key: ") + (englishOn ? "English" : "Vietnamese"));
                switchMode();
            }

            // As the IBus edition's focus-out: what the core held for the field is dropped, since
            // nothing may reach it now, and above all not the next field.
            void focusOut() {
                stopRepeat();
                toggle.reset();
                state->resetAfterAppCommit(true);
                state->forgetKeysWhileOff();
                host->dropPending();
            }

            void focusIn() {
                applyNewSettings();
                applyMode();
                // The per-field checks of Sen start over, as the fcitx5 frontend does on focus-in.
                if (realMode == Mode::Sen)
                    state->activate(Mode::Sen, true);
            }

            // activated can come after the field's activate, so the first key may be the first to see
            // the new app. Between words only, as a mode change would end the word.
            void followApp() {
                if (currentApp() == modeApp || host->hasPreedit() || state->holdsWord())
                    return;
                switchMode();
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

            void stopRepeat() {
                if (repeat.source != 0)
                    g_source_remove(repeat.source);
                repeat.source = 0;
                repeat.code   = 0;
            }

            void startRepeat(const ImEvent& event) {
                stopRepeat();
                if (im.repeatRate() <= 0 || !im.keyRepeats(event.code))
                    return;
                const auto delay = static_cast<guint>(std::max(im.repeatDelay(), 0));
                repeat.code      = event.code;
                repeat.time      = event.time + delay;
                repeat.source    = g_timeout_add(delay, onRepeatDelay, this);
            }

            guint repeatInterval() const {
                return static_cast<guint>(std::max(1000 / std::max(im.repeatRate(), 1), 1));
            }

            static gboolean onRepeatDelay(gpointer data) {
                auto* self          = static_cast<Daemon*>(data);
                self->repeat.source = g_timeout_add(self->repeatInterval(), onRepeat, self);
                self->repeatOnce();
                return G_SOURCE_REMOVE;
            }

            static gboolean onRepeat(gpointer data) {
                auto* self = static_cast<Daemon*>(data);
                self->repeat.time += self->repeatInterval();
                self->repeatOnce();
                return self->repeat.source != 0 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
            }

            void repeatOnce() {
                const guint mine = repeat.source;
                // A release or focus change may wait unread in the socket; it ends the repeat first.
                if (!im.readEvents()) {
                    NGOSEN_WARN("Lost the connection to the compositor");
                    stopRepeat();
                    status = 1;
                    g_main_loop_quit(loop);
                    return;
                }
                drain();
                // A key pressed in what was just read starts its own repeat, which waits for its delay.
                if (repeat.source != mine || repeat.code == 0 || !g_main_loop_is_running(loop))
                    return;
                if (!im.field().active) {
                    stopRepeat();
                    return;
                }
                ImEvent event{ImEvent::Kind::Key};
                event.code    = repeat.code;
                event.time    = repeat.time;
                event.pressed = true;
                // As fcitx5: the app gets a release before each repeat, so its own repeat never starts
                // and a repeat the core takes leaves no key down.
                im.sendKey(event.time, event.code, false);
                handleKey(event, true);
                im.flush();
            }

            void handleKey(const ImEvent& event, bool repeated = false) {
                if (!im.field().active)
                    return;
                applyNewSettings();
                followApp();
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
                if (result.toggle) {
                    toggleLanguage();
                    // No mark in the field says which language is on, so the desktop shows it.
                    notice.show(englishOn ? _("English") : _("Vietnamese"), englishOn ? "wl-ngosen-off" : "wl-ngosen");
                }
                if (result.swallow) {
                    host->flush();
                    return;
                }
                host->setKeyTime(event.time);
                WaylandKeyPress key(sym, event.code, mods | (repeated ? modifier::Repeat : 0), event.time);
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
                    if (im.field().active)
                        host->answerDone();
                    host->flush();
                }
                switch (event.kind) {
                    case ImEvent::Kind::Key:
                        handleKey(event);
                        if (event.pressed && im.field().active)
                            startRepeat(event);
                        else if (!event.pressed && event.code == repeat.code)
                            stopRepeat();
                        break;
                    case ImEvent::Kind::RepeatInfo:
                        if (im.repeatRate() <= 0)
                            stopRepeat();
                        break;
                    case ImEvent::Kind::Unavailable:
                        NGOSEN_WARN("The compositor gave the input method to another program, or the seat went away");
                        stopRepeat();
                        status = 1;
                        g_main_loop_quit(loop);
                        break;
                    case ImEvent::Kind::KeymapFailed:
                        stopRepeat();
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
        daemon.stopRepeat();
        g_source_destroy(&source->source);
        g_source_unref(&source->source);
        daemon.im.shutdown();
        g_main_loop_unref(daemon.loop);
        return daemon.status;
    }

} // namespace ngosen
