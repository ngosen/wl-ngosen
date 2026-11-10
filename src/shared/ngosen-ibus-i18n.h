/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NGOSEN_IBUS_I18N_H
#define NGOSEN_IBUS_I18N_H

#include <glib/gi18n-lib.h>

namespace ngosen {

    // Vietnamese from the catalog whatever the desktop language is, or the English the code holds, as
    // the fcitx5 edition does; the language can change while the engine runs.
    void        setUiEnglish(bool english);
    bool        uiEnglish();
    const char* uiText(const char* english);

} // namespace ngosen

#undef _
#define _(String) ngosen::uiText(String)

#endif
