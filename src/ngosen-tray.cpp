/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-tray.h"
#include "ngosen-ibus-i18n.h"
#include "ngosen-log.h"

#include <unistd.h>

namespace ngosen {

    namespace {

        constexpr const char* ItemPath      = "/StatusNotifierItem";
        constexpr const char* MenuPath      = "/MenuBar";
        constexpr const char* ItemInterface = "org.kde.StatusNotifierItem";
        constexpr const char* MenuInterface = "com.canonical.dbusmenu";
        constexpr const char* WatcherName   = "org.kde.StatusNotifierWatcher";

        const char* const     Introspection = R"xml(<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="ContextMenu"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
    <method name="Activate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg name="x" type="i" direction="in"/><arg name="y" type="i" direction="in"/></method>
    <method name="Scroll"><arg name="delta" type="i" direction="in"/><arg name="orientation" type="s" direction="in"/></method>
    <signal name="NewIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg name="status" type="s"/></signal>
  </interface>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg name="parentId" type="i" direction="in"/><arg name="recursionDepth" type="i" direction="in"/>
      <arg name="propertyNames" type="as" direction="in"/>
      <arg name="revision" type="u" direction="out"/><arg name="layout" type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg name="ids" type="ai" direction="in"/><arg name="propertyNames" type="as" direction="in"/>
      <arg name="properties" type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg name="id" type="i" direction="in"/><arg name="name" type="s" direction="in"/>
      <arg name="value" type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg name="id" type="i" direction="in"/><arg name="eventId" type="s" direction="in"/>
      <arg name="data" type="v" direction="in"/><arg name="timestamp" type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg name="events" type="a(isvu)" direction="in"/><arg name="idErrors" type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg name="id" type="i" direction="in"/><arg name="needUpdate" type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg name="ids" type="ai" direction="in"/><arg name="updatesNeeded" type="ai" direction="out"/>
      <arg name="idErrors" type="ai" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
    <signal name="LayoutUpdated"><arg name="revision" type="u"/><arg name="parent" type="i"/></signal>
  </interface>
</node>)xml";

        GDBusNodeInfo*        nodeInfo() {
            static GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(Introspection, nullptr);
            return info;
        }

        void onMethod(GDBusConnection*, const char*, const char*, const char* interface, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation,
                      gpointer data) {
            static_cast<Tray*>(data)->methodCall(interface, method, parameters, invocation);
        }

        GVariant* onProperty(GDBusConnection*, const char*, const char*, const char* interface, const char* name, GError**, gpointer data) {
            return static_cast<Tray*>(data)->property(interface, name);
        }

        const GDBusInterfaceVTable VTable = {onMethod, onProperty, nullptr, {}};

        // dbusmenu takes an underscore as the mark of an access key.
        std::string menuLabel(const std::string& label) {
            std::string escaped;
            for (const char c : label) {
                escaped += c;
                if (c == '_')
                    escaped += '_';
            }
            return escaped;
        }

        GVariant* emptyPixmaps() {
            return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
        }

    } // namespace

    Tray::Tray(Handlers handlers) : handlers_(std::move(handlers)) {}

    Tray::~Tray() {
        if (watchId_ != 0)
            g_bus_unwatch_name(watchId_);
        if (ownId_ != 0)
            g_bus_unown_name(ownId_);
        if (bus_ != nullptr) {
            if (itemObject_ != 0)
                g_dbus_connection_unregister_object(bus_, itemObject_);
            if (menuObject_ != 0)
                g_dbus_connection_unregister_object(bus_, menuObject_);
            g_dbus_connection_flush_sync(bus_, nullptr, nullptr);
            g_object_unref(bus_);
        }
    }

    bool Tray::start() {
        GError* error = nullptr;
        bus_          = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (bus_ == nullptr) {
            NGOSEN_WARN(std::string("No tray icon, no session bus: ") + error->message);
            g_error_free(error);
            return false;
        }
        itemObject_ = g_dbus_connection_register_object(bus_, ItemPath, nodeInfo()->interfaces[0], &VTable, this, nullptr, nullptr);
        menuObject_ = g_dbus_connection_register_object(bus_, MenuPath, nodeInfo()->interfaces[1], &VTable, this, nullptr, nullptr);
        name_       = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-1";
        ownId_      = g_bus_own_name_on_connection(
            bus_, name_.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE, [](GDBusConnection*, const char*, gpointer data) { static_cast<Tray*>(data)->nameAcquired(); }, nullptr, this,
            nullptr);
        // A bar started later, or restarted, brings a new watcher; the item registers with each.
        watchId_ = g_bus_watch_name_on_connection(
            bus_, WatcherName, G_BUS_NAME_WATCHER_FLAGS_NONE, [](GDBusConnection*, const char*, const char*, gpointer data) { static_cast<Tray*>(data)->watcherAppeared(); },
            [](GDBusConnection*, const char*, gpointer data) { static_cast<Tray*>(data)->watcherVanished(); }, this, nullptr);
        return true;
    }

    void Tray::nameAcquired() {
        nameOwned_ = true;
        registerWithWatcher();
    }

    void Tray::watcherAppeared() {
        watcherUp_ = true;
        registerWithWatcher();
    }

    void Tray::watcherVanished() {
        watcherUp_ = false;
    }

    void Tray::registerWithWatcher() {
        if (!nameOwned_ || !watcherUp_)
            return;
        g_dbus_connection_call(
            bus_, WatcherName, "/StatusNotifierWatcher", WatcherName, "RegisterStatusNotifierItem", g_variant_new("(s)", name_.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
            nullptr,
            [](GObject* source, GAsyncResult* result, gpointer) {
                GError*   error = nullptr;
                GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
                if (reply != nullptr) {
                    NGOSEN_INFO("Tray icon registered");
                    g_variant_unref(reply);
                    return;
                }
                NGOSEN_WARN(std::string("The tray did not take the icon: ") + error->message);
                g_error_free(error);
            },
            nullptr);
    }

    void Tray::show(std::vector<TrayItem> menu, bool english) {
        menu_ = std::move(menu);
        items_.assign(1, nullptr);
        children_.assign(1, {});
        // Ids follow the order of the items, so a click names an item of the layout last sent.
        const std::function<void(const std::vector<TrayItem>&, int)> add = [&](const std::vector<TrayItem>& list, int parent) {
            for (const auto& entry : list) {
                const int id = static_cast<int>(items_.size());
                items_.push_back(&entry);
                children_.emplace_back();
                children_[static_cast<size_t>(parent)].push_back(id);
                add(entry.children, id);
            }
        };
        add(menu_, 0);
        ++revision_;
        if (bus_ == nullptr)
            return;
        g_dbus_connection_emit_signal(bus_, nullptr, MenuPath, MenuInterface, "LayoutUpdated", g_variant_new("(ui)", revision_, 0), nullptr);
        if (english != english_) {
            english_ = english;
            emitItem("NewIcon", nullptr);
            emitItem("NewToolTip", nullptr);
        }
    }

    void Tray::emitItem(const char* signal, GVariant* parameters) {
        g_dbus_connection_emit_signal(bus_, nullptr, ItemPath, ItemInterface, signal, parameters, nullptr);
    }

    GVariant* Tray::toolTip() const {
        const char* text = english_ ? _("English") : _("Vietnamese");
        return g_variant_new("(s@a(iiay)ss)", "", emptyPixmaps(), "Ngó Sen", text);
    }

    GVariant* Tray::property(const char* interface, const char* name) {
        const std::string n = name;
        if (g_strcmp0(interface, MenuInterface) == 0) {
            if (n == "Version")
                return g_variant_new_uint32(3);
            if (n == "TextDirection")
                return g_variant_new_string("ltr");
            if (n == "Status")
                return g_variant_new_string("normal");
            const char* paths[] = {NGOSEN_ICONDIR};
            return g_variant_new_strv(paths, 1);
        }
        if (n == "Category")
            return g_variant_new_string("SystemServices");
        if (n == "Id")
            return g_variant_new_string("wl-ngosen");
        if (n == "Title")
            return g_variant_new_string("Ngó Sen");
        if (n == "Status")
            return g_variant_new_string("Active");
        if (n == "WindowId")
            return g_variant_new_int32(0);
        if (n == "IconName")
            return g_variant_new_string(english_ ? "wl-ngosen-off" : "wl-ngosen");
        if (n == "IconThemePath")
            return g_variant_new_string(NGOSEN_ICONDIR);
        if (n == "IconPixmap")
            return emptyPixmaps();
        if (n == "ToolTip")
            return toolTip();
        if (n == "ItemIsMenu")
            return g_variant_new_boolean(FALSE);
        if (n == "Menu")
            return g_variant_new_object_path(MenuPath);
        return g_variant_new_string("");
    }

    void Tray::methodCall(const char* interface, const char* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
        if (g_strcmp0(interface, MenuInterface) == 0)
            menuCall(method, parameters, invocation);
        else
            itemCall(method, invocation);
    }

    void Tray::itemCall(const char* method, GDBusMethodInvocation* invocation) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        // The menu opens on a right click; a left click switches the language, as in fcitx5.
        if (g_strcmp0(method, "Activate") == 0 && handlers_.activate)
            handlers_.activate();
    }

    GVariant* Tray::itemProperties(int id) const {
        GVariantBuilder props;
        g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
        if (id == 0) {
            g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
            return g_variant_builder_end(&props);
        }
        const TrayItem& entry = *items_[static_cast<size_t>(id)];
        if (entry.kind == TrayItem::Kind::Separator)
            g_variant_builder_add(&props, "{sv}", "type", g_variant_new_string("separator"));
        else
            g_variant_builder_add(&props, "{sv}", "label", g_variant_new_string(menuLabel(entry.label).c_str()));
        if (!entry.enabled)
            g_variant_builder_add(&props, "{sv}", "enabled", g_variant_new_boolean(FALSE));
        if (!entry.visible)
            g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(FALSE));
        if (entry.kind == TrayItem::Kind::Check || entry.kind == TrayItem::Kind::Radio) {
            g_variant_builder_add(&props, "{sv}", "toggle-type", g_variant_new_string(entry.kind == TrayItem::Kind::Check ? "checkmark" : "radio"));
            g_variant_builder_add(&props, "{sv}", "toggle-state", g_variant_new_int32(entry.checked ? 1 : 0));
        }
        if (entry.kind == TrayItem::Kind::Submenu)
            g_variant_builder_add(&props, "{sv}", "children-display", g_variant_new_string("submenu"));
        return g_variant_builder_end(&props);
    }

    GVariant* Tray::layout(int id, int depth) const {
        GVariantBuilder children;
        g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
        if (depth != 0) {
            for (const int child : children_[static_cast<size_t>(id)])
                g_variant_builder_add(&children, "v", layout(child, depth < 0 ? depth : depth - 1));
        }
        return g_variant_new("(i@a{sv}av)", id, itemProperties(id), &children);
    }

    void Tray::clicked(int id, const char* event) {
        if (g_strcmp0(event, "clicked") != 0 || id <= 0 || static_cast<size_t>(id) >= items_.size())
            return;
        const std::string action = items_[static_cast<size_t>(id)]->action;
        if (!action.empty() && handlers_.click)
            handlers_.click(action);
    }

    namespace {

        GVariant* emptyInts() {
            return g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0);
        }

    } // namespace

    bool Tray::knownItem(gint32 id) const {
        return id >= 0 && static_cast<size_t>(id) < items_.size();
    }

    void Tray::getLayout(GVariant* parameters, GDBusMethodInvocation* invocation) {
        gint32 parent = 0, depth = -1;
        g_variant_get(parameters, "(ii^a&s)", &parent, &depth, nullptr);
        if (!knownItem(parent))
            parent = 0;
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))", revision_, layout(parent, depth)));
    }

    void Tray::getGroupProperties(GVariant* parameters, GDBusMethodInvocation* invocation) {
        GVariantIter* ids = nullptr;
        g_variant_get(parameters, "(ai^a&s)", &ids, nullptr);
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));
        for (gint32 id = 0; g_variant_iter_next(ids, "i", &id);) {
            if (knownItem(id))
                g_variant_builder_add(&result, "(i@a{sv})", id, itemProperties(id));
        }
        g_variant_iter_free(ids);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ia{sv}))", &result));
    }

    void Tray::getProperty(GVariant* parameters, GDBusMethodInvocation* invocation) {
        gint32      id   = 0;
        const char* name = nullptr;
        g_variant_get(parameters, "(i&s)", &id, &name);
        GVariant* value = nullptr;
        if (knownItem(id)) {
            GVariant* props = g_variant_ref_sink(itemProperties(id));
            value           = g_variant_lookup_value(props, name, nullptr);
            g_variant_unref(props);
        }
        if (value == nullptr) {
            g_dbus_method_invocation_return_dbus_error(invocation, "com.canonical.dbusmenu.Error", "No such property");
            return;
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
        g_variant_unref(value);
    }

    // The reply goes first: a click can rebuild the menu, and the bar asks for it again at once.
    void Tray::events(GVariant* parameters, GDBusMethodInvocation* invocation, bool group) {
        std::vector<std::pair<int, std::string>> clicks;
        gint32                                   id    = 0;
        const char*                              event = nullptr;
        if (group) {
            GVariantIter* list = nullptr;
            g_variant_get(parameters, "(a(isvu))", &list);
            while (g_variant_iter_next(list, "(i&svu)", &id, &event, nullptr, nullptr))
                clicks.emplace_back(id, event);
            g_variant_iter_free(list);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", emptyInts()));
        } else {
            g_variant_get(parameters, "(i&svu)", &id, &event, nullptr, nullptr);
            clicks.emplace_back(id, event);
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }
        for (const auto& [clickId, what] : clicks)
            clicked(clickId, what.c_str());
    }

    void Tray::menuCall(const char* method, GVariant* parameters, GDBusMethodInvocation* invocation) {
        const std::string m = method;
        if (m == "GetLayout")
            getLayout(parameters, invocation);
        else if (m == "GetGroupProperties")
            getGroupProperties(parameters, invocation);
        else if (m == "GetProperty")
            getProperty(parameters, invocation);
        else if (m == "Event" || m == "EventGroup")
            events(parameters, invocation, m == "EventGroup");
        else if (m == "AboutToShow")
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        else
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)", emptyInts(), emptyInts()));
    }

} // namespace ngosen
