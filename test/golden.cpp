/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
// The files this edition writes are shared with the IBus edition, so their bytes are pinned here:
// a change shows up as a diff against test/golden/expected/.
//
//   golden-test <source dir of test/golden> [--update]
#include "bamboo-core.h"
#include "ngosen-ibus-app-modes.h"
#include "ngosen-ibus-config.h"
#include "ngosen-ibus-resources.h"

#include <cstdlib>
#include <cstring>
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

    std::string read(const fs::path& path) {
        std::string text;
        return readFile(path.string(), text) ? text : "<missing>\n";
    }

    SettingsPaths pathsIn(const fs::path& root) {
        return {(root / "config/ngosen").string(), (root / "data/ngosen").string(), (root / "config/fcitx5/conf").string(), (root / "data/fcitx5/ngosen").string(),
                (root / "state/ngosen").string()};
    }

    // What a user who came from fcitx5 has after picking a few things in the tray menu.
    void writeAll(const fs::path& source, const SettingsPaths& paths) {
        fs::create_directories(paths.fcitxConfigDir);
        for (const auto& entry : fs::directory_iterator(source / "fcitx5"))
            fs::copy_file(entry.path(), fs::path(paths.fcitxConfigDir) / entry.path().filename());
        {
            IBusResources resources(paths);
            IBusSettings  saved;
            check(resources.saveSetting("SpellCheck", "False", saved), "save one setting");
            check(resources.saveSetting("DirectTyping", "False", saved), "save the typing mode");
        }
        AppModes modes(paths.appModesFile());
        check(modes.set("org.gnome.TextEditor", AppMode::Sen) && modes.set("code", std::nullopt), "save app modes");
        AppSwitches switches(paths.appSwitchesFile());
        check(switches.set("firefox", false) && switches.set("kate", std::nullopt), "save app switches");
        VimApps vim(paths.vimAppsFile());
        check(vim.set("foot", true), "save Vim apps");
    }

    const char* const Files[] = {"ngosen.conf", "ngosen-macro-table.conf", "ngosen-app-rules.conf", "ngosen-app-switch.conf", "ngosen-vim-apps.conf"};

    // A deleted ngosen.conf comes back whole from what the daemon runs with, not from the defaults.
    void testSaveAfterDelete(const fs::path& root) {
        const SettingsPaths paths = pathsIn(root);
        fs::create_directories(paths.configDir);
        check(writeFileAtomically(paths.settingsFile(), "InputMethod=VNI\nRetypeAfterSwitch=False\nRetoneSelection=False\n"), "write settings");
        check(writeFileAtomically(paths.importedMark(), ""), "write the import mark");
        IBusResources resources(paths);
        fs::remove(paths.settingsFile());
        IBusSettings saved;
        check(resources.saveSetting("SpellCheck", "False", saved), "save into a deleted file");
        const IniFile ini = parseIni(read(paths.settingsFile()));
        check(ini.top.at("InputMethod") == "VNI", "the input method survives a deleted file");
        check(ini.top.at("RetypeAfterSwitch") == "False" && ini.top.at("RetoneSelection") == "False", "options this edition lacks keep the user's value");
    }

    void testNoReleaseNotice(const fs::path& root) {
        const SettingsPaths paths = pathsIn(root);
        check(prepareSettings(paths), "prepare a new user");
        check(parseIni(read(paths.settingsFile())).top.count("AnnouncedVersion") == 0, "the IBus edition's release notice is left to it");
        check(fs::exists(paths.senDefaultMark()), "the IBus edition does not switch a new file to Sen again");
    }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    Init();
    const fs::path source(argv[1]);
    const bool     update    = argc > 2 && std::strcmp(argv[2], "--update") == 0;
    char           pattern[] = "/tmp/wl-ngosen-golden-XXXXXX";
    const char*    dir       = mkdtemp(pattern);
    if (dir == nullptr)
        return 2;
    const fs::path      root(dir);
    const SettingsPaths paths = pathsIn(root / "golden");
    writeAll(source, paths);
    const fs::path written[] = {paths.settingsFile(), paths.macroFile(), paths.appModesFile(), paths.appSwitchesFile(), paths.vimAppsFile()};
    for (std::size_t i = 0; i < std::size(Files); ++i) {
        const fs::path expected = source / "expected" / Files[i];
        if (update) {
            fs::create_directories(expected.parent_path());
            fs::copy_file(written[i], expected, fs::copy_options::overwrite_existing);
            continue;
        }
        if (read(written[i]) != read(expected)) {
            check(false, std::string(Files[i]) + " differs from test/golden/expected; got:");
            std::cout << read(written[i]);
        }
    }
    testSaveAfterDelete(root / "deleted");
    testNoReleaseNotice(root / "new");
    fs::remove_all(root);
    if (failures == 0)
        std::cout << "golden files: all passed\n";
    return failures == 0 ? 0 : 1;
}
