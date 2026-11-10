/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-daemon.h"

#include "ngosen-input-method.h"
#include "ngosen-log.h"

#include <glib-unix.h>
#include <glib.h>
#include <signal.h>
#include <wayland-client.h>

namespace ngosen {

    namespace {

        struct Daemon {
            InputMethod im;
            GMainLoop*  loop   = nullptr;
            int         status = 0;

            void        handleKey(const ImEvent& event) {
                im.sendKey(event.time, event.code, event.pressed);
            }

            void handle(const ImEvent& event) {
                im.apply(event);
                switch (event.kind) {
                    case ImEvent::Kind::Key: handleKey(event); break;
                    case ImEvent::Kind::Unavailable:
                        NGOSEN_WARN("The compositor gave the input method to another program, or the seat went away");
                        status = 1;
                        g_main_loop_quit(loop);
                        break;
                    default: break;
                }
            }

            void drain() {
                while (auto event = im.takeEvent()) {
                    handle(*event);
                    if (!g_main_loop_is_running(loop))
                        return;
                }
                im.flush();
            }
        };

        // Reads the Wayland socket from the GLib main loop, so file monitors and D-Bus share one thread.
        struct WaylandSource {
            GSource  source;
            Daemon*  daemon;
            gpointer tag;
        };

        gboolean prepareSource(GSource* base, gint* timeout) {
            auto* source = reinterpret_cast<WaylandSource*>(base);
            *timeout     = -1;
            source->daemon->im.flush();
            return source->daemon->im.hasEvents();
        }

        gboolean checkSource(GSource* base) {
            auto* source = reinterpret_cast<WaylandSource*>(base);
            return source->daemon->im.hasEvents() || (g_source_query_unix_fd(base, source->tag) & (G_IO_IN | G_IO_ERR | G_IO_HUP)) != 0;
        }

        gboolean dispatchSource(GSource* base, GSourceFunc /*callback*/, gpointer /*data*/) {
            auto*              source = reinterpret_cast<WaylandSource*>(base);
            Daemon&            daemon = *source->daemon;
            const GIOCondition seen   = g_source_query_unix_fd(base, source->tag);
            if ((seen & (G_IO_ERR | G_IO_HUP)) != 0 || !daemon.im.readEvents()) {
                NGOSEN_WARN("Lost the connection to the compositor");
                daemon.status = 1;
                g_main_loop_quit(daemon.loop);
                return G_SOURCE_REMOVE;
            }
            daemon.drain();
            return G_SOURCE_CONTINUE;
        }

        GSourceFuncs sourceFuncs = {prepareSource, checkSource, dispatchSource, nullptr, nullptr, nullptr};

        gboolean     onSignal(gpointer data) {
            g_main_loop_quit(static_cast<Daemon*>(data)->loop);
            return G_SOURCE_CONTINUE;
        }

    } // namespace

    int runDaemon() {
        Daemon daemon;
        if (!daemon.im.connect())
            return 0;
        daemon.loop    = g_main_loop_new(nullptr, FALSE);
        auto* source   = reinterpret_cast<WaylandSource*>(g_source_new(&sourceFuncs, sizeof(WaylandSource)));
        source->daemon = &daemon;
        source->tag    = g_source_add_unix_fd(&source->source, daemon.im.fd(), static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP));
        g_source_attach(&source->source, nullptr);
        g_unix_signal_add(SIGINT, onSignal, &daemon);
        g_unix_signal_add(SIGTERM, onSignal, &daemon);
        NGOSEN_INFO("Typing on Wayland");
        g_main_loop_run(daemon.loop);
        g_source_destroy(&source->source);
        g_source_unref(&source->source);
        daemon.im.shutdown();
        g_main_loop_unref(daemon.loop);
        return daemon.status;
    }

} // namespace ngosen
