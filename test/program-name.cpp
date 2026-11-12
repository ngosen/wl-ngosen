/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-program-name.h"

#include <filesystem>
#include <glib.h>
#include <glib/gstdio.h>
#include <iostream>
#include <string>

using namespace ngosen;

namespace {

    int  failures = 0;

    void expect(const std::string& what, const std::string& got, const std::string& want) {
        if (got == want)
            return;
        ++failures;
        std::cout << "FAIL " << what << ": got '" << got << "', want '" << want << "'\n";
    }

    std::string root;

    std::string writeFile(const std::string& dir, const std::string& name, const std::string& text) {
        const std::string folder = root + "/" + dir + "/applications";
        g_mkdir_with_parents(folder.c_str(), 0700);
        const std::string path = folder + "/" + name;
        g_file_set_contents(path.c_str(), text.c_str(), -1, nullptr);
        return path;
    }

    std::string wmClass(const std::string& text) {
        return readStartupWmClass(writeFile("read", "app.desktop", text)).value_or("(unreadable)");
    }

} // namespace

int main() {
    gchar* dir = g_dir_make_tmp("ngosen-names-XXXXXX", nullptr);
    root       = dir;
    g_free(dir);
    // GLib reads the data directories once, so they are set before the first lookup.
    const std::string userExports   = "home/flatpak/exports/share";
    const std::string systemExports = "system/flatpak/exports/share";
    g_setenv("XDG_DATA_HOME", (root + "/home").c_str(), TRUE);
    g_setenv("XDG_DATA_DIRS", (root + "/" + userExports + ":" + root + "/" + systemExports + ":" + root + "/native").c_str(), TRUE);

    expect("the class", wmClass("[Desktop Entry]\nStartupWMClass=imgview\n"), "imgview");
    expect("spaces around the value", wmClass("[Desktop Entry]\nStartupWMClass=  imgview  \n"), "imgview");
    expect("a space in the key", wmClass("[Desktop Entry]\nStartupWMClass = imgview\n"), "");
    expect("no class", wmClass("[Desktop Entry]\nName=Viewer\n"), "");
    expect("a class outside the entry", wmClass("[Desktop Action new]\nStartupWMClass=other\n"), "");
    expect("a quoted value", wmClass("[Desktop Entry]\nStartupWMClass=\"Mullvad Browser\"\n"), "Mullvad Browser");
    expect("an escape in a quoted value", wmClass("[Desktop Entry]\nStartupWMClass=\"a\\\\b\"\n"), "a\\b");
    expect("a bad quoted value is skipped", wmClass("[Desktop Entry]\nStartupWMClass=imgview\nStartupWMClass=\"a\"b\"\n"), "imgview");
    expect("a backslash outside quotes stays", wmClass("[Desktop Entry]\nStartupWMClass=a\\sb\n"), "a\\sb");
    expect("a line that does not parse", wmClass("[Desktop Entry]\nnot a line\nStartupWMClass=imgview\n"), "imgview");
    expect("a file that cannot be read", readStartupWmClass(root + "/missing.desktop").value_or("(unreadable)"), "(unreadable)");

    writeFile("home", "org.example.Both.desktop", "");
    writeFile(systemExports, "org.example.Both.desktop", "");
    const auto found = desktopFiles("org.example.Both");
    expect("the user's data first", found.empty() ? "" : found.front(), root + "/home/applications/org.example.Both.desktop");
    expect("then the system's", std::to_string(found.size()), "2");

    writeFile(systemExports, "org.mozilla.firefox.desktop", "[Desktop Entry]\nStartupWMClass=firefox\n");
    writeFile(systemExports, "net.mullvad.MullvadBrowser.desktop", "[Desktop Entry]\nStartupWMClass=Mullvad Browser\n");
    writeFile(systemExports, "net.waterfox.waterfox.desktop", "[Desktop Entry]\nName=Waterfox\n");
    writeFile("native", "org.gnome.Ptyxis.desktop", "[Desktop Entry]\nStartupWMClass=ptyxis\n");
    expect("Flatpak Firefox", gnomeProgramName("org.mozilla.firefox.desktop"), "firefox");
    expect("a class with spaces", gnomeProgramName("net.mullvad.MullvadBrowser.desktop"), "mullvadbrowser");
    expect("no class names it by the ID", gnomeProgramName("net.waterfox.waterfox.desktop"), "waterfox");
    expect("a native app keeps its name", gnomeProgramName("org.gnome.Ptyxis.desktop"), "org.gnome.Ptyxis.desktop");
    expect("a plain name", gnomeProgramName("Alacritty.desktop"), "Alacritty.desktop");

    expect("an app not installed yet", gnomeProgramName("com.example.Late.desktop"), "com.example.Late.desktop");
    writeFile(systemExports, "com.example.Late.desktop", "[Desktop Entry]\nStartupWMClass=late\n");
    expect("an app installed later", gnomeProgramName("com.example.Late.desktop"), "late");

    // An unreadable export, here a folder, gives way to the next one on the data path.
    g_mkdir_with_parents((root + "/" + userExports + "/applications/com.example.Viewer.desktop").c_str(), 0700);
    writeFile(systemExports, "com.example.Viewer.desktop", "[Desktop Entry]\nStartupWMClass=imgview\n");
    expect("an unreadable export", gnomeProgramName("com.example.Viewer.desktop"), "imgview");

    std::filesystem::remove_all(root);
    if (failures == 0)
        std::cout << "program_name: all passed\n";
    return failures == 0 ? 0 : 1;
}
