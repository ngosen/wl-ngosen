/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "test-bus.h"

#include <functional>
#include <gio/gio.h>

namespace test {

    namespace {

        const char* const Introspection = R"xml(<node>
  <interface name="org.kde.StatusNotifierWatcher">
    <method name="RegisterStatusNotifierItem"><arg name="service" type="s" direction="in"/></method>
  </interface>
  <interface name="org.freedesktop.Notifications">
    <method name="Notify">
      <arg type="s" direction="in"/><arg type="u" direction="in"/><arg type="s" direction="in"/>
      <arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="as" direction="in"/>
      <arg type="a{sv}" direction="in"/><arg type="i" direction="in"/><arg type="u" direction="out"/>
    </method>
  </interface>
</node>)xml";

        GDBusNodeInfo*    nodeInfo() {
            static GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(Introspection, nullptr);
            return info;
        }

        void onMethod(GDBusConnection*, const char*, const char*, const char*, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data) {
            auto* bus = static_cast<TestBus*>(data);
            if (g_strcmp0(method, "RegisterStatusNotifierItem") == 0) {
                const char* name = nullptr;
                g_variant_get(parameters, "(&s)", &name);
                bus->noteRegistered(name);
                g_dbus_method_invocation_return_value(invocation, nullptr);
                return;
            }
            const char* summary = nullptr;
            g_variant_get(parameters, "(&su&s&s&s^a&s@a{sv}i)", nullptr, nullptr, nullptr, &summary, nullptr, nullptr, nullptr, nullptr);
            bus->noteNotice(summary);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 7U));
        }

        const GDBusInterfaceVTable VTable = {onMethod, nullptr, nullptr, {}};

        // Walks a dbusmenu layout, (ia{sv}av), until visit returns true.
        bool walk(GVariant* node, const std::function<bool(gint32, GVariant*)>& visit) {
            gint32    id       = 0;
            GVariant* props    = nullptr;
            GVariant* children = nullptr;
            g_variant_get(node, "(i@a{sv}@av)", &id, &props, &children);
            bool found = visit(id, props);
            for (gsize i = 0; !found && i < g_variant_n_children(children); ++i) {
                GVariant* boxed = g_variant_get_child_value(children, i);
                GVariant* child = g_variant_get_variant(boxed);
                found           = walk(child, visit);
                g_variant_unref(child);
                g_variant_unref(boxed);
            }
            g_variant_unref(props);
            g_variant_unref(children);
            return found;
        }

    } // namespace

    TestBus::TestBus() {
        bus_ = g_test_dbus_new(G_TEST_DBUS_NONE);
        g_test_dbus_up(bus_);
        const char* address = g_test_dbus_get_bus_address(bus_);
        if (address == nullptr)
            return;
        connection_ = g_dbus_connection_new_for_address_sync(
            address, static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, nullptr);
        if (connection_ == nullptr)
            return;
        noticeObject_ = g_dbus_connection_register_object(connection_, "/org/freedesktop/Notifications", nodeInfo()->interfaces[1], &VTable, this, nullptr, nullptr);
        noticeOwn_    = g_bus_own_name_on_connection(connection_, "org.freedesktop.Notifications", G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr, nullptr, nullptr);
    }

    TestBus::~TestBus() {
        if (connection_ != nullptr) {
            for (unsigned own : {watcherOwn_, noticeOwn_}) {
                if (own != 0)
                    g_bus_unown_name(own);
            }
            for (unsigned object : {watcherObject_, noticeObject_}) {
                if (object != 0)
                    g_dbus_connection_unregister_object(connection_, object);
            }
            g_dbus_connection_close_sync(connection_, nullptr, nullptr);
            g_object_unref(connection_);
        }
        g_test_dbus_down(bus_);
        g_object_unref(bus_);
    }

    std::string TestBus::address() const {
        return g_test_dbus_get_bus_address(bus_);
    }

    void TestBus::startWatcher() {
        watcherObject_ = g_dbus_connection_register_object(connection_, "/StatusNotifierWatcher", nodeInfo()->interfaces[0], &VTable, this, nullptr, nullptr);
        watcherOwn_    = g_bus_own_name_on_connection(connection_, "org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_NONE, nullptr, nullptr, nullptr, nullptr);
    }

    void TestBus::pump() {
        while (g_main_context_iteration(nullptr, FALSE)) {}
    }

    void TestBus::noteRegistered(const std::string& name) {
        registered_.push_back(name);
    }

    void TestBus::noteNotice(const std::string& summary) {
        notices_.push_back(summary);
    }

    GVariant* TestBus::call(const std::string& item, const char* path, const char* interface, const char* method, GVariant* parameters) {
        return g_dbus_connection_call_sync(connection_, item.c_str(), path, interface, method, parameters, nullptr, G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, nullptr);
    }

    std::string TestBus::iconName(const std::string& item) {
        GVariant* reply = call(item, "/StatusNotifierItem", "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", "org.kde.StatusNotifierItem", "IconName"));
        if (reply == nullptr)
            return "";
        GVariant* value = nullptr;
        g_variant_get(reply, "(v)", &value);
        std::string name = g_variant_get_string(value, nullptr);
        g_variant_unref(value);
        g_variant_unref(reply);
        return name;
    }

    void TestBus::activate(const std::string& item) {
        if (GVariant* reply = call(item, "/StatusNotifierItem", "org.kde.StatusNotifierItem", "Activate", g_variant_new("(ii)", 0, 0)))
            g_variant_unref(reply);
    }

    GVariant* TestBus::menuLayout(const std::string& item) {
        const gchar* none[]  = {nullptr};
        GVariant*    reply   = call(item, "/MenuBar", "com.canonical.dbusmenu", "GetLayout", g_variant_new("(ii^as)", 0, -1, none));
        GVariant*    layout  = nullptr;
        guint32      version = 0;
        if (reply != nullptr) {
            g_variant_get(reply, "(u@(ia{sv}av))", &version, &layout);
            g_variant_unref(reply);
        }
        return layout;
    }

    int TestBus::menuId(const std::string& item, const std::string& label) {
        GVariant* layout = menuLayout(item);
        if (layout == nullptr)
            return -1;
        int found = -1;
        walk(layout, [&](gint32 id, GVariant* props) {
            const char* text = nullptr;
            if (g_variant_lookup(props, "label", "&s", &text) && label == text)
                found = id;
            return found >= 0;
        });
        g_variant_unref(layout);
        return found;
    }

    int TestBus::toggleState(const std::string& item, const std::string& label) {
        GVariant* layout = menuLayout(item);
        if (layout == nullptr)
            return -1;
        int state = -1;
        walk(layout, [&](gint32, GVariant* props) {
            const char* text  = nullptr;
            gint32      value = 0;
            if (!g_variant_lookup(props, "label", "&s", &text) || label != text)
                return false;
            state = g_variant_lookup(props, "toggle-state", "i", &value) ? value : -1;
            return true;
        });
        g_variant_unref(layout);
        return state;
    }

    void TestBus::click(const std::string& item, int id) {
        GVariant* reply = call(item, "/MenuBar", "com.canonical.dbusmenu", "Event", g_variant_new("(isvu)", id, "clicked", g_variant_new_int32(0), 0U));
        if (reply != nullptr)
            g_variant_unref(reply);
    }

} // namespace test
