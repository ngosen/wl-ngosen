/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-ibus-app-modes.h"
#include "ngosen-ibus-config.h"
#include "ngosen-mode.h"

#include <optional>
#include <string>
#include <vector>

namespace ngosen {

    // The modes a pick can keep for an app, in the IBus edition's mode menu order.
    enum class MenuMode {
        Sen,
        Preedit,
        Off
    };

    // One item of the tray menu, as dbusmenu describes it.
    struct TrayItem {
        enum class Kind {
            Normal,
            Check,
            Radio,
            Separator,
            Submenu
        };
        // What a click asks for; empty for an item that does nothing.
        std::string           action;
        std::string           label;
        Kind                  kind    = Kind::Normal;
        bool                  checked = false;
        bool                  visible = true;
        bool                  enabled = true;
        std::vector<TrayItem> children;

        bool                  operator==(const TrayItem& other) const {
            return action == other.action && label == other.label && kind == other.kind && checked == other.checked && visible == other.visible && enabled == other.enabled &&
                children == other.children;
        }
    };

    // What the menu shows around the settings: the language and the mode of the app in use.
    struct TrayView {
        bool        english = false;
        std::string app;
        Mode        mode = Mode::Sen;
    };

    constexpr const char* LanguageAction      = "InputMode";
    constexpr const char* SaveTypingLogAction = "SaveTypingLog";

    // The fcitx5 edition's tray menu, with the IBus edition's mode menu in place of its field menu.
    std::vector<TrayItem> trayMenu(const IBusSettings& settings, const TrayView& view);

    // The shown modes in ModeOrder, then the shown ones it leaves out.
    std::vector<MenuMode>   menuModes(const IBusSettings& settings);

    std::optional<MenuMode> modeOfAction(const std::string& action);

    // The ngosen.conf change a click asks for; an on/off item flips what the file says now. False
    // for an action that is not a setting.
    bool settingForClick(const std::string& action, const IBusSettings& settings, std::string& key, std::string& value);

    // As the IBus edition: a pick is kept for an app unless it is the mode the app gets anyway. Off,
    // and picks made while English is on everywhere, are always kept, or the app would follow the
    // shared language instead.
    std::optional<AppMode> keptAppMode(MenuMode mode, bool englishOn, Mode sharedVietnamese);

} // namespace ngosen
