/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bamboo-core.h"
#include "ngosen-ibus-config.h"
#include "ngosen-ibus-keys.h"
#include "ngosen-masks.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <cstdio>
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

    std::string read(const std::string& path) {
        std::string text;
        return readFile(path, text) ? text : "<missing>";
    }

    void write(const std::string& path, const std::string& text) {
        fs::create_directories(fs::path(path).parent_path());
        check(writeFileAtomically(path, text), "write " + path);
    }

    bool sameOptions(const IBusSettings& a, const IBusSettings& b) {
        return settingsText(a) == settingsText(b);
    }

    SettingsPaths pathsIn(const fs::path& root) {
        return {root / "config/ngosen", root / "data/ngosen", root / "config/fcitx5/conf", root / "data/fcitx5/ngosen"};
    }

    // As the fcitx5 edition writes them: defaults are commented out.
    const char* const FcitxSettings = "# Mode\n# Mode=Sen\nMode=Preedit\nInputMethod=VNI\nSettingsVersion=3\n"
                                      "DoubleSpaceToPeriod=True\nVietnameseOn=True\n# W2U=Non-Start\nW2U=Everywhere\nRetoneSelection=False\n"
                                      "RetypeAfterSwitch=True\nDoubleHyphenToEmDash=True\nEnableMacroInOffMode=True\n";
    const char* const FcitxMacros   = "[Macro]\n[Macro/0]\nKey=ko\nValue=không\n[Macro/1]\nKey=vn\nValue=\"Việt Nam\"\n";

    void              testValues() {
        for (const std::string value : {"không", "a b", "x\"y", "back\\slash", "line\nbreak", "", "  lead", "tab\tin", "100%"}) {
            const IniFile ini = parseIni("Key=" + escapeIniValue(value) + "\n");
            const auto    it  = ini.top.find("Key");
            check(it != ini.top.end() && it->second == value, "value round trip: " + value);
        }
        check(parseIni("Key=\"a\\qb\"\n").top.count("Key") == 0, "an unknown escape drops the value, as in fcitx5");
        check(parseIni("Key=a\\nb\n").top.at("Key") == "a\\nb", "an unquoted value has no escapes, as in fcitx5");
        check(parseIni("Key=  spaced  \n").top.at("Key") == "  spaced", "spaces at the ends of a line are dropped");
    }

    void testLists() {
        const auto macros = iniList(parseIni(FcitxMacros), "Macro");
        check(macros.size() == 2 && macros[0].key == "ko" && macros[0].value == "không" && macros[1].value == "Việt Nam", "macro list");
        const auto gap = iniList(parseIni("[Macro/0]\nKey=a\nValue=b\n[Macro/2]\nKey=c\nValue=d\n"), "Macro");
        check(gap.size() == 1, "a list stops at the first missing index, as in fcitx5");
    }

    void testSettings() {
        const IBusSettings defaults = defaultSettings();
        check(sameOptions(readSettings(parseIni("")), defaults), "an empty file reads as the defaults");
        IBusSettings expected = defaults;
        expected.options      = defaultOptions;
        check(sameOptions(defaults, expected), "the defaults are the fcitx5 edition's");
        check(defaults.options.retypeAfterSwitch, "Cứu chữ is on by default, as in fcitx5");
        check(defaults.options.retoneSelection, "RetoneSelection is on by default, as in fcitx5");

        const IBusSettings read = readSettings(parseIni(FcitxSettings));
        check(read.options.inputMethod == "VNI", "InputMethod is read");
        check(read.options.w2u == 2, "W2U is read by name");
        check(read.options.doubleSpaceToPeriod && read.options.doubleHyphenToEmDash, "the punctuation options are read");
        check(!defaults.options.enableMacroInOffMode, "macros in English are off by default");
        check(read.options.enableMacroInOffMode, "EnableMacroInOffMode is read");
        check(read.options.retypeAfterSwitch, "RetypeAfterSwitch is read");
        check(!read.options.retoneSelection, "RetoneSelection is read");

        const IBusSettings odd = readSettings(parseIni("InputMethod=Nope\nSpellCheck=yes\nOutputCharset=Nope\nLockWordModifier=Disabled\n"));
        check(odd.options.inputMethod == "Telex" && odd.options.spellCheck && odd.options.outputCharset == "Unicode", "unknown values keep the defaults");
        check(odd.options.lockWordKey == LockWordKey::None, "LockWordModifier is read by name");

        IBusSettings changed                 = defaults;
        changed.options.inputMethod          = "VNI";
        changed.options.outputCharset        = "TCVN3 (ABC)";
        changed.options.bracketTransform     = 1;
        changed.options.lockWordKey          = LockWordKey::Alt;
        changed.options.timeFormat           = "%H giờ %M";
        changed.options.spellCheck           = false;
        changed.options.retoneSelection      = false;
        changed.options.retypeAfterSwitch    = false;
        changed.options.doubleSpaceToPeriod  = true;
        changed.options.doubleHyphenToEmDash = true;
        changed.options.enableMacroInOffMode = true;
        changed.enableDictionary             = true;
        changed.enableCustomKeymap           = true;
        changed.directTyping                 = true;
        const std::string text               = settingsText(changed);
        check(sameOptions(readSettings(parseIni(text)), changed), "settings round trip");
        check(!readSettings(parseIni(text)).options.retoneSelection, "RetoneSelection round trip");
        check(!readSettings(parseIni(text)).options.retypeAfterSwitch, "RetypeAfterSwitch round trip");
        check(readSettings(parseIni(text)).options.enableMacroInOffMode, "EnableMacroInOffMode round trip");
        check(parseIni(text).top.size() == 46, "the file has every key");
        // fcitx5 reads SettingsVersion as the version of its own upgrades.
        check(parseIni(text).top.count("SettingsVersion") == 0, "no SettingsVersion");
    }

    void testOneSetting() {
        const std::string mine = "# my note\nInputMethod=Telex\nSpellCheck=True\n";
        check(withSetting(mine, "SpellCheck", "False") == "# my note\nInputMethod=Telex\nSpellCheck=False\n", "one key changes, the rest stays");
        check(withSetting(mine, "EnableMacro", "True") == mine + "EnableMacro=True\n", "a missing key is added");
        check(withSetting("#SpellCheck=True\n", "SpellCheck", "False") == "#SpellCheck=True\nSpellCheck=False\n", "a commented key is left alone");
        check(readSettings(parseIni(withSetting("SpellCheck=True\nSpellCheck=True\n", "SpellCheck", "False"))).options.spellCheck == false, "every copy of the key changes");
        check(withSetting("A=1\n[Macro/0]\nKey=x\n", "SpellCheck", "False") == "A=1\nSpellCheck=False\n[Macro/0]\nKey=x\n", "a new key goes before the first section");
        check(withSetting("[S]\nSpellCheck=True\n", "SpellCheck", "False") == "SpellCheck=False\n[S]\nSpellCheck=True\n", "a key inside a section is not the setting");
        check(parseIni(withSetting("", "OutputCharset", "TCVN3 (ABC)")).top.at("OutputCharset") == "TCVN3 (ABC)", "a value with spaces is quoted");
    }

    void testToggleKeys() {
        check(readSettings(parseIni("")).toggleKeys == defaultToggleKeys(), "no toggle key reads as Ctrl+Shift");
        check(readSettings(parseIni("ToggleKey=\n")).toggleKeys.empty(), "an empty toggle key turns the key off");
        check(readSettings(parseIni("ToggleKey=Alt+z Control+space\n")).toggleKeys == std::vector<std::string>{"Alt+z", "Control+space"}, "toggle keys are read");
        // fcitx5 allows these; here a bare Shift would switch on every capital.
        check(readSettings(parseIni("ToggleKey=Shift_L Zenkaku_Hankaku\n")).toggleKeys == defaultToggleKeys(), "single keys fall back to the default");
        check(readSettings(parseIni("ToggleKey=Shift_L Alt+z Nope+x\n")).toggleKeys == std::vector<std::string>{"Alt+z"}, "single and unknown keys are dropped");
        const std::vector<std::string> shiftAlt{"Shift+Alt_L", "Shift+Alt_R", "Alt+Shift_L", "Alt+Shift_R"};
        check(readSettings(parseIni("ToggleKey=Shift+Alt_L Shift+Alt_R Alt+Shift_L Alt+Shift_R\n")).toggleKeys == shiftAlt, "the Shift+Alt choice is read whole");

        IBusSettings changed = defaultSettings();
        changed.toggleKeys   = {"Alt+z", "Control+space"};
        check(readSettings(parseIni(settingsText(changed))).toggleKeys == changed.toggleKeys, "toggle keys round trip");
        changed.toggleKeys = {};
        check(readSettings(parseIni(settingsText(changed))).toggleKeys.empty(), "a key turned off stays off");
        check(parseIni(settingsText(defaultSettings()) + "SpellCheck=False\n").top.at("SpellCheck") == "False", "a key appended to the file is still a setting");

        const char* const fcitxKeys = "[ToggleKey]\n0=Alt+z\n2=Control+space\n";
        check(readSettings(parseIni(std::string(FcitxSettings) + fcitxKeys)).toggleKeys == std::vector<std::string>{"Alt+z"},
              "the fcitx5 edition's key list is imported, up to its first missing index");
        check(readSettings(parseIni(std::string(FcitxSettings) + "[ToggleKey]\n")).toggleKeys.empty(), "a key turned off in the fcitx5 edition stays off");
        check(readSettings(parseIni(std::string(FcitxSettings) + "[ToggleKey]\n0=\n")).toggleKeys.empty(), "an empty fcitx5 key entry turns the key off");
    }

    void testModeMenuSettings() {
        const IBusSettings defaults = readSettings(parseIni(""));
        check(defaults.modeMenuKeys == std::vector<std::string>{"grave"}, "the mode menu opens with ` by default, as in fcitx5");
        check(defaults.shortcutSen == "1" && defaults.shortcutPreedit == "q" && defaults.shortcutOff == "e", "menu shortcuts default to fcitx5's");
        check(readSettings(parseIni("ModeMenuKey=Shift+asciitilde\n")).modeMenuKeys == std::vector<std::string>{"Shift+asciitilde"}, "Shift+~ opens the menu");
        check(readSettings(parseIni("ModeMenuKey=\n")).modeMenuKeys.empty(), "an empty menu key turns the menu off");
        check(readSettings(parseIni("ModeMenuKey=Shift_L Nope\n")).modeMenuKeys == std::vector<std::string>{"grave"}, "a lone modifier falls back to `");
        check(readSettings(parseIni(std::string(FcitxSettings) + "[ModeMenuKey]\n0=apostrophe\n")).modeMenuKeys == std::vector<std::string>{"apostrophe"},
              "the fcitx5 edition's menu key list is imported");
        const IBusSettings shortcuts = readSettings(parseIni("ShortcutSen=a\nShortcutPreedit=semicolon\nShortcutOff=\n"));
        check(shortcuts.shortcutSen == "a" && shortcuts.shortcutPreedit == "q" && shortcuts.shortcutOff == "e", "a shortcut is one letter or digit");
        check(readSettings(parseIni("ShortcutOff=X\n")).shortcutOff == "x", "a shortcut written in capitals is read in lowercase");
        IBusSettings changed  = defaults;
        changed.modeMenuKeys  = {};
        changed.shortcutOff   = "x";
        changed.shortcutVim   = "m";
        changed.showModeVim   = false;
        const IBusSettings rt = readSettings(parseIni(settingsText(changed)));
        check(rt.modeMenuKeys.empty() && rt.shortcutOff == "x", "menu settings round trip");
        check(rt.shortcutVim == "m" && !rt.showModeVim, "the Vim item's settings round trip");
        check(defaults.shortcutVim == "v" && defaults.showModeVim, "Vim mode is in the menu on v by default, as in fcitx5");
        const IBusSettings old = readSettings(parseIni("ShortcutDefault=r\nShowModeDefault=False\nModeOrder=Sen,Preedit,Emoji,Off,Default\n"));
        check(old.modeOrder == "Sen,Preedit,Emoji,Off" && old.showModeSen && old.shortcutSen == "1", "a config saved with Default Typing still reads");
        check(settingsText(old).find("Default") == std::string::npos, "and is saved without it");
    }

    bool matches(const std::string& key, guint keyval, guint state) {
        const auto chord = parseToggleKey(key);
        return chord && matchesKey(*chord, keyval, state);
    }

    void testKeyMatching() {
        check(!parseToggleKey("Shift_L") && !parseToggleKey("Shift+Shift_L") && !parseToggleKey("space") && !parseToggleKey("Nope+x"), "single and unknown keys");
        check(!parseToggleKey("") && !parseMenuKey(""), "an empty key");
        check(matches("Control+Shift_L", XKB_KEY_Shift_L, ControlMask), "Ctrl held, Shift pressed");
        check(matches("Control+Shift_L", XKB_KEY_Shift_L, ControlMask | ShiftMask | ReleaseMask), "Shift released with Ctrl held");
        check(matches("Control+Shift_L", XKB_KEY_Shift_L, ControlMask | Mod2Mask | LockMask), "NumLock and Caps Lock do not count");
        check(!matches("Control+Shift_L", XKB_KEY_Shift_R, ControlMask), "the other Shift is another entry");
        check(!matches("Control+Shift_L", XKB_KEY_Shift_L, ControlMask | Mod1Mask), "Ctrl+Alt+Shift is not Ctrl+Shift");
        check(matches("Alt+Z", XKB_KEY_z, Mod1Mask) && matches("Alt+z", XKB_KEY_z, Mod1Mask), "letters match whatever their case");
        check(!matches("Alt+z", XKB_KEY_Z, Mod1Mask | ShiftMask), "Alt+Shift+z is not Alt+z");
        check(matches("Super+space", XKB_KEY_space, Mod4Mask), "GNOME reports Super as Mod4");
        check(matches("Shift+asciitilde", XKB_KEY_asciitilde, ShiftMask), "keysym names as fcitx5 stores them");
        // The US layout puts Meta on the second level of Alt, so Shift then Alt arrives as Meta.
        check(matches("Shift+Alt_L", XKB_KEY_Meta_L, ShiftMask), "Alt pressed with Shift held reads as Meta");
        check(matches("Shift+Alt_L", XKB_KEY_Meta_L, ShiftMask | Mod1Mask | ReleaseMask), "Meta released with Shift held");
        check(!otherModifierHeld(XKB_KEY_Meta_R, Mod1Mask | ReleaseMask), "Meta released alone holds nothing else");
        // GTK 4 can report the Meta bit beside Mod1 while Alt is held.
        check(matches("Alt+Shift_L", XKB_KEY_Shift_L, ShiftMask | Mod1Mask | MetaMask | ReleaseMask), "Shift released with Alt and its Meta bit held");
        check(!otherModifierHeld(XKB_KEY_Alt_L, Mod1Mask | MetaMask | ReleaseMask), "Alt released with its Meta bit holds nothing else");
        check(matches("Meta+space", XKB_KEY_space, Mod1Mask), "a Meta chord reads as Alt");
    }

    const char* const FcitxSwitches = "[Switches]\n[Switches/0]\nApp=kate\nVietnamese=False\n\n";

    void              testImport(const fs::path& root) {
        const SettingsPaths paths = pathsIn(root);
        write(paths.fcitxConfigDir + "/ngosen.conf", FcitxSettings);
        write(paths.fcitxConfigDir + "/ngosen-macro-table.conf", FcitxMacros);
        write(paths.fcitxDataDir + "/vietnamese.cm.dict", "người dùng\n");
        write(paths.fcitxConfigDir + "/ngosen-app-rules.conf", "firefox=5\ncode=0\n");
        write(paths.fcitxConfigDir + "/ngosen-app-switch.conf", FcitxSwitches);
        write(paths.fcitxConfigDir + "/ngosen-vim-apps.conf", "kitty\n");
        check(prepareSettings(paths), "import succeeds");
        const IniFile settings = parseIni(read(paths.settingsFile()));
        check(settings.top.count("InputMethod") == 1 && settings.top.at("InputMethod") == "VNI", "InputMethod imported");
        check(settings.top.count("Mode") == 0, "Mode not imported");
        check(settings.top.count("SettingsVersion") == 0, "the fcitx5 SettingsVersion not imported");
        check(read(paths.macroFile()) == FcitxMacros, "macros copied as they are");
        check(read(paths.dictionaryFile()) == "người dùng\n", "the user's dictionary copied");
        check(!fs::exists(paths.keymapFile()), "no keymap file without one to copy");
        check(read(paths.appModesFile()) == "firefox=5\ncode=0\n", "the modes picked for each app copied as they are");
        check(read(paths.appSwitchesFile()) == FcitxSwitches, "the language each app was switched to copied as it is");
        check(read(paths.vimAppsFile()) == "kitty\n", "the apps with Vim mode copied as they are");

        write(paths.fcitxConfigDir + "/ngosen.conf", "InputMethod=Telex\n");
        check(prepareSettings(paths) && parseIni(read(paths.settingsFile())).top.at("InputMethod") == "VNI", "a later run imports nothing");

        write(paths.fcitxConfigDir + "/ngosen.conf", FcitxSettings);
        fs::remove(paths.settingsFile());
        check(prepareSettings(paths) && read(paths.settingsFile()) == settingsText(defaultSettings()), "a deleted ngosen.conf comes back as the defaults");
    }

    void testNoOverwrite(const fs::path& root) {
        const SettingsPaths paths = pathsIn(root);
        write(paths.fcitxConfigDir + "/ngosen-macro-table.conf", FcitxMacros);
        write(paths.macroFile(), "[Macro/0]\nKey=a\nValue=b\n");
        check(prepareSettings(paths), "prepare with a macro file already there");
        check(read(paths.macroFile()) == "[Macro/0]\nKey=a\nValue=b\n", "an existing file is kept");
    }

    void testDefaults(const fs::path& root) {
        const SettingsPaths paths = pathsIn(root);
        check(prepareSettings(paths), "defaults written");
        check(read(paths.settingsFile()) == settingsText(defaultSettings()), "nothing to import writes the defaults");
    }

    // A settings file without an option added later: fcitx5's choice comes over once, else the default.

    // A run killed after any write leaves files the next run finishes into the same result.
    void testKilledHalfway(const fs::path& root) {
        const auto fill = [](const SettingsPaths& paths) {
            write(paths.fcitxConfigDir + "/ngosen.conf", FcitxSettings);
            write(paths.fcitxConfigDir + "/ngosen-macro-table.conf", FcitxMacros);
            write(paths.fcitxConfigDir + "/ngosen-custom-keymap.conf", "[CustomKeymap/0]\nKey=z\nValue=XoaDauThanh\n");
            write(paths.fcitxDataDir + "/vietnamese.cm.dict", "từ\n");
        };
        const auto result = [](const SettingsPaths& paths) {
            return read(paths.settingsFile()) + read(paths.macroFile()) + read(paths.keymapFile()) + read(paths.dictionaryFile()) + read(paths.importedMark());
        };
        const SettingsPaths clean = pathsIn(root / "clean");
        fill(clean);
        prepareSettings(clean);
        for (int stopAt = 0; stopAt < 6; ++stopAt) {
            const SettingsPaths paths = pathsIn(root / std::to_string(stopAt));
            fill(paths);
            int        writes = 0;
            const bool done = prepareSettings(paths, [&](const std::string& path, const std::string& content) { return writes++ < stopAt && writeFileAtomically(path, content); });
            check(!done && !fs::exists(paths.importedMark()), "stopped after " + std::to_string(stopAt) + " writes");
            prepareSettings(paths);
            check(result(paths) == result(clean), "finished after stopping at write " + std::to_string(stopAt));
        }
    }

} // namespace

int main() {
    Init();
    char        pattern[] = "/tmp/ngosen-config-XXXXXX";
    const char* dir       = mkdtemp(pattern);
    if (dir == nullptr)
        return 2;
    const fs::path root(dir);
    testValues();
    testLists();
    testSettings();
    testOneSetting();
    testToggleKeys();
    testModeMenuSettings();
    testKeyMatching();
    testImport(root / "import");
    testNoOverwrite(root / "keep");
    testDefaults(root / "defaults");
    testKilledHalfway(root / "killed");
    fs::remove_all(root);
    if (failures == 0)
        std::cout << "all settings checks passed\n";
    return failures == 0 ? 0 : 1;
}
