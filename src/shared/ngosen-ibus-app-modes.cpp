/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-app-modes.h"

#include "ngosen-log.h"
#include "ngosen-strings.h"

#include <sstream>
#include <system_error>
#include <utility>

namespace ngosen {

    namespace {

        constexpr int OffNumber     = 0;
        constexpr int SenNumber     = 2;
        constexpr int PreeditNumber = 5;

        int           numberOfMode(AppMode mode) {
            switch (mode) {
                case AppMode::Sen: return SenNumber;
                case AppMode::Preedit: return PreeditNumber;
                case AppMode::Off: break;
            }
            return OffNumber;
        }

        bool isNumber(const std::string& text) {
            return !text.empty() && text.size() <= 2 && text.find_first_not_of("0123456789") == std::string::npos;
        }

        std::optional<AppMode> modeOfNumber(int number) {
            // As fcitx5 reads the file: 1, 3, 4 and 8 are Sen too.
            switch (number) {
                case 1:
                case SenNumber:
                case 3:
                case 4:
                case 8: return AppMode::Sen;
                case PreeditNumber: return AppMode::Preedit;
                case OffNumber: return AppMode::Off;
                default: return std::nullopt;
            }
        }

        // The line's app, or empty for a comment or a line with no app.
        std::string appOfLine(const std::string& line) {
            const size_t equals = line.find('=');
            if (line.empty() || line[0] == '#' || equals == std::string::npos)
                return {};
            return modeAppName(trimIni(line.substr(0, equals)));
        }

    } // namespace

    std::string modeAppName(const std::string& program) {
        std::string name = stripDesktopSuffix(program);
        // As the core names an IBus client: "gtk3-im:firefox", and a snap's "firefox_firefox".
        const size_t colon = name.find(':');
        if (colon != std::string::npos && isEndsWith(name.substr(0, colon), "-im"))
            name.erase(0, colon + 1);
        const size_t underscore = name.find('_');
        if (underscore != std::string::npos && name.compare(0, underscore, name, underscore + 1) == 0)
            name.erase(underscore);
        if (name == "gnome-shell" || name == "QIBusInputContext" || name == "xim")
            return {};
        return name;
    }

    AppModeMap parseAppModes(const std::string& text) {
        AppModeMap         modes;
        std::istringstream lines(text);
        std::string        line;
        while (std::getline(lines, line)) {
            if (line.empty() || line[0] == '#')
                continue;
            const size_t      equals = line.find('=');
            const std::string number = equals == std::string::npos ? std::string() : trimIni(line.substr(equals + 1));
            if (!isNumber(number)) {
                NGOSEN_WARN("Skipped a line of the app modes: " + line);
                continue;
            }
            const std::string app = modeAppName(trimIni(line.substr(0, equals)));
            if (const auto mode = modeOfNumber(std::stoi(number)); mode && !app.empty())
                modes[app] = *mode;
        }
        return modes;
    }

    std::string appModesText(const AppModeMap& modes) {
        std::string text = "# The typing mode picked for each app: 0 = Off, 2 = Sen, 5 = Preedit\n";
        for (const auto& [app, mode] : modes)
            text += app + "=" + std::to_string(numberOfMode(mode)) + "\n";
        return text;
    }

    AppModes::AppModes(std::string path, FileWriter write) : path_(std::move(path)), write_(std::move(write)) {}

    void AppModes::reloadIfChanged() {
        std::error_code                                error;
        const auto                                     time = std::filesystem::last_write_time(path_, error);
        std::optional<std::filesystem::file_time_type> now;
        if (!error)
            now = time;
        if (loaded_ && now == mtime_)
            return;
        loaded_ = true;
        mtime_  = now;
        std::string text;
        modes_ = now && readFile(path_, text) ? parseAppModes(text) : AppModeMap{};
    }

    std::optional<AppMode> AppModes::find(const std::string& app) {
        if (app.empty())
            return std::nullopt;
        reloadIfChanged();
        const auto it = modes_.find(app);
        return it == modes_.end() ? std::nullopt : std::optional<AppMode>(it->second);
    }

    bool AppModes::set(const std::string& app, std::optional<AppMode> mode) {
        if (app.empty())
            return false;
        reloadIfChanged();
        // Only the app's own lines change, so comments and the modes fcitx5 keeps here (Off, Emoji) stay.
        std::string old;
        if (mtime_)
            readFile(path_, old);
        std::string        text;
        std::istringstream lines(old);
        for (std::string line; std::getline(lines, line);) {
            if (appOfLine(line) != app)
                text += line + "\n";
        }
        if (text.empty())
            text = appModesText({});
        if (mode)
            text += app + "=" + std::to_string(numberOfMode(*mode)) + "\n";
        if (!write_(path_, text))
            return false;
        modes_ = parseAppModes(text);
        // The write is ours; reading it back would change nothing.
        std::error_code error;
        if (const auto time = std::filesystem::last_write_time(path_, error); !error)
            mtime_ = time;
        return true;
    }

    AppSwitchMap parseAppSwitches(const std::string& text) {
        AppSwitchMap  switches;
        const IniFile ini = parseIni(text);
        // fcitx5 stops at the first missing index.
        for (size_t i = 0;; ++i) {
            const auto section = ini.sections.find("Switches/" + std::to_string(i));
            if (section == ini.sections.end())
                break;
            const auto        app        = section->second.find("App");
            const auto        vietnamese = section->second.find("Vietnamese");
            const std::string name       = app == section->second.end() ? std::string() : modeAppName(app->second);
            if (!name.empty() && vietnamese != section->second.end() && (vietnamese->second == "True" || vietnamese->second == "False"))
                switches[name] = vietnamese->second == "True";
        }
        return switches;
    }

    std::string appSwitchesText(const AppSwitchMap& switches) {
        std::string text  = "[Switches]\n";
        size_t      index = 0;
        for (const auto& [app, vietnamese] : switches)
            text += "[Switches/" + std::to_string(index++) + "]\nApp=" + escapeIniValue(app) + "\nVietnamese=" + (vietnamese ? "True" : "False") + "\n\n";
        return text;
    }

    AppSwitches::AppSwitches(std::string path, FileWriter write) : path_(std::move(path)), write_(std::move(write)) {}

    void AppSwitches::reloadIfChanged() {
        std::error_code                                error;
        const auto                                     time = std::filesystem::last_write_time(path_, error);
        std::optional<std::filesystem::file_time_type> now;
        if (!error)
            now = time;
        if (loaded_ && now == mtime_)
            return;
        loaded_ = true;
        mtime_  = now;
        std::string text;
        switches_ = now && readFile(path_, text) ? parseAppSwitches(text) : AppSwitchMap{};
    }

    std::optional<bool> AppSwitches::find(const std::string& app) {
        if (app.empty())
            return std::nullopt;
        reloadIfChanged();
        const auto it = switches_.find(app);
        return it == switches_.end() ? std::nullopt : std::optional<bool>(it->second);
    }

    bool AppSwitches::set(const std::string& app, std::optional<bool> vietnamese) {
        if (app.empty())
            return false;
        reloadIfChanged();
        AppSwitchMap changed = switches_;
        if (vietnamese)
            changed[app] = *vietnamese;
        else
            changed.erase(app);
        // Every tap of the toggle key and every pick in the menu ends here; an unchanged list needs no write.
        if (changed == switches_ && (mtime_ || changed.empty()))
            return true;
        if (!write_(path_, appSwitchesText(changed)))
            return false;
        switches_ = std::move(changed);
        // The write is ours; reading it back would change nothing.
        std::error_code error;
        if (const auto time = std::filesystem::last_write_time(path_, error); !error)
            mtime_ = time;
        return true;
    }

    VimAppSet parseVimApps(const std::string& text) {
        VimAppSet          apps;
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);) {
            // Named as the other app files name them, so a name fcitx5 saved as GNOME reports it matches.
            const std::string app = line.empty() || line[0] == '#' ? std::string() : modeAppName(trimIni(line));
            if (!app.empty())
                apps.insert(app);
        }
        return apps;
    }

    std::string vimAppsText(const VimAppSet& apps) {
        std::string text = "# Apps where Ngó Sen follows Vim's normal-mode keys, one per line\n";
        for (const auto& app : apps)
            text += app + "\n";
        return text;
    }

    VimApps::VimApps(std::string path, FileWriter write) : path_(std::move(path)), write_(std::move(write)) {}

    void VimApps::reloadIfChanged() {
        std::error_code                                error;
        const auto                                     time = std::filesystem::last_write_time(path_, error);
        std::optional<std::filesystem::file_time_type> now;
        if (!error)
            now = time;
        if (loaded_ && now == mtime_)
            return;
        loaded_ = true;
        mtime_  = now;
        std::string text;
        apps_ = now && readFile(path_, text) ? parseVimApps(text) : VimAppSet{};
    }

    bool VimApps::has(const std::string& app) {
        if (app.empty())
            return false;
        reloadIfChanged();
        return apps_.count(app) > 0;
    }

    bool VimApps::set(const std::string& app, bool on) {
        if (app.empty())
            return false;
        reloadIfChanged();
        VimAppSet changed = apps_;
        if (on)
            changed.insert(app);
        else
            changed.erase(app);
        if (changed == apps_ && (mtime_ || changed.empty()))
            return true;
        if (!write_(path_, vimAppsText(changed)))
            return false;
        apps_ = std::move(changed);
        std::error_code error;
        if (const auto time = std::filesystem::last_write_time(path_, error); !error)
            mtime_ = time;
        return true;
    }

} // namespace ngosen
