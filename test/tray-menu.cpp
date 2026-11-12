/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bamboo-core.h"
#include "ngosen-ibus-i18n.h"
#include "ngosen-tray-menu.h"

#include <iostream>
#include <string>

using namespace ngosen;

namespace {

    int  failures = 0;

    void check(bool ok, const std::string& what) {
        if (!ok) {
            ++failures;
            std::cout << "FAIL " << what << '\n';
        }
    }

    const TrayItem* find(const std::vector<TrayItem>& menu, const std::string& label) {
        for (const auto& item : menu) {
            if (item.label == label)
                return &item;
            if (const TrayItem* child = find(item.children, label))
                return child;
        }
        return nullptr;
    }

    void testModeOrder() {
        IBusSettings settings;
        settings.modeOrder = "Off, Preedit";
        const auto modes   = menuModes(settings);
        check(modes == std::vector<MenuMode>{MenuMode::Off, MenuMode::Preedit, MenuMode::Sen}, "ModeOrder first, then the modes it leaves out");
        settings.showModePreedit = false;
        check(menuModes(settings) == std::vector<MenuMode>{MenuMode::Off, MenuMode::Sen}, "a hidden mode is left out");
        settings.showModeSen = settings.showModeOff = false;
        const auto menu                             = trayMenu(settings, {});
        check(find(menu, "Typing mode") == nullptr, "no heading when every mode is hidden");
    }

    void testMenu() {
        IBusSettings settings;
        settings.options.spellCheck = true;
        settings.showMenuCharset    = false;
        const auto menu             = trayMenu(settings, {false, "firefox", Mode::Preedit});
        check(menu.front().action == LanguageAction && menu.front().checked, "Vietnamese comes first and is checked");
        const TrayItem* heading = find(menu, "Typing mode for firefox");
        check(heading != nullptr && !heading->enabled, "the heading names the app and cannot be clicked");
        const TrayItem* preedit = find(menu, "Preedit");
        check(preedit != nullptr && preedit->checked && preedit->kind == TrayItem::Kind::Radio, "the app's mode is the checked radio");
        const TrayItem* charset = find(menu, "Charset");
        check(charset != nullptr && !charset->visible, "a menu item hidden in the settings stays hidden");
        check(find(menu, "VNI") != nullptr && find(menu, "Microsoft layout") == nullptr, "only Telex and VNI in the menu");
    }

    void testClicks() {
        IBusSettings settings;
        settings.options.spellCheck = true;
        std::string key, value;
        check(settingForClick("SpellCheck", settings, key, value) && key == "SpellCheck" && value == "False", "an on/off item flips what the file says");
        check(settingForClick("CapitalizeStartOfText", settings, key, value) && key == "CapitalizeStartOfText", "the capitalize submenu items are settings");
        check(settingForClick("InputMethod/VNI", settings, key, value) && key == "InputMethod" && value == "VNI", "a method item sets the method");
        check(!settingForClick("InputMethod/Bogus", settings, key, value), "an unknown method is refused");
        check(!settingForClick("OutputCharset/Bogus", settings, key, value), "an unknown charset is refused");
        check(!settingForClick("UiLanguage/English", settings, key, value), "a key that is not in the menu is refused");
        check(!settingForClick(SaveTypingLogAction, settings, key, value), "the typing log item is not a setting");
        check(modeOfAction("Mode/Off") == MenuMode::Off && !modeOfAction("Mode/Vim"), "mode actions map to modes");
    }

    void testKeptMode() {
        check(keptAppMode(MenuMode::Off, false, Mode::Sen) == AppMode::Off, "Off is always kept");
        check(!keptAppMode(MenuMode::Sen, false, Mode::Sen), "the shared mode is not kept, so the app follows the setting");
        check(keptAppMode(MenuMode::Preedit, false, Mode::Sen) == AppMode::Preedit, "another mode is kept");
        check(keptAppMode(MenuMode::Sen, true, Mode::Sen) == AppMode::Sen, "a pick while English is on is kept");
    }

} // namespace

int main() {
    Init();
    setUiEnglish(true);
    testModeOrder();
    testMenu();
    testClicks();
    testKeptMode();
    if (failures == 0)
        std::cout << "all tray menu checks passed\n";
    return failures == 0 ? 0 : 1;
}
