/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-app-modes.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace ngosen;

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& what) {
        if (!ok) {
            ++failures;
            std::cout << "FAIL " << what << '\n';
        }
    }

    void testNames() {
        check(modeAppName("gtk3-im:firefox") == "firefox", "an IBus client is named by its program");
        check(modeAppName("firefox_firefox") == "firefox", "a snap is named once");
        check(modeAppName("org.gnome.TextEditor.desktop") == "org.gnome.TextEditor", "a GNOME app is named without .desktop");
        check(modeAppName("org.gnome.Ptyxis") == "org.gnome.Ptyxis", "a name stays as it is");
        for (const char* shared : {"", "gnome-shell", "QIBusInputContext", "xim"})
            check(modeAppName(shared).empty(), std::string("no single app: ") + shared);
    }

    void testFormat() {
        const AppModeMap modes = parseAppModes("# comment\nfirefox=5\nkate = 2\nlegacy=1\ncode=0\nemoji=6\nwide=99\nword=abc\nno equals\ngnome-shell=5\n");
        check(modes.size() == 4, "Sen, Preedit and Off for a named app are kept");
        check(modes.count("code") && modes.at("code") == AppMode::Off, "0 is Off");
        check(!modes.count("emoji"), "an emoji pick has no meaning here");
        check(modes.count("firefox") && modes.at("firefox") == AppMode::Preedit, "5 is Preedit");
        check(modes.count("kate") && modes.at("kate") == AppMode::Sen, "2 is Sen, spaces trimmed");
        check(modes.count("legacy") && modes.at("legacy") == AppMode::Sen, "a former Sen number is Sen");
        check(parseAppModes(appModesText(modes)) == modes, "app modes round trip");
        check(appModesText({{"firefox", AppMode::Preedit}}).find("\nfirefox=5\n") != std::string::npos, "written as fcitx5 writes it");
        check(appModesText({{"code", AppMode::Off}}).find("\ncode=0\n") != std::string::npos, "Off written as 0");
    }

    void testSwitchFormat() {
        const AppSwitchMap switches = parseAppSwitches("[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n[Switches/1]\nApp=firefox.desktop\nVietnamese=True\n"
                                                       "[Switches/2]\nApp=gnome-shell\nVietnamese=True\n[Switches/3]\nApp=broken\nVietnamese=Maybe\n");
        check(switches.size() == 2, "named apps with a clear value are kept");
        check(switches.count("kate") && !switches.at("kate"), "False is English");
        check(switches.count("firefox") && switches.at("firefox"), "True is Vietnamese, named without .desktop");
        check(parseAppSwitches(appSwitchesText(switches)) == switches, "app switches round trip");
        check(appSwitchesText({{"kate", false}}) == "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n", "written as fcitx5 writes it");
        check(parseAppSwitches("").empty(), "an empty file has no switches");
        const AppSwitchMap quoted = parseAppSwitches("[Switches/0]\nApp=\"My App\"\nVietnamese=False\n[Switches/2]\nApp=late\nVietnamese=True\n");
        check(quoted.size() == 1 && quoted.count("My App"), "a quoted name is read as fcitx5 writes it, and the list stops at a gap");
        check(parseAppSwitches(appSwitchesText({{"My App", true}})) == AppSwitchMap{{"My App", true}}, "a name with a space round trips");
    }

    void testSwitchFile(const fs::path& root) {
        const std::string path = (root / "ngosen-app-switch.conf").string();
        AppSwitches       switches(path);
        check(!switches.find("kate"), "no file, no switches");
        check(switches.set("kate", std::nullopt) && !fs::exists(path), "forgetting nothing writes no file");
        check(switches.set("kate", false) && switches.find("kate") == false, "a switch is saved");
        check(!switches.find("firefox"), "another app was never switched");
        check(AppSwitches(path).find("kate") == false, "the switch is kept after a restart");
        check(switches.set("firefox", true) && switches.find("kate") == false && switches.find("firefox") == true, "two apps keep their own");

        writeFileAtomically(path, "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=True\n");
        fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(2));
        check(switches.find("kate") == true && !switches.find("firefox"), "an edit elsewhere applies without a restart");

        check(switches.set("kate", std::nullopt) && !switches.find("kate"), "a mode picked for the app forgets its switch");
        check(!switches.set("", true) && !switches.find(""), "an app without a name keeps no switch");

        AppSwitches failing(path, [](const std::string&, const std::string&) { return false; });
        check(!failing.set("kate", false), "a failed write is reported");
        check(!failing.find("kate"), "a failed write changes nothing");
    }

    void testFile(const fs::path& root) {
        const std::string path = (root / "ngosen-app-rules.conf").string();
        AppModes          modes(path);
        check(!modes.find("firefox"), "no file, no modes");
        check(modes.set("firefox", AppMode::Preedit), "a mode is saved");
        check(modes.find("firefox") == AppMode::Preedit, "the saved mode applies");
        check(!modes.find("kate"), "another app keeps the shared mode");
        check(AppModes(path).find("firefox") == AppMode::Preedit, "the mode is kept after a restart");

        writeFileAtomically(path, "firefox=2\nkate=5\n");
        fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(2));
        check(modes.find("firefox") == AppMode::Sen && modes.find("kate") == AppMode::Preedit, "an edit by hand applies without a restart");

        check(modes.set("firefox", std::nullopt) && !modes.find("firefox"), "Default forgets the app");
        check(modes.find("kate") == AppMode::Preedit, "forgetting one app keeps the others");

        check(!modes.set("", AppMode::Sen) && !modes.find(""), "an app without a name keeps nothing");

        writeFileAtomically(path, "# mine\ncode=0\nfirefox.desktop=5\nkate=2\n");
        fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(4));
        check(modes.set("firefox", AppMode::Sen), "a pick over a file written elsewhere");
        std::string text;
        readFile(path, text);
        check(text == "# mine\ncode=0\nkate=2\nfirefox=2\n", "a pick changes only the app's own lines: " + text);

        AppModes failing(path, [](const std::string&, const std::string&) { return false; });
        check(!failing.set("kate", AppMode::Sen), "a failed write is reported");
        check(failing.find("kate") == AppMode::Sen, "a failed write changes nothing");
    }

    int  writes = 0;

    void testVimFile(const fs::path& root) {
        check(parseVimApps("# comment\nkitty\n\norg.gnome.Ptyxis\n") == VimAppSet{"kitty", "org.gnome.Ptyxis"}, "one app per line, comments and blank lines skipped");
        check(parseVimApps("kitty.desktop\ngtk3-im:code \r\ngnome-shell\n") == VimAppSet{"kitty", "code"}, "names are read as the other app files read them");
        check(parseVimApps(vimAppsText({"kitty"})) == VimAppSet{"kitty"}, "Vim apps round trip");
        const std::string path = (root / "ngosen-vim-apps.conf").string();
        VimApps           apps(path);
        check(!apps.has("kitty"), "no file, no Vim mode");
        check(apps.set("kitty", true) && apps.has("kitty") && !apps.has("code"), "Vim mode is kept for one app");
        check(VimApps(path).has("kitty"), "Vim mode is kept after a restart");
        check(apps.set("kitty", false) && !apps.has("kitty"), "Vim mode can be switched off");
        check(!apps.set("", true) && !apps.has(""), "an app without a name keeps nothing");

        writeFileAtomically(path, "# by fcitx5\nkitty.desktop\n");
        fs::last_write_time(path, fs::last_write_time(path) + std::chrono::seconds(2));
        check(apps.has("kitty"), "a name fcitx5 saved with .desktop applies, without a restart");
        check(apps.set("kitty", false) && !apps.has("kitty"), "switching it off forgets the name fcitx5 saved");

        VimApps counting(path, [&](const std::string& to, const std::string& text) {
            ++writes;
            return writeFileAtomically(to, text);
        });
        writes = 0;
        check(counting.set("code", false) && writes == 0, "switching off an app without Vim mode writes nothing");

        VimApps failing(path, [](const std::string&, const std::string&) { return false; });
        check(!failing.set("code", true) && !failing.has("code"), "a failed write changes nothing");
    }

} // namespace

int main() {
    char        pattern[] = "/tmp/ngosen-app-modes-XXXXXX";
    const char* dir       = mkdtemp(pattern);
    if (dir == nullptr)
        return 2;
    const fs::path root(dir);
    testNames();
    testFormat();
    testFile(root);
    testSwitchFormat();
    testSwitchFile(root);
    testVimFile(root);
    fs::remove_all(root);
    if (failures == 0)
        std::cout << "all app mode checks passed\n";
    return failures == 0 ? 0 : 1;
}
