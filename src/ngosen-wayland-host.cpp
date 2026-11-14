/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-wayland-host.h"

#include "ngosen-ibus-i18n.h"
#include "ngosen-input-method.h"
#include "ngosen-log.h"
#include "ngosen-masks.h"

#include <glib.h>
#include <xkbcommon/xkbcommon.h>

namespace ngosen {

    namespace {

        // text-input-v3 content hints and purposes.
        constexpr uint32_t HintHiddenText    = 0x40;
        constexpr uint32_t HintSensitiveData = 0x80;
        constexpr uint32_t PurposeUrl        = 5;
        constexpr uint32_t PurposePassword   = 8;
        constexpr uint32_t PurposePin        = 9;
        constexpr uint32_t PurposeTerminal   = 13;

        // evdev codes the core's edit keys type with.
        constexpr uint32_t BackSpaceCode = 14, TabCode = 15, ReturnCode = 28, ShiftCode = 42, RightCode = 106;

        class GlibTimer final : public Timer {
          public:
            GlibTimer(uint64_t deadlineUs, std::function<bool(Timer&)> onTime, std::function<void()> after) : onTime_(std::move(onTime)), after_(std::move(after)) {
                source_                                   = g_source_new(&funcs_, sizeof(Source));
                reinterpret_cast<Source*>(source_)->timer = this;
                g_source_set_ready_time(source_, static_cast<gint64>(deadlineUs));
                g_source_attach(source_, nullptr);
            }
            ~GlibTimer() override {
                g_source_destroy(source_);
                g_source_unref(source_);
            }
            GlibTimer(const GlibTimer&)            = delete;
            GlibTimer& operator=(const GlibTimer&) = delete;

            void       rearm(uint64_t deadlineUs) override {
                g_source_set_ready_time(source_, static_cast<gint64>(deadlineUs));
            }

          private:
            struct Source {
                GSource    base;
                GlibTimer* timer;
            };

            static gboolean dispatch(GSource* base, GSourceFunc /*callback*/, gpointer /*data*/) {
                auto* timer = reinterpret_cast<Source*>(base)->timer;
                // Fires once unless the callback rearms it and returns true.
                g_source_set_ready_time(base, -1);
                if (!timer->onTime_(*timer))
                    g_source_set_ready_time(base, -1);
                if (timer->after_)
                    timer->after_();
                return G_SOURCE_CONTINUE;
            }

            static GSourceFuncs         funcs_;
            GSource*                    source_ = nullptr;
            std::function<bool(Timer&)> onTime_;
            std::function<void()>       after_;
        };

        GSourceFuncs GlibTimer::funcs_ = {nullptr, nullptr, &GlibTimer::dispatch, nullptr, nullptr, nullptr};

        // Byte offset of the character at index chars, or npos past the end.
        std::size_t byteOfChar(const std::string& text, std::size_t chars) {
            const char* begin = text.c_str();
            const char* p     = begin;
            const char* end   = begin + text.size();
            for (std::size_t i = 0; i < chars; ++i) {
                if (p >= end)
                    return std::string::npos;
                p = g_utf8_next_char(p);
            }
            return p <= end ? static_cast<std::size_t>(p - begin) : std::string::npos;
        }

    } // namespace

    const Options& WaylandResources::options() const {
        if (generation_ != shared_.generation()) {
            options_                   = shared_.options();
            options_.retypeAfterSwitch = false;
            options_.retoneSelection   = false;
            generation_                = shared_.generation();
        }
        return options_;
    }

    void WaylandHost::flush() {
        if (!dirty_)
            return;
        dirty_ = false;
        if (!im_.maySend()) {
            dropPending();
            return;
        }
        if (deleteBefore_ != 0 || deleteAfter_ != 0)
            im_.deleteSurrounding(deleteBefore_, deleteAfter_);
        if (!commit_.empty() || emptyCommit_)
            im_.commitString(commit_);
        if (!preedit_.empty())
            im_.setPreedit(preedit_);
        im_.commit();
        shownPreedit_ = preedit_;
        commit_.clear();
        emptyCommit_  = false;
        deleteBefore_ = deleteAfter_ = 0;
    }

    void WaylandHost::dropPending() {
        if (!commit_.empty() || deleteBefore_ != 0 || deleteAfter_ != 0)
            NGOSEN_DEBUG("Dropped what was not sent before the field went away");
        commit_.clear();
        emptyCommit_ = false;
        preedit_.clear();
        shownPreedit_.clear();
        deleteBefore_ = deleteAfter_ = 0;
        dirty_                       = false;
    }

    bool WaylandHost::takesPassword() const {
        const WaylandField& f = im_.field();
        return f.purpose == PurposePassword || f.purpose == PurposePin;
    }

    void WaylandHost::commitText(const std::string& text) {
        if (text.empty())
            emptyCommit_ = true;
        commit_ += text;
        dirty_ = true;
    }

    void WaylandHost::sendKey(uint32_t sym, uint32_t code, bool release, uint32_t extraMods) {
        // A key goes out at once, so what the core committed before it must reach the field first.
        flush();
        if (!im_.maySend())
            return;
        if (code == 0) {
            code = im_.codeForSym(sym);
            // A key the layout lacks can still type its text.
            if (code == 0) {
                if (!release) {
                    commitText(keyText(sym));
                    flush();
                }
                return;
            }
        }
        im_.sendKey(keyTime_, code, !release, extraMods);
    }

    void WaylandHost::forwardKey(EditKey key, bool release, uint32_t /*time*/) {
        const uint32_t shift = im_.modIndexMask(XKB_MOD_NAME_SHIFT);
        switch (key) {
            case EditKey::BackSpace: sendKey(XKB_KEY_BackSpace, BackSpaceCode, release, 0); break;
            case EditKey::Right: sendKey(XKB_KEY_Right, RightCode, release, 0); break;
            case EditKey::Shift: sendKey(XKB_KEY_Shift_L, ShiftCode, release, 0); break;
            case EditKey::Return: sendKey(XKB_KEY_Return, ReturnCode, release, 0); break;
            case EditKey::ShiftReturn: sendKey(XKB_KEY_Return, ReturnCode, release, shift); break;
            case EditKey::Tab: sendKey(XKB_KEY_Tab, TabCode, release, 0); break;
            case EditKey::ShiftTab: sendKey(XKB_KEY_ISO_Left_Tab, TabCode, release, shift); break;
        }
    }

    // The core counts X11 keycodes, which are evdev codes plus 8.
    void WaylandHost::forwardKeyPress(uint32_t sym, uint32_t states, uint32_t code, bool release, uint32_t /*time*/) {
        const uint32_t shift = (states & ShiftMask) != 0 ? im_.modIndexMask(XKB_MOD_NAME_SHIFT) : 0;
        sendKey(sym, code >= 8 ? code - 8 : 0, release, shift);
    }

    // As fcitx5 does: only a range around the cursor maps onto delete_surrounding_text.
    void WaylandHost::deleteSurrounding(int offset, unsigned int size) {
        const Surrounding s = surrounding();
        if (!s.isValid() || offset > 0 || offset + static_cast<long>(size) < 0 || static_cast<long>(s.cursor()) + offset < 0) {
            NGOSEN_WARN("Cannot delete " + std::to_string(size) + " characters at " + std::to_string(offset) + " from the cursor");
            return;
        }
        const std::size_t start  = byteOfChar(s.text(), s.cursor() + offset);
        const std::size_t cursor = byteOfChar(s.text(), s.cursor());
        const std::size_t end    = byteOfChar(s.text(), s.cursor() + offset + size);
        if (start == std::string::npos || cursor == std::string::npos || end == std::string::npos) {
            NGOSEN_WARN("Cannot delete past the text the app reported");
            return;
        }
        // The protocol applies the deletion before the commit string; a commit gathered earlier goes first.
        if (!commit_.empty() || deleteBefore_ != 0 || deleteAfter_ != 0)
            flush();
        deleteBefore_ = static_cast<uint32_t>(cursor - start);
        deleteAfter_  = static_cast<uint32_t>(end - cursor);
        dirty_        = true;
    }

    // There is no X server to press keys at.
    bool WaylandHost::pressSystemKeys(int /*count*/) {
        return false;
    }

    Surrounding WaylandHost::surrounding() const {
        const WaylandField& f = im_.field();
        if (!f.active || !f.hasText || !g_utf8_validate(f.text.c_str(), static_cast<gssize>(f.text.size()), nullptr))
            return {};
        if (f.cursor > f.text.size() || f.anchor > f.text.size())
            return {};
        // The protocol counts bytes; the core counts characters. A byte inside a character is a bad report.
        const char* text = f.text.c_str();
        const char* end  = text + f.text.size();
        auto        at   = [&](uint32_t bytes) -> long {
            const char* p = text + bytes;
            if (p < end && (static_cast<unsigned char>(*p) & 0xC0) == 0x80)
                return -1;
            return g_utf8_pointer_to_offset(text, p);
        };
        const long cursor = at(f.cursor);
        const long anchor = at(f.anchor);
        if (cursor < 0 || anchor < 0)
            return {};
        return Surrounding(f.text, static_cast<unsigned int>(cursor), static_cast<unsigned int>(anchor));
    }

    Field WaylandHost::field() const {
        const WaylandField& w = im_.field();
        Field               f;
        // As for fcitx5's own input-method-v2 frontend, so the core treats both alike.
        f.frontend = "wayland_v2";
        f.program  = im_.activeProgram();
        if (const std::string appId = im_.activeAppId(); appId != f.program)
            f.reportedProgram = appId;
        f.surroundingText = w.hasText;
        f.preedit         = true;
        f.url             = w.purpose == PurposeUrl;
        f.terminal        = w.purpose == PurposeTerminal;
        f.password        = w.purpose == PurposePassword || w.purpose == PurposePin || (w.hint & (HintHiddenText | HintSensitiveData)) != 0;
        return f;
    }

    bool WaylandHost::hasFocus() const {
        return im_.field().active;
    }

    std::optional<std::string> WaylandHost::takeSelection() {
        return std::nullopt;
    }

    // Converting selected text is a fcitx5 shortcut; this edition has none.
    std::string WaylandHost::primarySelection() const {
        return {};
    }

    void WaylandHost::showPreedit(const std::string& text, bool /*underline*/) {
        preedit_ = text;
    }

    void WaylandHost::clearPreedit() {
        preedit_.clear();
    }

    void WaylandHost::resetPanel() {
        preedit_.clear();
    }

    void WaylandHost::refreshPreedit() {
        if (preedit_ != shownPreedit_)
            dirty_ = true;
    }

    // No candidate list or status line in this edition.
    void                         WaylandHost::refreshPanel() {}
    void                         WaylandHost::showCandidates(const std::vector<std::string>& /*labels*/, int /*pageSize*/, std::function<void(size_t)> /*onPick*/) {}
    void                         WaylandHost::hideCandidates() {}
    std::optional<CandidatePage> WaylandHost::candidates() const {
        return std::nullopt;
    }
    void        WaylandHost::highlightCandidate(int /*index*/) {}
    void        WaylandHost::nextCandidatePage() {}
    void        WaylandHost::prevCandidatePage() {}
    void        WaylandHost::pickCandidate(int /*index*/) {}
    void        WaylandHost::setStatus(const std::string& /*text*/) {}

    std::string WaylandHost::translate(const char* text) const {
        return uiText(text);
    }

    std::unique_ptr<Timer> WaylandHost::startTimer(uint64_t deadlineUs, uint64_t /*accuracyUs*/, std::function<bool(Timer&)> onTime) {
        return std::make_unique<GlibTimer>(deadlineUs, std::move(onTime), afterTimer);
    }

    std::string WaylandHost::keyText(uint32_t sym) const {
        char      buf[8];
        const int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
        // n counts the NUL; control keys type nothing.
        if (n <= 1 || static_cast<unsigned char>(buf[0]) < 0x20 || buf[0] == 0x7f)
            return {};
        return std::string(buf, static_cast<size_t>(n - 1));
    }

} // namespace ngosen
