/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// The daemon against a compositor that writes down every request, for the order rules of
// input-method-v2 and the virtual keyboard that a real compositor punishes with a disconnect.
#include "compositor/test-compositor.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using test::TestCompositor;

namespace {

    int         failures = 0;
    std::string program;
    fs::path    root;

    // evdev codes
    constexpr uint32_t KeyA = 30, KeyS = 31, KeyZ = 44, KeyLeftShift = 42, KeyLeftCtrl = 29, KeyLeftAlt = 56;
    constexpr uint32_t KeyC = 46, KeyH = 35, KeyO = 24, KeyF = 33, KeySpace = 57;
    constexpr uint32_t PurposePassword = 8, CauseOther = 1;
    // Modifier bits of the us keymap the test compositor sends.
    constexpr uint32_t ShiftBit = 1U << 0, CtrlBit = 1U << 2, AltBit = 1U << 3;

    void               check(bool ok, const std::string& what, const TestCompositor* compositor = nullptr) {
        if (ok)
            return;
        ++failures;
        std::cout << "FAIL " << what << '\n';
        if (compositor != nullptr)
            compositor->dump();
    }

    std::string freshHome(const std::string& name) {
        const fs::path home = root / name;
        fs::create_directories(home / ".config");
        return home.string();
    }

    // Starts the daemon and waits until it has the input method object.
    enum class Language {
        English,
        Vietnamese
    };

    // English unless asked: most checks are about keys reaching the app, which typing Vietnamese changes.
    bool begin(TestCompositor& compositor, const std::string& name, const std::string& settings = "", Language language = Language::English) {
        const std::string home = freshHome(name);
        if (language == Language::English) {
            fs::create_directories(fs::path(home) / ".local/state/ngosen");
            std::ofstream(fs::path(home) / ".local/state/ngosen/english");
        }
        if (!settings.empty()) {
            fs::create_directories(fs::path(home) / ".config/ngosen");
            std::ofstream(fs::path(home) / ".config/ngosen/ngosen.conf") << settings;
        }
        compositor.start(program, home, {std::string("NGOSEN_IBUS_DICTIONARY=") + TEST_DICTIONARY});
        return compositor.waitFor([&] { return compositor.hasClient() && compositor.logged("vk.create"); });
    }

    // Activates a field and waits for the keymap to reach the virtual keyboard.
    bool focus(TestCompositor& compositor) {
        compositor.activate();
        compositor.done();
        return compositor.waitFor([&] { return compositor.grabbed() && compositor.count("vk.keymap") > 0; });
    }

    std::size_t indexOf(const TestCompositor& compositor, const std::string& prefix) {
        const auto& log = compositor.log();
        for (std::size_t i = 0; i < log.size(); ++i) {
            if (log[i].rfind(prefix, 0) == 0)
                return i;
        }
        return log.size();
    }

    bool balanced(const TestCompositor& compositor) {
        return compositor.count("vk.key ") > 0 && compositor.lines("vk.key ").size() % 2 == 0 &&
            compositor.count("vk.key " + std::to_string(KeyA) + " 1") == compositor.count("vk.key " + std::to_string(KeyA) + " 0");
    }

    void testPassthrough() {
        TestCompositor compositor({});
        check(begin(compositor, "passthrough"), "the daemon binds the input method and a virtual keyboard", &compositor);
        check(focus(compositor), "an active field gets the grab and the keymap", &compositor);
        check(indexOf(compositor, "vk.keymap") < indexOf(compositor, "vk.mods"), "the keymap reaches the virtual keyboard before modifiers", &compositor);
        compositor.tap(KeyA);
        compositor.tap(KeyS);
        check(compositor.barrier(), "keys come back through the virtual keyboard", &compositor);
        const auto keys = compositor.lines("vk.key ");
        check(keys.size() >= 4 && keys[0] == "vk.key 30 1" && keys[1] == "vk.key 30 0" && keys[2] == "vk.key 31 1" && keys[3] == "vk.key 31 0", "keys pass in the order they came",
              &compositor);
        check(compositor.count("im.") == 1 /* im.create */, "plain passthrough sends nothing to the field", &compositor);
        compositor.stop();
        check(!compositor.logged("error: a second grab while one is held"), "one grab at a time", &compositor);
    }

    void testModifiersMirrored() {
        TestCompositor compositor({});
        check(begin(compositor, "modifiers") && focus(compositor), "setup for modifiers", &compositor);
        compositor.key(KeyLeftShift, true);
        compositor.modifiers(1, 0, 0, 0);
        compositor.tap(KeyA);
        compositor.modifiers(0, 0, 0, 0);
        compositor.key(KeyLeftShift, false);
        check(compositor.barrier(), "barrier after shift", &compositor);
        check(compositor.logged("vk.mods 1 0 0 0"), "the shift state reaches the virtual keyboard", &compositor);
        check(indexOf(compositor, "vk.mods 1 0 0 0") < indexOf(compositor, "vk.key 30 1"), "shift is down before the key it changes", &compositor);
        compositor.stop();
    }

    void testFocusSequences() {
        {
            TestCompositor compositor({});
            check(begin(compositor, "activate-twice"), "setup for activate twice", &compositor);
            compositor.activate();
            compositor.activate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.grabbed(); }), "activate;activate;done grabs", &compositor);
            check(compositor.barrier(), "barrier after activate twice", &compositor);
            check(compositor.count("grab.take") - compositor.count("grab.release") == 1, "activate;activate;done holds one grab", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "activate-deactivate") && focus(compositor), "setup for activate;deactivate", &compositor);
            compositor.clearLog();
            compositor.activate();
            compositor.deactivate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.logged("grab.release"); }), "activate;deactivate;done releases the grab", &compositor);
            compositor.run(100);
            check(!compositor.grabbed(), "no grab is held after activate;deactivate;done", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "deactivate-activate") && focus(compositor), "setup for deactivate;activate", &compositor);
            compositor.clearLog();
            compositor.deactivate();
            compositor.activate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.logged("grab.release") && compositor.logged("grab.take"); }),
                  "deactivate;activate;done gives the new field a grab of its own", &compositor);
            check(indexOf(compositor, "grab.release") < indexOf(compositor, "grab.take"), "the old grab goes before the new one", &compositor);
            check(compositor.barrier(), "keys pass in the new field", &compositor);
            compositor.stop();
        }
    }

    void testReleaseOnDeactivate() {
        TestCompositor compositor({});
        check(begin(compositor, "release") && focus(compositor), "setup for release on deactivate", &compositor);
        compositor.key(KeyA, true);
        check(compositor.waitFor([&] { return compositor.logged("vk.key 30 1"); }), "the press passes", &compositor);
        compositor.deactivate();
        compositor.done();
        check(compositor.waitFor([&] { return compositor.logged("vk.key 30 0"); }), "a key held when the field goes away is let go", &compositor);
        check(indexOf(compositor, "vk.key 30 0") < indexOf(compositor, "grab.release"), "the key is let go before the grab", &compositor);
        compositor.run(100);
        const std::size_t before = compositor.count("vk.key ");
        compositor.key(KeyA, false); // the grab is gone; a real compositor sends nothing more
        compositor.run(100);
        check(compositor.count("vk.key ") == before, "nothing is typed after deactivate", &compositor);
        compositor.stop();
    }

    void testKeymaps() {
        {
            TestCompositor::Options options;
            options.keymapOnGrab = false;
            TestCompositor compositor(options);
            check(begin(compositor, "no-keymap"), "setup for keys before a keymap", &compositor);
            compositor.activate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.grabbed(); }), "the grab comes without a keymap", &compositor);
            compositor.tap(KeyA);
            compositor.sendEmptyKeymap();
            compositor.tap(KeyA);
            compositor.run(200);
            check(compositor.count("vk.key ") == 0 && compositor.count("vk.mods") == 0, "no key or modifiers before the virtual keyboard has a keymap", &compositor);
            compositor.sendKeymap("us");
            check(compositor.barrier(), "a real keymap after an empty one starts the typing", &compositor);
            check(indexOf(compositor, "vk.keymap") < indexOf(compositor, "vk.key "), "the keymap comes first", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "keymap-change") && focus(compositor), "setup for a keymap change", &compositor);
            compositor.sendKeymap("fr");
            check(compositor.barrier(), "keys pass after a keymap change", &compositor);
            check(compositor.count("vk.keymap") == 2, "the new keymap reaches the virtual keyboard", &compositor);
            compositor.sendKeymap("fr");
            compositor.barrier();
            check(compositor.count("vk.keymap") == 2, "the same keymap again is not sent again", &compositor);
            compositor.stop();
        }
    }

    void testReviewCases() {
        {
            TestCompositor compositor({});
            check(begin(compositor, "keymap-size") && focus(compositor), "setup for the keymap size", &compositor);
            check(compositor.count("error:") == 0, "the virtual keyboard's keymap file is as long as its size says", &compositor);
            compositor.stop();
        }
        {
            TestCompositor::Options options;
            options.keymapOnGrab = false;
            TestCompositor compositor(options);
            check(begin(compositor, "broken-keymap-first"), "setup for a broken first keymap", &compositor);
            compositor.activate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.grabbed(); }), "the grab comes before the broken keymap", &compositor);
            compositor.sendBrokenKeymap();
            // Holding the grab with no keymap to type with would swallow every key.
            check(compositor.waitExit(3000) == 1, "a keymap the daemon cannot read ends it with status 1", &compositor);
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "keys-behind-deactivate") && focus(compositor), "setup for keys behind a deactivate", &compositor);
            compositor.clearLog();
            compositor.deactivate();
            compositor.done(false);
            compositor.key(KeyA, true, false);
            compositor.run(0);
            check(compositor.waitFor([&] { return compositor.logged("grab.release"); }), "the grab goes with the field", &compositor);
            compositor.run(100);
            check(compositor.count("vk.key ") == 0, "a key read with the deactivate is not typed after the grab is gone", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "mods-on-deactivate") && focus(compositor), "setup for modifiers on deactivate", &compositor);
            compositor.key(KeyLeftShift, true);
            compositor.modifiers(1, 0, 2, 0);
            check(compositor.waitFor([&] { return compositor.logged("vk.mods 1 0 2 0"); }), "shift and caps lock reach the virtual keyboard", &compositor);
            compositor.deactivate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.logged("grab.release"); }), "the grab goes with the field", &compositor);
            const auto mods = compositor.lines("vk.mods ");
            check(!mods.empty() && mods.back() == "vk.mods 0 0 2 0", "shift is let go on deactivate, caps lock stays", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            compositor.start(program, freshHome("no-compositor"), {"WAYLAND_DISPLAY=wl-ngosen-missing"});
            check(compositor.waitExit(3000) == 1, "no compositor to connect to ends the daemon with status 1", &compositor);
        }
    }

    bool englishMark(const std::string& name) {
        return fs::exists(root / name / ".local/state/ngosen/english");
    }

    // Ctrl+Shift as a compositor sends it: each key, then the modifiers it leaves.
    void tapCtrlShift(TestCompositor& compositor) {
        compositor.key(KeyLeftCtrl, true);
        compositor.modifiers(CtrlBit, 0, 0, 0);
        compositor.key(KeyLeftShift, true);
        compositor.modifiers(CtrlBit | ShiftBit, 0, 0, 0);
        compositor.key(KeyLeftShift, false);
        compositor.modifiers(CtrlBit, 0, 0, 0);
        compositor.key(KeyLeftCtrl, false);
        compositor.modifiers(0, 0, 0, 0);
    }

    void testToggleKey() {
        {
            TestCompositor compositor({});
            check(begin(compositor, "toggle", "", Language::Vietnamese) && focus(compositor), "setup for the toggle key", &compositor);
            check(!englishMark("toggle"), "a new user starts in Vietnamese");
            tapCtrlShift(compositor);
            check(compositor.barrier(), "barrier after Ctrl+Shift", &compositor);
            check(englishMark("toggle"), "a tap of Ctrl+Shift switches to English, shared with the IBus edition", &compositor);
            check(compositor.count("vk.key " + std::to_string(KeyLeftShift) + " ") == 2 && compositor.count("vk.key " + std::to_string(KeyLeftCtrl) + " ") == 2,
                  "the app sees Ctrl and Shift, as with fcitx5", &compositor);
            tapCtrlShift(compositor);
            check(compositor.barrier(), "barrier after the second Ctrl+Shift", &compositor);
            check(!englishMark("toggle"), "a second tap switches back to Vietnamese", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "toggle-letter", "ToggleKey=Alt+z\n", Language::Vietnamese) && focus(compositor), "setup for Alt+Z", &compositor);
            compositor.key(KeyLeftAlt, true);
            compositor.modifiers(AltBit, 0, 0, 0);
            compositor.tap(KeyZ);
            compositor.key(KeyLeftAlt, false);
            compositor.modifiers(0, 0, 0, 0);
            check(compositor.barrier(), "barrier after Alt+Z", &compositor);
            check(englishMark("toggle-letter"), "Alt+Z switches to English", &compositor);
            check(compositor.count("vk.key " + std::to_string(KeyZ) + " ") == 0, "the app sees neither the press nor the release of Z", &compositor);
            check(balanced(compositor), "every key the app saw is let go", &compositor);
            compositor.tap(KeyZ);
            check(compositor.barrier() && compositor.count("vk.key " + std::to_string(KeyZ) + " ") == 2, "a plain z still types", &compositor);
            compositor.stop();
        }
    }

    void type(TestCompositor& compositor, std::initializer_list<uint32_t> keys, bool send = true) {
        for (uint32_t key : keys)
            compositor.tap(key, send);
    }

    // Each commit must name the latest done, or the compositor drops it as stale.
    bool serialsCurrent(const TestCompositor& compositor) {
        const auto commits = compositor.lines("im.commit ");
        for (const auto& line : commits) {
            if (line != "im.commit " + std::to_string(compositor.dones()))
                return false;
        }
        return !commits.empty();
    }

    void testPreedit() {
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit", "", Language::Vietnamese) && focus(compositor), "setup for preedit", &compositor);
            compositor.clearLog();
            type(compositor, {KeyC, KeyH, KeyA, KeyO, KeyF, KeySpace});
            check(compositor.barrier(), "barrier after a word", &compositor);
            const auto preedits = compositor.lines("im.preedit ");
            // The protocol counts the cursor in bytes: "chào" is five.
            check(!preedits.empty() && preedits.back() == "im.preedit chào 5 5", "the word shows as underlined text while typing", &compositor);
            check(compositor.count("im.commit_string chào ") == 1, "space commits the word with the space", &compositor);
            check(compositor.lines("vk.key ").size() == 2 /* the barrier */, "the app gets no key for letters that went into the word", &compositor);
            check(compositor.count("vk.key " + std::to_string(KeySpace) + " ") == 0, "the app gets no space key on top", &compositor);
            const std::size_t at = indexOf(compositor, "im.commit_string");
            check(at + 1 < compositor.log().size() && compositor.log()[at + 1].rfind("im.commit ", 0) == 0, "the commit carries no preedit, which clears it", &compositor);
            check(serialsCurrent(compositor), "every commit names the latest done", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit-focus", "", Language::Vietnamese) && focus(compositor), "setup for focus loss", &compositor);
            type(compositor, {KeyC, KeyH, KeyA, KeyO});
            // Not barrier(): its key would end the word.
            check(compositor.waitFor([&] { return compositor.logged("im.preedit chao 4 4"); }), "a half word shows", &compositor);
            compositor.clearLog();
            compositor.deactivate();
            compositor.done();
            compositor.run(200);
            check(compositor.count("im.commit") == 0, "nothing goes to a field that lost focus", &compositor);
            compositor.activate();
            compositor.done();
            check(compositor.waitFor([&] { return compositor.grabbed(); }), "a new field gets the grab", &compositor);
            type(compositor, {KeyA});
            check(compositor.barrier(), "barrier in the new field", &compositor);
            const auto preedits = compositor.lines("im.preedit ");
            check(preedits.size() == 1 && preedits[0] == "im.preedit a 1 1", "the new field starts with an empty word", &compositor);
            check(compositor.count("im.commit_string chao") == 0, "nothing of the old word reaches the new field", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit-stale", "", Language::Vietnamese) && focus(compositor), "setup for a stale batch", &compositor);
            compositor.clearLog();
            // Keys and the focus change arrive in one read: the keys belong to the field that is gone.
            type(compositor, {KeyC, KeyH, KeyA}, false);
            compositor.deactivate();
            compositor.done();
            compositor.run(300);
            check(compositor.count("im.") == 0, "keys read with a focus change send nothing to either field", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit-serial", "", Language::Vietnamese) && focus(compositor), "setup for a done behind a key", &compositor);
            compositor.clearLog();
            // The done is read with the key, so the commit for the key must already name it.
            type(compositor, {KeyA}, false);
            compositor.surrounding("x", 1, 1);
            compositor.done();
            check(compositor.waitFor([&] { return compositor.logged("im.preedit a 1 1"); }), "the key shows", &compositor);
            check(serialsCurrent(compositor), "a commit names a done read in the same batch", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit-password", "", Language::Vietnamese) && focus(compositor), "setup for a password", &compositor);
            compositor.contentType(0, PurposePassword);
            compositor.done();
            compositor.clearLog();
            type(compositor, {KeyC, KeyH, KeyA, KeyO, KeyF});
            check(compositor.barrier(), "barrier in a password field", &compositor);
            check(compositor.count("im.") == 0, "a password field gets no preedit", &compositor);
            const auto keys = compositor.lines("vk.key ");
            check(keys.size() >= 10 && keys[0] == "vk.key 46 1" && keys[8] == "vk.key 33 1", "a password field gets the keys as typed", &compositor);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "preedit-cause", "", Language::Vietnamese) && focus(compositor), "setup for an app change", &compositor);
            compositor.surrounding("xyz", 3, 3);
            compositor.done();
            type(compositor, {KeyC, KeyH, KeyA});
            check(compositor.waitFor([&] { return compositor.logged("im.preedit cha 3 3"); }), "a half word shows", &compositor);
            compositor.clearLog();
            // A click moves the cursor in unchanged text, and the app says it made the change.
            compositor.surrounding("xyz", 0, 0);
            compositor.cause(CauseOther);
            compositor.done();
            type(compositor, {KeyF, KeySpace});
            check(compositor.barrier(), "barrier after the click", &compositor);
            // As with fcitx5's input-method-v2 frontend: the underlined word moves with the cursor and goes on there.
            check(compositor.count("im.commit_string chà ") == 1 && !compositor.logged("im.commit_string cha"), "a click leaves the underlined word to the core, as fcitx5 does",
                  &compositor);
            check(serialsCurrent(compositor), "commits after the click name its done", &compositor);
            compositor.stop();
        }
    }

    // Presses and releases of one key alternate, starting with a press and ending with a release.
    bool alternates(const TestCompositor& compositor, uint32_t code) {
        const auto lines = compositor.lines("vk.key " + std::to_string(code) + " ");
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] != "vk.key " + std::to_string(code) + (i % 2 == 0 ? " 1" : " 0"))
                return false;
        }
        return !lines.empty() && lines.size() % 2 == 0;
    }

    void testRepeat() {
        {
            TestCompositor compositor({});
            check(begin(compositor, "repeat") && focus(compositor), "setup for repeat", &compositor);
            // Fast, so the test is short: a repeat every 10 ms after 50 ms.
            compositor.repeatInfo(100, 50);
            compositor.clearLog();
            compositor.key(KeyA, true);
            compositor.runFor(300);
            compositor.key(KeyA, false);
            // No other key yet: a new press would end the repeat by itself.
            compositor.runFor(50);
            const std::size_t atRelease = compositor.count("vk.key " + std::to_string(KeyA) + " ");
            compositor.runFor(200);
            check(compositor.count("vk.key " + std::to_string(KeyA) + " ") == atRelease, "repeat stops when the key is let go", &compositor);
            check(compositor.barrier(), "barrier after a held key", &compositor);
            check(compositor.count("vk.key " + std::to_string(KeyA) + " 1") >= 5, "a held key repeats", &compositor);
            check(alternates(compositor, KeyA), "each repeat is a release then a press, and the last is let go", &compositor);

            compositor.clearLog();
            compositor.key(KeyLeftShift, true);
            compositor.runFor(200);
            compositor.key(KeyLeftShift, false);
            check(compositor.barrier() && compositor.count("vk.key " + std::to_string(KeyLeftShift) + " 1") == 1, "modifiers do not repeat", &compositor);

            compositor.repeatInfo(0, 50);
            compositor.clearLog();
            compositor.key(KeyA, true);
            compositor.runFor(200);
            compositor.key(KeyA, false);
            check(compositor.barrier() && compositor.count("vk.key " + std::to_string(KeyA) + " 1") == 1, "rate 0 turns repeat off", &compositor);

            compositor.repeatInfo(100, 50);
            compositor.clearLog();
            compositor.key(KeyA, true);
            compositor.runFor(150);
            compositor.deactivate();
            compositor.done();
            compositor.runFor(50);
            const std::size_t atFocusLoss = compositor.count("vk.key " + std::to_string(KeyA) + " ");
            compositor.runFor(200);
            check(compositor.count("vk.key " + std::to_string(KeyA) + " ") == atFocusLoss && alternates(compositor, KeyA),
                  "repeat stops when the field loses focus, with the key let go", &compositor);
            compositor.key(KeyA, false);
            compositor.stop();
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "repeat-word", "", Language::Vietnamese) && focus(compositor), "setup for a held letter", &compositor);
            compositor.repeatInfo(100, 50);
            compositor.clearLog();
            compositor.key(KeyA, true);
            compositor.runFor(300);
            compositor.key(KeyA, false);
            check(compositor.waitFor([&] { return compositor.lines("im.preedit ").size() >= 3; }), "a held letter repeats into the word", &compositor);
            compositor.runFor(50);
            check(compositor.count("vk.key " + std::to_string(KeyA) + " ") == 0, "the app gets no key for repeats the core took", &compositor);
            compositor.stop();
        }
    }

    void testExits() {
        {
            TestCompositor::Options options;
            options.inputMethod = false;
            TestCompositor compositor(options);
            compositor.start(program, freshHome("no-im"));
            check(compositor.waitExit(3000) == 0, "a compositor without input-method-v2 ends the daemon quietly", &compositor);
        }
        {
            TestCompositor::Options options;
            options.virtualKeyboard = false;
            TestCompositor compositor(options);
            compositor.start(program, freshHome("no-vk"));
            check(compositor.waitExit(3000) == 0, "a compositor without a virtual keyboard ends the daemon quietly", &compositor);
            check(compositor.count("grab.take") == 0, "and it never grabs the keyboard", &compositor);
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "unavailable") && focus(compositor), "setup for unavailable", &compositor);
            compositor.unavailable();
            check(compositor.waitExit(3000) == 1, "unavailable ends the daemon with status 1", &compositor);
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "global-removed") && focus(compositor), "setup for a removed global", &compositor);
            compositor.removeInputMethodGlobal();
            check(compositor.waitExit(3000) == 1, "losing the input method global ends the daemon with status 1", &compositor);
        }
        {
            TestCompositor compositor({});
            check(begin(compositor, "signal") && focus(compositor), "setup for SIGTERM", &compositor);
            compositor.key(KeyA, true);
            check(compositor.waitFor([&] { return compositor.logged("vk.key 30 1"); }), "the press passes before SIGTERM", &compositor);
            // The daemon's last requests must still arrive when the compositor reads them late.
            compositor.stopWhileBusy(300);
            check(compositor.logged("vk.key 30 0"), "a held key is let go when the daemon stops", &compositor);
            check(balanced(compositor), "every press has its release", &compositor);
        }
    }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wire-test <wl-ngosen>\n";
        return 2;
    }
    program               = argv[1];
    char        pattern[] = "/tmp/wl-ngosen-wire-XXXXXX";
    const char* dir       = mkdtemp(pattern);
    if (dir == nullptr)
        return 2;
    root = dir;
    testPassthrough();
    testModifiersMirrored();
    testFocusSequences();
    testReleaseOnDeactivate();
    testKeymaps();
    testExits();
    testReviewCases();
    testToggleKey();
    testPreedit();
    testRepeat();
    fs::remove_all(root);
    if (failures == 0)
        std::cout << "all wire checks passed\n";
    return failures == 0 ? 0 : 1;
}
