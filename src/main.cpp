/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bamboo-core.h"
#include "ngosen-daemon.h"
#include "ngosen-ibus-config.h"
#include "ngosen-ibus-keys.h"
#include "ngosen-ibus-resources.h"

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glib/gi18n-lib.h>

namespace {

    // The settings window asks the daemon, so the defaults and the names live in one place.
    int runSettingsCommand(const char* command) {
        const bool methods = std::strcmp(command, "--list-input-methods") == 0;
        const bool print   = std::strcmp(command, "--print-settings") == 0;
        if (std::strcmp(command, "--print-dictionary-paths") == 0) {
            std::printf("%s\n%s\n", ngosen::SettingsPaths::fromEnvironment().dictionaryFile().c_str(), ngosen::bundledDictionaryFile().c_str());
            return 0;
        }
        if (!methods && !print && std::strcmp(command, "--list-charsets") != 0)
            return -1;
        Init();
        if (print) {
            // What the daemon types with, after the first-run import: missing or bad keys show their defaults.
            const auto  paths = ngosen::SettingsPaths::fromEnvironment();
            std::string text;
            if (!ngosen::prepareSettings(paths) || !ngosen::readFile(paths.settingsFile(), text))
                return 1;
            std::fputs(ngosen::settingsText(ngosen::readSettings(ngosen::parseIni(text))).c_str(), stdout);
            return 0;
        }
        for (const auto& name : methods ? ngosen::inputMethodNames() : ngosen::charsetNames())
            std::printf("%s\n", name.c_str());
        return 0;
    }

    // The keys the toggle can use, one per line, so a restore names the others before writing.
    int printUsableToggleKeys(int count, char** keys) {
        for (int i = 0; i < count; ++i) {
            if (ngosen::parseToggleKey(keys[i]))
                std::printf("%s\n", keys[i]);
        }
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("wl-ngosen %s\n", NGOSEN_IBUS_VERSION);
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--usable-toggle-keys") == 0)
        return printUsableToggleKeys(argc - 2, argv + 2);
    if (argc > 1) {
        if (const int status = runSettingsCommand(argv[1]); status >= 0)
            return status;
        std::fprintf(stderr, "wl-ngosen: unknown option %s\n", argv[1]);
        return 2;
    }
    // Menu labels are Vietnamese whatever the desktop language is, as in the IBus edition; the
    // English setting shows the strings as written.
    g_setenv("LANGUAGE", "vi", TRUE);
    std::setlocale(LC_ALL, "");
    const char* localeDir = std::getenv("NGOSEN_WL_LOCALEDIR");
    bindtextdomain(GETTEXT_PACKAGE, localeDir != nullptr ? localeDir : NGOSEN_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    return ngosen::runDaemon();
}
