/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-ibus-config.h"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace ngosen {

    // The typing mode picked for one app in the mode menu; Off types English until the toggle key
    // switches the app back.
    enum class AppMode {
        Sen,
        Preedit,
        Off
    };

    using AppModeMap = std::map<std::string, AppMode>;

    // The name a mode is remembered by, from the program a field reports. Empty when the name stands for
    // no single app: GNOME Shell before the portal names the app, Qt and XIM clients of any app.
    std::string modeAppName(const std::string& program);

    // The fcitx5 edition's ngosen-app-rules.conf format: "app=number", 0 for Off, 2 for Sen and 5 for
    // Preedit. Emoji (6) has no meaning here and is skipped.
    AppModeMap  parseAppModes(const std::string& text);
    std::string appModesText(const AppModeMap& modes);

    class AppModes {
      public:
        explicit AppModes(std::string path, FileWriter write = writeFileAtomically);

        std::optional<AppMode> find(const std::string& app);
        // nullopt forgets the app, so it follows the shared mode again. False when the file was not written.
        bool set(const std::string& app, std::optional<AppMode> mode);

      private:
        // Rereads the file when it changed, so an edit by hand applies without a restart.
        void        reloadIfChanged();

        std::string path_;
        FileWriter  write_;
        AppModeMap  modes_;
        // Unset until the file was read; a missing file reads as no modes.
        std::optional<std::filesystem::file_time_type> mtime_;
        bool                                           loaded_ = false;
    };

    // Whether the toggle key last left each app in Vietnamese (true) or English.
    using AppSwitchMap = std::map<std::string, bool>;

    // The fcitx5 edition's ngosen-app-switch.conf, an fcitx5 list: "[Switches/0]", "App=", "Vietnamese=".
    AppSwitchMap parseAppSwitches(const std::string& text);
    std::string  appSwitchesText(const AppSwitchMap& switches);

    class AppSwitches {
      public:
        explicit AppSwitches(std::string path, FileWriter write = writeFileAtomically);

        std::optional<bool> find(const std::string& app);
        // nullopt forgets the app, so it follows its mode or the last switch again.
        bool set(const std::string& app, std::optional<bool> vietnamese);

      private:
        void                                           reloadIfChanged();

        std::string                                    path_;
        FileWriter                                     write_;
        AppSwitchMap                                   switches_;
        std::optional<std::filesystem::file_time_type> mtime_;
        bool                                           loaded_ = false;
    };

    // The fcitx5 edition's ngosen-vim-apps.conf: one app per line, # for comments.
    using VimAppSet = std::set<std::string>;
    VimAppSet   parseVimApps(const std::string& text);
    std::string vimAppsText(const VimAppSet& apps);

    // The apps whose fields follow Vim's normal-mode keys, picked in the mode menu.
    class VimApps {
      public:
        explicit VimApps(std::string path, FileWriter write = writeFileAtomically);

        bool has(const std::string& app);
        // False when the file was not written.
        bool set(const std::string& app, bool on);

      private:
        void                                           reloadIfChanged();

        std::string                                    path_;
        FileWriter                                     write_;
        VimAppSet                                      apps_;
        std::optional<std::filesystem::file_time_type> mtime_;
        bool                                           loaded_ = false;
    };

} // namespace ngosen
