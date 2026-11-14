/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ngosen-engine-resources.h"
#include "ngosen-host.h"
#include "ngosen-ibus-resources.h"

#include <string>

namespace ngosen {

    class InputMethod;

    // The shared settings, with what this edition cannot do turned off: retyping a word after the
    // toggle key and retoning a selection both need the mode menu and clicks this edition lacks.
    class WaylandResources final : public EngineResources {
      public:
        explicit WaylandResources(IBusResources& shared) : shared_(shared) {}

        const Options&           options() const override;
        std::vector<KeymapEntry> customKeymap() const override {
            return shared_.customKeymap();
        }
        uintptr_t dictionary() const override {
            return shared_.dictionary();
        }
        uintptr_t macroTable() const override {
            return shared_.macroTable();
        }
        uintptr_t englishWords() const override {
            return shared_.englishWords();
        }
        uintptr_t keepWords() const override {
            return shared_.keepWords();
        }
        std::vector<EmojiEntry> emojiHistory() override {
            return {};
        }
        std::vector<EmojiEntry> searchEmoji(const std::string& /*prefix*/) override {
            return {};
        }
        void      recordEmoji(const EmojiEntry& /*entry*/) override {}
        Recorder& recorder() override {
            return shared_.recorder();
        }

      private:
        IBusResources&   shared_;
        mutable Options  options_;
        mutable unsigned generation_ = ~0U;
    };

    // The focused field as input-method-v2 shows it. Requests to the field gather until flush(),
    // which sends them with one commit: the compositor applies a commit whole, so the app never sees
    // a word committed without the preedit that follows it.
    class WaylandHost final : public Host {
      public:
        explicit WaylandHost(InputMethod& im) : im_(im) {}

        // Sends what gathered since the last flush, if the field still has focus.
        void flush();
        // Drops what gathered: the field went away, and nothing may reach the next one.
        void dropPending();
        // Makes the next flush commit even with nothing new. Chromium reports surrounding text only
        // after the text-input done that our commit causes, as fcitx5 relies on.
        void answerDone() {
            dirty_ = true;
        }
        bool hasPreedit() const {
            return !preedit_.empty();
        }
        bool takesPassword() const;
        // The time of the key being handled, which keys the core sends carry.
        void setKeyTime(uint32_t time) {
            keyTime_ = time;
        }

        void                         commitText(const std::string& text) override;
        void                         forwardKey(EditKey key, bool release, uint32_t time) override;
        void                         forwardKeyPress(uint32_t sym, uint32_t states, uint32_t code, bool release, uint32_t time) override;
        void                         deleteSurrounding(int offset, unsigned int size) override;
        bool                         pressSystemKeys(int count) override;
        Surrounding                  surrounding() const override;
        Field                        field() const override;
        bool                         hasFocus() const override;
        std::optional<std::string>   takeSelection() override;
        std::string                  primarySelection() const override;
        void                         showPreedit(const std::string& text, bool underline) override;
        void                         clearPreedit() override;
        void                         resetPanel() override;
        void                         refreshPreedit() override;
        void                         refreshPanel() override;
        void                         showCandidates(const std::vector<std::string>& labels, int pageSize, std::function<void(size_t)> onPick) override;
        void                         hideCandidates() override;
        std::optional<CandidatePage> candidates() const override;
        void                         highlightCandidate(int index) override;
        void                         nextCandidatePage() override;
        void                         prevCandidatePage() override;
        void                         pickCandidate(int index) override;
        void                         setStatus(const std::string& text) override;
        std::string                  translate(const char* text) const override;
        std::unique_ptr<Timer>       startTimer(uint64_t deadlineUs, uint64_t accuracyUs, std::function<bool(Timer&)> onTime) override;
        std::string                  keyText(uint32_t sym) const override;

        // Called after each timer, so what the core did then reaches the field too.
        std::function<void()> afterTimer;

      private:
        void         sendKey(uint32_t sym, uint32_t code, bool release, uint32_t extraMods);

        InputMethod& im_;
        // What the core wants the field to show; sent with every commit, as a commit without it clears it.
        std::string preedit_;
        std::string shownPreedit_;
        std::string commit_;
        // The core sent an empty commit on purpose: GTK 3 asks the app for a new copy when one arrives.
        bool     emptyCommit_  = false;
        uint32_t deleteBefore_ = 0;
        uint32_t deleteAfter_  = 0;
        bool     dirty_        = false;
        uint32_t keyTime_      = 0;
    };

} // namespace ngosen
