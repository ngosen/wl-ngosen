/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-config.h"

#include "bamboo-core.h"
#include "ngosen-ibus-keys.h"
#include "ngosen-log.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <glib.h>
#include <sstream>
#include <unistd.h>
#include <utility>

namespace ngosen {

    std::string trimIni(const std::string& text) {
        const auto begin = text.find_first_not_of(" \t\r\f\v");
        if (begin == std::string::npos)
            return "";
        const auto end = text.find_last_not_of(" \t\r\f\v");
        return text.substr(begin, end - begin + 1);
    }

    namespace {

        constexpr char EscapedChars[]  = "\\\"\n\f\r\t\v";
        constexpr char EscapeLetters[] = "\\\"nfrtv";

        // As fcitx5 reads a value: only a quoted one has escapes, and a bad escape drops it.
        bool unescapeIniValue(const std::string& value, std::string& out) {
            if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
                out = value;
                return true;
            }
            out.clear();
            for (size_t i = 1; i + 1 < value.size(); ++i) {
                if (value[i] == '"')
                    return false;
                if (value[i] != '\\') {
                    out += value[i];
                    continue;
                }
                const char* letter = ++i + 1 < value.size() ? std::strchr(EscapeLetters, value[i]) : nullptr;
                if (letter == nullptr || *letter == '\0')
                    return false;
                out += EscapedChars[letter - EscapeLetters];
            }
            return true;
        }

        std::vector<std::string> takeNames(char** names) {
            std::vector<std::string> result;
            if (names == nullptr)
                return result;
            for (size_t i = 0; names[i] != nullptr; ++i) { //NOLINT
                result.emplace_back(names[i]);             //NOLINT
                free(names[i]);                            //NOLINT
            }
            free(names); //NOLINT
            return result;
        }

        bool contains(const std::vector<std::string>& list, const std::string& value) {
            return std::find(list.begin(), list.end(), value) != list.end();
        }

        // One line, so a key appended to the end of the file stays a top-level key. fcitx5 keeps a list
        // in its own section, which is read when importing its settings.
        const char* const ToggleKeyName        = "ToggleKey";
        const char* const ModeMenuKeyName      = "ModeMenuKey";
        const char* const CycleModeKeyName     = "CycleModeKey";
        const char* const AnnouncedVersionName = "AnnouncedVersion";

        // The items each menu shows, as fcitx5 names their switches.
        const std::pair<const char*, bool IBusSettings::*> ShownItems[] = {
            {"ShowModeSen", &IBusSettings::showModeSen},
            {"ShowModePreedit", &IBusSettings::showModePreedit},
            {"ShowModeOff", &IBusSettings::showModeOff},
            {"ShowModeVim", &IBusSettings::showModeVim},
            {"ShowMenuInputMethod", &IBusSettings::showMenuInputMethod},
            {"ShowMenuCharset", &IBusSettings::showMenuCharset},
            {"ShowMenuSpellCheck", &IBusSettings::showMenuSpellCheck},
            {"ShowMenuMacro", &IBusSettings::showMenuMacro},
            {"ShowMenuCapitalizeMacro", &IBusSettings::showMenuCapitalizeMacro},
            {"ShowMenuAutoCapitalize", &IBusSettings::showMenuAutoCapitalize},
            {"ShowMenuAutoRestore", &IBusSettings::showMenuAutoRestore},
            {"ShowMenuDictionary", &IBusSettings::showMenuDictionary},
        };

        std::vector<std::string> splitWords(const std::string& text) {
            std::istringstream       words(text);
            std::vector<std::string> out;
            for (std::string word; words >> word;)
                out.push_back(word);
            return out;
        }

        // A line of keys, or fcitx5's numbered list. A list with only keys the engine cannot use falls back
        // to the default; an empty one turns the key off.
        template <typename Parse>
        std::vector<std::string> readKeys(const IniFile& ini, const char* name, const std::vector<std::string>& defaults, Parse parse) {
            std::vector<std::string> keys;
            if (const auto line = ini.top.find(name); line != ini.top.end())
                keys = splitWords(line->second);
            else if (ini.sections.count(name) != 0)
                keys = iniKeyList(ini, name);
            else
                return defaults;
            // fcitx5 writes a cleared key as an empty entry, which matches nothing.
            keys.erase(std::remove(keys.begin(), keys.end(), std::string()), keys.end());
            std::vector<std::string> usable;
            for (const auto& key : keys) {
                if (parse(key))
                    usable.push_back(key);
            }
            return usable.empty() && !keys.empty() ? defaults : usable;
        }

        // fcitx5 allows a single key, which the IBus edition does not: a bare Shift would switch on every
        // capital.
        std::vector<std::string> readToggleKeys(const IniFile& ini) {
            return readKeys(ini, ToggleKeyName, defaultToggleKeys(), parseToggleKey);
        }

        std::vector<std::string> readModeMenuKeys(const IniFile& ini) {
            return readKeys(ini, ModeMenuKeyName, defaultModeMenuKeys(), parseMenuKey);
        }

        std::string joinWords(const std::vector<std::string>& words) {
            std::string line;
            for (const auto& word : words)
                line += (line.empty() ? "" : " ") + word;
            return line;
        }

        // Default Typing is no longer a mode, but configs that name it must still load.
        std::string withoutDefaultTyping(const std::string& order) {
            std::string        kept;
            std::istringstream names(order);
            for (std::string name; std::getline(names, name, ',');) {
                if (trimIni(name) != "Default")
                    kept += (kept.empty() ? "" : ",") + name;
            }
            return kept;
        }

        const char* const ModeNames[]        = {"Disabled", "Non-Start", "Everywhere"};
        const char* const LockWordKeyNames[] = {"Disabled", "Shift", "Ctrl", "Alt"};

        struct Reader {
            const std::map<std::string, std::string>& values;

            const std::string*                        find(const char* key) const {
                const auto it = values.find(key);
                return it == values.end() ? nullptr : &it->second;
            }
            void boolean(const char* key, bool& out) const {
                if (const auto* value = find(key); value != nullptr && (*value == "True" || *value == "False"))
                    out = *value == "True";
            }
            void text(const char* key, std::string& out) const {
                if (const auto* value = find(key))
                    out = *value;
            }
            // A key whose name is its own letter or digit, so the menu shows what to press.
            void shortcut(const char* key, std::string& out) const {
                // Lowercase, since the engine matches the key as typed without Shift.
                if (const auto* value = find(key); value != nullptr && value->size() == 1 && std::isalnum(static_cast<unsigned char>((*value)[0])) != 0)
                    out = std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>((*value)[0]))));
            }
            void choice(const char* key, std::string& out, const std::vector<std::string>& allowed) const {
                if (const auto* value = find(key); value != nullptr && contains(allowed, *value))
                    out = *value;
            }
            template <size_t N>
            void index(const char* key, int& out, const char* const (&names)[N]) const {
                if (const auto* value = find(key)) {
                    for (size_t i = 0; i < N; ++i) {
                        if (*value == names[i])
                            out = static_cast<int>(i);
                    }
                }
            }
        };

        const char* boolText(bool value) {
            return value ? "True" : "False";
        }

        bool fileExists(const std::string& path) {
            std::error_code error;
            return std::filesystem::exists(path, error);
        }

    } // namespace

    const std::vector<std::string>& inputMethodNames() {
        static const std::vector<std::string> names = [] {
            auto list = takeNames(GetInputMethodNames());
            list.emplace_back("Custom");
            return list;
        }();
        return names;
    }

    const std::vector<std::string>& charsetNames() {
        static const std::vector<std::string> names = takeNames(GetCharsetNames());
        return names;
    }

    IniFile parseIni(const std::string& text) {
        IniFile                             ini;
        std::map<std::string, std::string>* current = &ini.top;
        std::istringstream                  lines(text);
        std::string                         line;
        while (std::getline(lines, line)) {
            line = trimIni(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line.front() == '[' && line.back() == ']') {
                current = &ini.sections[line.substr(1, line.size() - 2)];
                continue;
            }
            const auto equal = line.find('=');
            if (equal == std::string::npos)
                continue;
            std::string value;
            if (unescapeIniValue(line.substr(equal + 1), value))
                (*current)[line.substr(0, equal)] = value;
        }
        return ini;
    }

    std::string escapeIniValue(const std::string& value) {
        if (value.find_first_of(" \f\r\t\v\"\\\n") == std::string::npos)
            return value;
        std::string out = "\"";
        for (const char c : value) {
            if (const char* escaped = c != '\0' ? std::strchr(EscapedChars, c) : nullptr) {
                out += '\\';
                out += EscapeLetters[escaped - EscapedChars];
            } else {
                out += c;
            }
        }
        return out + "\"";
    }

    std::vector<KeymapEntry> iniList(const IniFile& ini, const std::string& name) {
        // fcitx5 stops at the first missing index too.
        std::vector<KeymapEntry> entries;
        for (size_t i = 0;; ++i) {
            const auto it = ini.sections.find(name + "/" + std::to_string(i));
            if (it == ini.sections.end())
                return entries;
            const auto key   = it->second.find("Key");
            const auto value = it->second.find("Value");
            entries.push_back({key != it->second.end() ? key->second : "", value != it->second.end() ? value->second : ""});
        }
    }

    std::vector<std::string> iniKeyList(const IniFile& ini, const std::string& name) {
        std::vector<std::string> keys;
        const auto               section = ini.sections.find(name);
        if (section == ini.sections.end())
            return keys;
        // fcitx5 stops at the first missing index.
        for (auto it = section->second.find("0"); it != section->second.end(); it = section->second.find(std::to_string(keys.size())))
            keys.push_back(it->second);
        return keys;
    }

    IBusSettings defaultSettings() {
        IBusSettings settings;
        settings.toggleKeys   = defaultToggleKeys();
        settings.modeMenuKeys = defaultModeMenuKeys();
        return settings;
    }

    IBusSettings readSettings(const IniFile& ini) {
        IBusSettings settings = defaultSettings();
        auto&        o        = settings.options;
        const Reader read{ini.top};
        read.choice("InputMethod", o.inputMethod, inputMethodNames());
        read.choice("OutputCharset", o.outputCharset, charsetNames());
        read.boolean("SpellCheck", o.spellCheck);
        read.boolean("ModernStyle", o.modernStyle);
        read.boolean("FreeMarking", o.freeMarking);
        read.index("W2U", o.w2u, ModeNames);
        read.index("BracketTransform", o.bracketTransform, ModeNames);
        read.boolean("AutoNonVnRestore", o.autoNonVnRestore);
        read.boolean("DdFreeStyle", o.ddFreeStyle);
        read.boolean("BackspaceRemovesTone", o.backspaceRemovesTone);
        read.boolean("RetoneSelection", o.retoneSelection);
        read.boolean("RetypeAfterSwitch", o.retypeAfterSwitch);
        read.boolean("EnableMacro", o.enableMacro);
        read.boolean("CapitalizeMacro", o.capitalizeMacro);
        read.boolean("EnableMacroInOffMode", o.enableMacroInOffMode);
        int lockWordKey = static_cast<int>(o.lockWordKey);
        read.index("LockWordModifier", lockWordKey, LockWordKeyNames);
        o.lockWordKey = static_cast<LockWordKey>(lockWordKey);
        read.boolean("AutoCapitalizeAfterPunctuation", o.autoCapitalizeAfterPunctuation);
        read.boolean("CapitalizeStartOfText", o.capitalizeStartOfText);
        read.boolean("DoubleSpaceToPeriod", o.doubleSpaceToPeriod);
        read.boolean("DoubleHyphenToEmDash", o.doubleHyphenToEmDash);
        read.text("TimeFormat", o.timeFormat);
        read.text("DateFormat", o.dateFormat);
        read.boolean("EnableDictionary", settings.enableDictionary);
        read.boolean("EnableCustomKeymap", settings.enableCustomKeymap);
        read.boolean("DirectTyping", settings.directTyping);
        settings.toggleKeys   = readToggleKeys(ini);
        settings.modeMenuKeys = readModeMenuKeys(ini);
        read.shortcut("ShortcutSen", settings.shortcutSen);
        read.shortcut("ShortcutPreedit", settings.shortcutPreedit);
        read.shortcut("ShortcutOff", settings.shortcutOff);
        read.shortcut("ShortcutVim", settings.shortcutVim);
        for (const auto& [key, shown] : ShownItems)
            read.boolean(key, settings.*shown);
        read.text("ModeOrder", settings.modeOrder);
        settings.modeOrder = withoutDefaultTyping(settings.modeOrder);
        read.text("UiLanguage", settings.uiLanguage);
        settings.cycleModeKeys = readKeys(ini, CycleModeKeyName, {}, parseMenuKey);
        return settings;
    }

    std::string settingsText(const IBusSettings& settings) {
        const auto&        o = settings.options;
        std::ostringstream out;
        const auto         line = [&out](const char* key, const std::string& value) { out << key << '=' << escapeIniValue(value) << '\n'; };
        line("InputMethod", o.inputMethod);
        line("OutputCharset", o.outputCharset);
        line("SpellCheck", boolText(o.spellCheck));
        line("ModernStyle", boolText(o.modernStyle));
        line("FreeMarking", boolText(o.freeMarking));
        line("W2U", ModeNames[o.w2u]);
        line("BracketTransform", ModeNames[o.bracketTransform]);
        line("AutoNonVnRestore", boolText(o.autoNonVnRestore));
        line("DdFreeStyle", boolText(o.ddFreeStyle));
        line("BackspaceRemovesTone", boolText(o.backspaceRemovesTone));
        line("RetoneSelection", boolText(o.retoneSelection));
        line("RetypeAfterSwitch", boolText(o.retypeAfterSwitch));
        line("EnableMacro", boolText(o.enableMacro));
        line("CapitalizeMacro", boolText(o.capitalizeMacro));
        line("EnableMacroInOffMode", boolText(o.enableMacroInOffMode));
        line("LockWordModifier", LockWordKeyNames[static_cast<int>(o.lockWordKey)]);
        line("AutoCapitalizeAfterPunctuation", boolText(o.autoCapitalizeAfterPunctuation));
        line("CapitalizeStartOfText", boolText(o.capitalizeStartOfText));
        line("DoubleSpaceToPeriod", boolText(o.doubleSpaceToPeriod));
        line("DoubleHyphenToEmDash", boolText(o.doubleHyphenToEmDash));
        line("TimeFormat", o.timeFormat);
        line("DateFormat", o.dateFormat);
        line("EnableDictionary", boolText(settings.enableDictionary));
        line("EnableCustomKeymap", boolText(settings.enableCustomKeymap));
        line("DirectTyping", boolText(settings.directTyping));
        line(ToggleKeyName, joinWords(settings.toggleKeys));
        line(ModeMenuKeyName, joinWords(settings.modeMenuKeys));
        line("ShortcutSen", settings.shortcutSen);
        line("ShortcutPreedit", settings.shortcutPreedit);
        line("ShortcutOff", settings.shortcutOff);
        line("ShortcutVim", settings.shortcutVim);
        for (const auto& [key, shown] : ShownItems)
            line(key, boolText(settings.*shown));
        line("ModeOrder", settings.modeOrder);
        line("UiLanguage", settings.uiLanguage);
        line(CycleModeKeyName, joinWords(settings.cycleModeKeys));
        return out.str();
    }

    std::string withSetting(const std::string& text, const std::string& key, const std::string& value) {
        const std::string  setting = key + '=' + escapeIniValue(value);
        std::istringstream lines(text);
        std::ostringstream out;
        std::string        line;
        bool               top = true;
        bool               set = false;
        while (std::getline(lines, line)) {
            const std::string trimmed = trimIni(line);
            const auto        equal   = trimmed.find('=');
            if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
                // A key after a section header would belong to that section.
                if (top && !set)
                    out << setting << '\n';
                top = false;
                set = true;
            } else if (top && equal != std::string::npos && trimmed[0] != '#' && trimmed.substr(0, equal) == key) {
                // Every copy, since parseIni() keeps the last one.
                line = setting;
                set  = true;
            }
            out << line << '\n';
        }
        if (!set)
            out << setting << '\n';
        return out.str();
    }

    SettingsPaths SettingsPaths::fromEnvironment() {
        const std::string config = g_get_user_config_dir();
        const std::string data   = g_get_user_data_dir();
        return {config + "/ngosen", data + "/ngosen", config + "/fcitx5/conf", data + "/fcitx5/ngosen", std::string(g_get_user_state_dir()) + "/ngosen"};
    }

    std::string SettingsPaths::settingsFile() const {
        return configDir + "/ngosen.conf";
    }
    std::string SettingsPaths::macroFile() const {
        return configDir + "/ngosen-macro-table.conf";
    }
    std::string SettingsPaths::keymapFile() const {
        return configDir + "/ngosen-custom-keymap.conf";
    }
    std::string SettingsPaths::dictionaryFile() const {
        return dataDir + "/vietnamese.cm.dict";
    }
    // The fcitx5 edition's name, so the two files look alike.
    std::string SettingsPaths::appModesFile() const {
        return configDir + "/ngosen-app-rules.conf";
    }
    std::string SettingsPaths::appSwitchesFile() const {
        return configDir + "/ngosen-app-switch.conf";
    }
    std::string SettingsPaths::vimAppsFile() const {
        return configDir + "/ngosen-vim-apps.conf";
    }
    std::string SettingsPaths::importedMark() const {
        return configDir + "/.fcitx5-imported";
    }

    std::string SettingsPaths::senDefaultMark() const {
        return configDir + "/.sen-default";
    }

    std::string SettingsPaths::englishMark() const {
        return stateDir + "/english";
    }

    bool readEnglishOn(const SettingsPaths& paths) {
        return fileExists(paths.englishMark());
    }

    bool saveEnglishOn(const SettingsPaths& paths, bool on) {
        // The typing log shares the folder, which only the user may read. An empty marker needs no
        // sync: losing it in a crash only starts the next session in Vietnamese.
        if (on) {
            const int fd = g_mkdir_with_parents(paths.stateDir.c_str(), 0700) == 0 ? ::open(paths.englishMark().c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0600) : -1;
            if (fd == -1) {
                NGOSEN_WARN("Cannot write " + paths.englishMark() + ": " + std::strerror(errno));
                return false;
            }
            ::close(fd);
            return true;
        }
        std::error_code error;
        std::filesystem::remove(paths.englishMark(), error);
        if (error)
            NGOSEN_WARN("Cannot remove " + paths.englishMark() + ": " + error.message());
        return !error;
    }

    std::string SettingsPaths::extensionEnabledMark() const {
        return configDir + "/.forward-keys-enabled";
    }

    std::string SettingsPaths::duplicateLayoutMark() const {
        return configDir + "/.us-source-removed";
    }

    bool readFile(const std::string& path, std::string& content) {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
        return true;
    }

    bool writeFileAtomically(const std::string& path, const std::string& content) {
        const std::filesystem::path target(path);
        std::error_code             error;
        std::filesystem::create_directories(target.parent_path(), error);
        std::string temp = path + ".XXXXXX";
        const int   fd   = ::mkostemp(temp.data(), O_CLOEXEC);
        if (fd == -1) {
            NGOSEN_WARN("Cannot write " + path + ": " + std::strerror(errno));
            return false;
        }
        size_t written = 0;
        while (written < content.size()) {
            const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
            if (n > 0)
                written += static_cast<size_t>(n);
            else if (n == 0 || errno != EINTR)
                break;
        }
        const bool ok = written == content.size() && ::fsync(fd) == 0;
        ::close(fd);
        if (!ok || ::rename(temp.c_str(), path.c_str()) != 0) {
            NGOSEN_WARN("Cannot write " + path + ": " + std::strerror(errno));
            ::unlink(temp.c_str());
            return false;
        }
        return true;
    }

    namespace {
        // fcitx5 keeps Sen or the underline in Mode; Off types with Sen while Vietnamese is on, and there is no
        // emoji picker here, so it underlines.
        void readFcitxMode(const IniFile& fcitx, IBusSettings& settings) {
            const auto mode = fcitx.top.find("Mode");
            if (mode == fcitx.top.end())
                return;
            std::string name = mode->second;
            for (char& c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name == "preedit" || name == "emoji picker")
                settings.directTyping = false;
            else if (name == "sen" || name == "off")
                settings.directTyping = true;
        }
    } // namespace

    bool prepareSettings(const SettingsPaths& paths, const FileWriter& write) {
        const bool  imported = fileExists(paths.importedMark());
        std::string content;
        if (!imported) {
            const std::pair<std::string, std::string> copies[] = {
                {paths.fcitxDataDir + "/vietnamese.cm.dict", paths.dictionaryFile()},        {paths.fcitxConfigDir + "/ngosen-macro-table.conf", paths.macroFile()},
                {paths.fcitxConfigDir + "/ngosen-custom-keymap.conf", paths.keymapFile()},   {paths.fcitxConfigDir + "/ngosen-app-rules.conf", paths.appModesFile()},
                {paths.fcitxConfigDir + "/ngosen-app-switch.conf", paths.appSwitchesFile()}, {paths.fcitxConfigDir + "/ngosen-vim-apps.conf", paths.vimAppsFile()},
            };
            for (const auto& [from, to] : copies) {
                if (!fileExists(to) && readFile(from, content) && !write(to, content))
                    return false;
            }
        }
        if (!fileExists(paths.settingsFile())) {
            IBusSettings settings = defaultSettings();
            if (!imported && readFile(paths.fcitxConfigDir + "/ngosen.conf", content)) {
                NGOSEN_INFO("Importing the settings of the fcitx5 edition");
                const IniFile fcitx = parseIni(content);
                settings            = readSettings(fcitx);
                readFcitxMode(fcitx, settings);
            }
            // A new file already holds the mode wanted, so the IBus edition never switches it. Its
            // release notice is left to the IBus edition.
            if (!write(paths.senDefaultMark(), "") || !write(paths.settingsFile(), settingsText(settings)))
                return false;
        }
        return imported || write(paths.importedMark(), "");
    }

    namespace {
        std::pair<int, int> releaseNumbers(const std::string& version) {
            int major = 0;
            int minor = 0;
            std::sscanf(version.c_str(), "%d.%d", &major, &minor);
            return {major, minor};
        }
    } // namespace

    std::string featureRelease(const std::string& version) {
        const auto [major, minor] = releaseNumbers(version);
        return std::to_string(major) + "." + std::to_string(minor);
    }

    std::string releaseToAnnounce(const SettingsPaths& paths, const std::string& version, const FileWriter& write) {
        std::string text;
        if (!readFile(paths.settingsFile(), text))
            return {};
        const IniFile ini       = parseIni(text);
        const auto    announced = ini.top.find(AnnouncedVersionName);
        if (announced != ini.top.end() && releaseNumbers(announced->second) >= releaseNumbers(version))
            return {};
        const std::string release = featureRelease(version);
        // Recorded first: a notice that could not be recorded would come back at every login.
        return write(paths.settingsFile(), withSetting(text, AnnouncedVersionName, release)) ? release : std::string();
    }

} // namespace ngosen
