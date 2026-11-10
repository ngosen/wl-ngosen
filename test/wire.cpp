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
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using test::TestCompositor;

namespace {

    int         failures = 0;
    std::string program;
    fs::path    root;

    // evdev codes
    constexpr uint32_t KeyA = 30, KeyS = 31, KeyLeftShift = 42;

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
    bool begin(TestCompositor& compositor, const std::string& name) {
        compositor.start(program, freshHome(name));
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
    fs::remove_all(root);
    if (failures == 0)
        std::cout << "all wire checks passed\n";
    return failures == 0 ? 0 : 1;
}
