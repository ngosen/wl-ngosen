/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-engine-resources.h"
#include "ngosen-ibus-config.h"

#include <gio/gio.h>

#include <functional>

namespace ngosen {

    // The word list that comes with the package.
    std::string bundledDictionaryFile();

    // Settings and tables every field shares. No emoji list: the IBus edition has no emoji picker.
    class IBusResources final : public EngineResources {
      public:
        explicit IBusResources(SettingsPaths paths);
        ~IBusResources() override;
        IBusResources(const IBusResources&)            = delete;
        IBusResources& operator=(const IBusResources&) = delete;

        const Options& options() const override {
            return settings_.options;
        }
        std::vector<KeymapEntry> customKeymap() const override {
            return settings_.enableCustomKeymap ? keymap_ : std::vector<KeymapEntry>{};
        }
        uintptr_t dictionary() const override {
            return dictionary_;
        }
        uintptr_t macroTable() const override {
            return macroTable_;
        }
        std::vector<EmojiEntry> emojiHistory() override {
            return {};
        }
        std::vector<EmojiEntry> searchEmoji(const std::string& /*prefix*/) override {
            return {};
        }
        void      recordEmoji(const EmojiEntry& /*entry*/) override {}
        Recorder& recorder() override {
            return recorder_;
        }

        const IBusSettings& settings() const {
            return settings_;
        }
        const SettingsPaths& paths() const {
            return paths_;
        }
        // Writes one setting and returns what the file now says. The file monitor reloads it
        // between words, as for a change the settings window makes.
        bool saveSetting(const std::string& key, const std::string& value, IBusSettings& saved) const;
        // What the file says now, which settings() catches up with only between words.
        IBusSettings savedSettings() const;

        // Counts reloads, so a field knows its engine was built from older settings.
        unsigned generation() const {
            return generation_;
        }
        // Called between words only, as a field's engine is rebuilt after it.
        void reloadIfChanged();
        void noticeChange();
        // The English marker changed under the engine, which reads it again between words.
        void noticeEnglishChange();
        bool takeEnglishChange();
        void onChange(std::function<void()> callback) {
            onChange_ = std::move(callback);
        }

      private:
        void                     load();
        uintptr_t                openDictionary() const;

        SettingsPaths            paths_;
        IBusSettings             settings_;
        std::vector<KeymapEntry> keymap_;
        uintptr_t                dictionary_     = 0;
        uintptr_t                macroTable_     = 0;
        unsigned                 generation_     = 0;
        bool                     changed_        = false;
        bool                     englishChanged_ = false;
        GFileMonitor*            monitor_        = nullptr;
        GFileMonitor*            dataMonitor_    = nullptr;
        GFileMonitor*            stateMonitor_   = nullptr;
        Recorder                 recorder_;
        std::function<void()>    onChange_;
    };

} // namespace ngosen
