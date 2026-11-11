/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-tray-menu.h"
#include "ngosen-ibus-i18n.h"

#include <algorithm>
#include <iterator>
#include <sstream>

namespace ngosen {

    namespace {

        struct Toggle {
            const char* key;
            const char* label;
            bool (*isOn)(const IBusSettings&);
            bool IBusSettings::* shown;
        };

        // The IBus edition's items, without "Type with Sen": the mode items below pick it.
        const Toggle Toggles[] = {
            {"SpellCheck", N_("Spell Check"), [](const IBusSettings& s) { return s.options.spellCheck; }, &IBusSettings::showMenuSpellCheck},
            {"EnableMacro", N_("Macro"), [](const IBusSettings& s) { return s.options.enableMacro; }, &IBusSettings::showMenuMacro},
            {"CapitalizeMacro", N_("Capitalize Macro"), [](const IBusSettings& s) { return s.options.capitalizeMacro; }, &IBusSettings::showMenuCapitalizeMacro},
            {"AutoNonVnRestore", N_("Auto Restore Invalid Words"), [](const IBusSettings& s) { return s.options.autoNonVnRestore; }, &IBusSettings::showMenuAutoRestore},
            {"EnableDictionary", N_("Custom Dictionary"), [](const IBusSettings& s) { return s.enableDictionary; }, &IBusSettings::showMenuDictionary},
        };

        const Toggle CapitalizeToggles[] = {
            {"AutoCapitalizeAfterPunctuation", N_("After . ! ? and Enter"), [](const IBusSettings& s) { return s.options.autoCapitalizeAfterPunctuation; },
             &IBusSettings::showMenuAutoCapitalize},
            {"CapitalizeStartOfText", N_("Start of text or line"), [](const IBusSettings& s) { return s.options.capitalizeStartOfText; }, &IBusSettings::showMenuAutoCapitalize},
        };

        // The two methods most people switch between, as in the fcitx5 edition; the rest stay in
        // the settings window.
        const std::vector<std::string> MenuInputMethods = {"Telex", "VNI"};

        struct ModeName {
            MenuMode    mode;
            const char* name;
            const char* label;
            bool IBusSettings::* shown;
        };

        const ModeName ModeNames[] = {
            {MenuMode::Sen, "Sen", N_("Sen"), &IBusSettings::showModeSen},
            {MenuMode::Preedit, "Preedit", N_("Preedit"), &IBusSettings::showModePreedit},
            {MenuMode::Off, "Off", N_("OFF"), &IBusSettings::showModeOff},
        };

        const ModeName& nameOf(MenuMode mode) {
            return *std::find_if(std::begin(ModeNames), std::end(ModeNames), [mode](const ModeName& m) { return m.mode == mode; });
        }

        TrayItem item(const std::string& action, const std::string& label, TrayItem::Kind kind = TrayItem::Kind::Normal, bool checked = false) {
            TrayItem made;
            made.action  = action;
            made.label   = label;
            made.kind    = kind;
            made.checked = checked;
            return made;
        }

        TrayItem separator() {
            return item("", "", TrayItem::Kind::Separator);
        }

        TrayItem checkItem(const Toggle& toggle, const IBusSettings& settings) {
            TrayItem made = item(toggle.key, _(toggle.label), TrayItem::Kind::Check, toggle.isOn(settings));
            made.visible  = settings.*toggle.shown;
            return made;
        }

        TrayItem choice(const std::string& key, const char* label, const std::vector<std::string>& names, const std::string& current, bool shown) {
            TrayItem menu = item("", label, TrayItem::Kind::Submenu);
            menu.visible  = shown;
            for (const auto& name : names)
                menu.children.push_back(item(key + '/' + name, name, TrayItem::Kind::Radio, name == current));
            return menu;
        }

        MenuMode menuModeOf(Mode mode) {
            return mode == Mode::Off ? MenuMode::Off : mode == Mode::Preedit ? MenuMode::Preedit : MenuMode::Sen;
        }

        void addModes(std::vector<TrayItem>& menu, const IBusSettings& settings, const TrayView& view) {
            const auto modes = menuModes(settings);
            if (modes.empty())
                return;
            // A heading, so the user knows the pick is kept for this app only.
            std::string label = _("Typing mode");
            if (!view.app.empty()) {
                gchar* text = g_strdup_printf(_("Typing mode for %s"), view.app.c_str());
                label       = text;
                g_free(text);
            }
            TrayItem heading = item("", label);
            heading.enabled  = false;
            menu.push_back(heading);
            for (const MenuMode mode : modes) {
                const ModeName& name = nameOf(mode);
                menu.push_back(item(std::string("Mode/") + name.name, _(name.label), TrayItem::Kind::Radio, mode == menuModeOf(view.mode)));
            }
            menu.push_back(separator());
        }

        bool contains(const std::vector<std::string>& list, const std::string& value) {
            return std::find(list.begin(), list.end(), value) != list.end();
        }

    } // namespace

    std::vector<MenuMode> menuModes(const IBusSettings& settings) {
        std::vector<MenuMode> modes;
        const auto            add = [&](const ModeName& name) {
            if (settings.*name.shown && std::find(modes.begin(), modes.end(), name.mode) == modes.end())
                modes.push_back(name.mode);
        };
        std::istringstream names(settings.modeOrder);
        for (std::string name; std::getline(names, name, ',');) {
            for (const auto& known : ModeNames) {
                if (trimIni(name) == known.name)
                    add(known);
            }
        }
        for (const auto& known : ModeNames)
            add(known);
        return modes;
    }

    std::vector<TrayItem> trayMenu(const IBusSettings& settings, const TrayView& view) {
        std::vector<TrayItem> menu;
        menu.push_back(item(LanguageAction, _("Vietnamese"), TrayItem::Kind::Check, !view.english));
        menu.push_back(separator());
        addModes(menu, settings, view);
        menu.push_back(choice("InputMethod", _("Input Method"), MenuInputMethods, settings.options.inputMethod, settings.showMenuInputMethod));
        menu.push_back(choice("OutputCharset", _("Charset"), charsetNames(), settings.options.outputCharset, settings.showMenuCharset));
        for (const auto& toggle : Toggles) {
            menu.push_back(checkItem(toggle, settings));
            if (toggle.key == std::string("CapitalizeMacro")) {
                TrayItem capitalize = item("", _("Auto Capitalize"), TrayItem::Kind::Submenu);
                capitalize.visible  = settings.showMenuAutoCapitalize;
                for (const auto& sub : CapitalizeToggles)
                    capitalize.children.push_back(checkItem(sub, settings));
                menu.push_back(capitalize);
            }
        }
        menu.push_back(separator());
        menu.push_back(item(SaveTypingLogAction, _("Save typing log")));
        menu.push_back(item(SettingsAction, _("Settings")));
        return menu;
    }

    std::optional<MenuMode> modeOfAction(const std::string& action) {
        for (const auto& name : ModeNames) {
            if (action == std::string("Mode/") + name.name)
                return name.mode;
        }
        return std::nullopt;
    }

    bool settingForClick(const std::string& action, const IBusSettings& settings, std::string& key, std::string& value) {
        if (const auto slash = action.find('/'); slash != std::string::npos) {
            key   = action.substr(0, slash);
            value = action.substr(slash + 1);
            if (key == "InputMethod")
                return contains(MenuInputMethods, value);
            return key == "OutputCharset" && contains(charsetNames(), value);
        }
        const auto flip = [&](const Toggle& toggle) {
            if (action != toggle.key)
                return false;
            key   = action;
            value = toggle.isOn(settings) ? "False" : "True";
            return true;
        };
        return std::any_of(std::begin(Toggles), std::end(Toggles), flip) || std::any_of(std::begin(CapitalizeToggles), std::end(CapitalizeToggles), flip);
    }

    std::optional<AppMode> keptAppMode(MenuMode mode, bool englishOn, Mode sharedVietnamese) {
        if (mode == MenuMode::Off)
            return AppMode::Off;
        const Mode picked = mode == MenuMode::Sen ? Mode::Sen : Mode::Preedit;
        if (englishOn || picked != sharedVietnamese)
            return mode == MenuMode::Sen ? AppMode::Sen : AppMode::Preedit;
        return std::nullopt;
    }

} // namespace ngosen
