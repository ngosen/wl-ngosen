/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-ibus-resources.h"

#include "bamboo-core.h"
#include "ngosen-log.h"

#include <cstdlib>
#include <fcntl.h>

namespace ngosen {

    namespace {

        uintptr_t newMacroTable(const std::vector<KeymapEntry>& macros) {
            std::vector<char*> definition;
            definition.reserve((macros.size() * 2) + 1);
            for (const auto& macro : macros) {
                // The C API takes char** but does not write through it.
                definition.push_back(const_cast<char*>(macro.key.data()));   //NOLINT
                definition.push_back(const_cast<char*>(macro.value.data())); //NOLINT
            }
            definition.push_back(nullptr);
            return NewMacroTable(definition.data());
        }

        std::vector<KeymapEntry> readList(const std::string& path, const char* name) {
            std::string text;
            return readFile(path, text) ? iniList(parseIni(text), name) : std::vector<KeymapEntry>{};
        }

        uintptr_t openDictionaryAt(const std::string& path) {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            return fd != -1 ? NewDictionary(static_cast<uintptr_t>(fd)) : 0;
        }

        // Editors and atomic saves also leave temporary files there.
        bool isSettingsFile(GFile* file) {
            if (file == nullptr)
                return false;
            gchar*     name  = g_file_get_basename(file);
            const bool known = g_strcmp0(name, "ngosen.conf") == 0 || g_strcmp0(name, "ngosen-macro-table.conf") == 0 || g_strcmp0(name, "ngosen-custom-keymap.conf") == 0 ||
                g_strcmp0(name, "vietnamese.cm.dict") == 0;
            g_free(name);
            return known;
        }

        bool isEnglishMark(GFile* file) {
            if (file == nullptr)
                return false;
            gchar*     name = g_file_get_basename(file);
            const bool mark = g_strcmp0(name, "english") == 0;
            g_free(name);
            return mark;
        }

        void onDirChanged(GFileMonitor* /*monitor*/, GFile* file, GFile* other, GFileMonitorEvent /*event*/, gpointer resources) {
            if (isSettingsFile(file) || isSettingsFile(other))
                static_cast<IBusResources*>(resources)->noticeChange();
        }

        void onStateChanged(GFileMonitor* /*monitor*/, GFile* file, GFile* other, GFileMonitorEvent /*event*/, gpointer resources) {
            if (isEnglishMark(file) || isEnglishMark(other))
                static_cast<IBusResources*>(resources)->noticeEnglishChange();
        }

        GFileMonitor* watchDirectory(const std::string& path, GCallback onChanged, IBusResources* resources) {
            GFile*        dir     = g_file_new_for_path(path.c_str());
            GFileMonitor* monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
            g_object_unref(dir);
            if (monitor != nullptr)
                g_signal_connect(monitor, "changed", onChanged, resources);
            return monitor;
        }

    } // namespace

    IBusResources::IBusResources(SettingsPaths paths) : paths_(std::move(paths)) {
        Init();
        prepareSettings(paths_);
        load();
        monitor_ = watchDirectory(paths_.configDir, G_CALLBACK(onDirChanged), this);
        // GLib notices a folder created later only seconds after, missing a file written at once.
        g_mkdir_with_parents(paths_.dataDir.c_str(), 0700);
        dataMonitor_ = watchDirectory(paths_.dataDir, G_CALLBACK(onDirChanged), this);
        // The settings window restores the language by writing the marker.
        g_mkdir_with_parents(paths_.stateDir.c_str(), 0700);
        stateMonitor_ = watchDirectory(paths_.stateDir, G_CALLBACK(onStateChanged), this);
    }

    IBusResources::~IBusResources() {
        for (GFileMonitor* monitor : {monitor_, dataMonitor_, stateMonitor_}) {
            if (monitor != nullptr)
                g_object_unref(monitor);
        }
        DeleteObject(macroTable_);
        DeleteObject(dictionary_);
    }

    void IBusResources::noticeChange() {
        changed_ = true;
        if (onChange_)
            onChange_();
    }

    void IBusResources::noticeEnglishChange() {
        englishChanged_ = true;
        if (onChange_)
            onChange_();
    }

    bool IBusResources::takeEnglishChange() {
        const bool changed = englishChanged_;
        englishChanged_    = false;
        return changed;
    }

    void IBusResources::reloadIfChanged() {
        if (!changed_)
            return;
        changed_ = false;
        load();
        ++generation_;
        NGOSEN_INFO("Settings reloaded");
    }

    bool IBusResources::saveSetting(const std::string& key, const std::string& value, IBusSettings& saved) const {
        std::string text;
        // A deleted file comes back whole, so the settings window still finds every key.
        if (!readFile(paths_.settingsFile(), text))
            text = settingsText(settings_);
        text = withSetting(text, key, value);
        if (!writeFileAtomically(paths_.settingsFile(), text))
            return false;
        saved = readSettings(parseIni(text));
        return true;
    }

    IBusSettings IBusResources::savedSettings() const {
        std::string text;
        return readFile(paths_.settingsFile(), text) ? readSettings(parseIni(text)) : settings_;
    }

    void IBusResources::load() {
        std::string text;
        settings_ = readFile(paths_.settingsFile(), text) ? readSettings(parseIni(text)) : defaultSettings();
        keymap_   = readList(paths_.keymapFile(), "CustomKeymap");
        // An engine keeps its own reference to the tables, so the old handles can go now.
        DeleteObject(macroTable_);
        DeleteObject(dictionary_);
        macroTable_ = newMacroTable(readList(paths_.macroFile(), "Macro"));
        dictionary_ = openDictionary();
    }

    std::string bundledDictionaryFile() {
        // Lets the tests run the engine from the build tree.
        const char* override = std::getenv("NGOSEN_IBUS_DICTIONARY");
        return override != nullptr ? override : NGOSEN_IBUS_DICTIONARY;
    }

    // The user's dictionary only with the custom dictionary on, as the fcitx5 edition does. The
    // spell check reads the packaged one otherwise.
    uintptr_t IBusResources::openDictionary() const {
        if (settings_.enableDictionary) {
            if (const uintptr_t dictionary = openDictionaryAt(paths_.dictionaryFile())) {
                NGOSEN_INFO("Dictionary: " + paths_.dictionaryFile());
                return dictionary;
            }
        }
        const std::string path       = bundledDictionaryFile();
        const uintptr_t   dictionary = openDictionaryAt(path);
        if (dictionary != 0)
            NGOSEN_INFO("Dictionary: " + path);
        else
            NGOSEN_WARN("No dictionary at " + path);
        return dictionary;
    }

} // namespace ngosen
