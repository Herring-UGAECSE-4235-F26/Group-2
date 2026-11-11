/*
 * ble_server.c - PARMCO BLE GATT server (BlueZ D-Bus API via GLib/GIO GDBus).
 *
 * AI-generated (Claude). See ble_server.h for the service layout.
 *
 * How it works:
 *   1. A GLib main loop runs in a dedicated thread and owns a system-bus
 *      connection.
 *   2. We export D-Bus objects that BlueZ understands:
 *        /org/parmco                 org.freedesktop.DBus.ObjectManager
 *        /org/parmco/service0        org.bluez.GattService1
 *        /org/parmco/service0/char0  org.bluez.GattCharacteristic1  (RX, write)
 *        /org/parmco/service0/char1  org.bluez.GattCharacteristic1  (TX, notify)
 *        /org/parmco/adv0            org.bluez.LEAdvertisement1     ("PARMCO")
 *   3. We call GattManager1.RegisterApplication and
 *      LEAdvertisingManager1.RegisterAdvertisement on /org/bluez/hci0. These
 *      calls are asynchronous because BlueZ calls back into our objects while
 *      it registers them.
 *   4. A phone write arrives as WriteValue(); bytes are buffered until '\n'
 *      and passed to the controller. Replies/telemetry are sent by emitting
 *      PropertiesChanged("Value") on the TX characteristic, which BlueZ turns
 *      into notifications. Long lines are split into chunks that fit the
 *      negotiated ATT MTU (reported by BlueZ in the WriteValue options).
 *
 * Library attribution: GLib/GIO (LGPL-2.1+, the GNOME Project) provides the
 * D-Bus client/server used here; BlueZ (bluetoothd) is the Linux Bluetooth
 * stack that turns these D-Bus objects into a real GATT service.
 */
#include "ble_server.h"

#include <gio/gio.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define BLUEZ            "org.bluez"
#define ADAPTER_PATH     "/org/bluez/hci0"
#define APP_PATH         "/org/parmco"
#define SERVICE_PATH     "/org/parmco/service0"
#define RX_PATH          "/org/parmco/service0/char0"
#define TX_PATH          "/org/parmco/service0/char1"
#define ADV_PATH         "/org/parmco/adv0"

#define SERVICE_UUID     "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define RX_UUID          "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define TX_UUID          "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define LOCAL_NAME       "PARMCO"

#define RX_BUF_MAX       256

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.DBus.ObjectManager'>"
    "    <method name='GetManagedObjects'>"
    "      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
    "    </method>"
    "  </interface>"
    "  <interface name='org.bluez.GattService1'>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Primary' type='b' access='read'/>"
    "  </interface>"
    "  <interface name='org.bluez.GattCharacteristic1'>"
    "    <method name='ReadValue'>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "      <arg name='value' type='ay' direction='out'/>"
    "    </method>"
    "    <method name='WriteValue'>"
    "      <arg name='value' type='ay' direction='in'/>"
    "      <arg name='options' type='a{sv}' direction='in'/>"
    "    </method>"
    "    <method name='StartNotify'/>"
    "    <method name='StopNotify'/>"
    "    <property name='UUID' type='s' access='read'/>"
    "    <property name='Service' type='o' access='read'/>"
    "    <property name='Flags' type='as' access='read'/>"
    "    <property name='Notifying' type='b' access='read'/>"
    "    <property name='Value' type='ay' access='read'/>"
    "  </interface>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='LocalName' type='s' access='read'/>"
    "  </interface>"
    "</node>";

enum obj_id { OBJ_APP = 1, OBJ_SERVICE, OBJ_RX, OBJ_TX, OBJ_ADV };

static GDBusNodeInfo   *node_info;
static GDBusConnection *bus;
static GMainLoop       *loop;
static pthread_t        ble_thread;
static int              thread_started;
static guint            reg_ids[5];
static guint            device_sub_id;

static ble_line_cb      line_cb;
static ble_conn_cb      conn_cb;
static int              use_secure;

static pthread_mutex_t  tx_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int     notifying;
static volatile int     att_mtu = 23;          /* default ATT MTU */
static char             device_path[128];      /* BlueZ path of the phone */

static char             rx_buf[RX_BUF_MAX];
static size_t           rx_len;
static int              rx_overflow;

/* ------------------------------------------------------------------ helpers */

static GVariant *flags_variant(int id)
{
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
    if (id == OBJ_RX) {
        g_variant_builder_add(&b, "s", use_secure ? "encrypt-write" : "write");
        g_variant_builder_add(&b, "s", "write-without-response");
    } else {
        g_variant_builder_add(&b, "s", "notify");
        g_variant_builder_add(&b, "s", "read");
    }
    return g_variant_builder_end(&b);
}

static GVariant *empty_bytes(void)
{
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, NULL, 0, 1);
}

static GVariant *object_property(int id, const gchar *name)
{
    switch (id) {
    case OBJ_SERVICE:
        if (!g_strcmp0(name, "UUID"))    return g_variant_new_string(SERVICE_UUID);
        if (!g_strcmp0(name, "Primary")) return g_variant_new_boolean(TRUE);
        break;
    case OBJ_RX:
    case OBJ_TX:
        if (!g_strcmp0(name, "UUID"))
            return g_variant_new_string(id == OBJ_RX ? RX_UUID : TX_UUID);
        if (!g_strcmp0(name, "Service"))   return g_variant_new_object_path(SERVICE_PATH);
        if (!g_strcmp0(name, "Flags"))     return flags_variant(id);
        if (!g_strcmp0(name, "Notifying")) return g_variant_new_boolean(id == OBJ_TX && notifying);
        if (!g_strcmp0(name, "Value"))     return empty_bytes();
        break;
    case OBJ_ADV:
        if (!g_strcmp0(name, "Type"))      return g_variant_new_string("peripheral");
        if (!g_strcmp0(name, "LocalName")) return g_variant_new_string(LOCAL_NAME);
        if (!g_strcmp0(name, "ServiceUUIDs")) {
            const gchar *uuids[] = { SERVICE_UUID, NULL };
            return g_variant_new_strv(uuids, -1);
        }
        break;
    }
    return NULL;
}

/* a{sv} of all properties of one interface */
static GVariant *interface_props(int id, const char *iface)
{
    GVariantBuilder b;
    GDBusInterfaceInfo *info = g_dbus_node_info_lookup_interface(node_info, iface);
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    for (int i = 0; info && info->properties && info->properties[i]; i++) {
        GVariant *v = object_property(id, info->properties[i]->name);
        if (v) g_variant_builder_add(&b, "{sv}", info->properties[i]->name, v);
    }
    return g_variant_builder_end(&b);
}

static void add_object(GVariantBuilder *objs, const char *path, int id, const char *iface)
{
    GVariantBuilder ifaces;
    g_variant_builder_init(&ifaces, G_VARIANT_TYPE("a{sa{sv}}"));
    g_variant_builder_add(&ifaces, "{s@a{sv}}", iface, interface_props(id, iface));
    g_variant_builder_add(objs, "{o@a{sa{sv}}}", path, g_variant_builder_end(&ifaces));
}

static void set_connected(int c)
{
    int changed;
    pthread_mutex_lock(&tx_lock);
    changed = (notifying != c);
    notifying = c;
    if (!c) {
        device_path[0] = '\0';
        att_mtu = 23;
        rx_len = 0;
        rx_overflow = 0;
    }
    pthread_mutex_unlock(&tx_lock);
    if (changed && conn_cb) conn_cb(c);
}

/* -------------------------------------------------------- method handlers */

static void handle_write(GVariant *params)
{
    GVariant *value = NULL, *options = NULL;
    gsize n = 0;
    g_variant_get(params, "(@ay@a{sv})", &value, &options);
    const guint8 *bytes = g_variant_get_fixed_array(value, &n, 1);

    /* BlueZ reports the negotiated MTU and the phone's device path. */
    guint16 mtu = 0;
    const gchar *dev = NULL;
    if (g_variant_lookup(options, "mtu", "q", &mtu) && mtu >= 23) att_mtu = mtu;
    if (g_variant_lookup(options, "device", "&o", &dev) && dev) {
        pthread_mutex_lock(&tx_lock);
        g_strlcpy(device_path, dev, sizeof(device_path));
        pthread_mutex_unlock(&tx_lock);
    }

    for (gsize i = 0; i < n; i++) {
        char c = (char)bytes[i];
        if (c == '\n') {
            if (!rx_overflow && rx_len > 0) {
                rx_buf[rx_len] = '\0';
                if (line_cb) line_cb(rx_buf);
            }
            rx_len = 0;
            rx_overflow = 0;
        } else if (c != '\r' && !rx_overflow) {
            if (rx_len < RX_BUF_MAX - 1) rx_buf[rx_len++] = c;
            else rx_overflow = 1;
        }
    }
    g_variant_unref(value);
    g_variant_unref(options);
}

static void method_call(GDBusConnection *c, const gchar *sender, const gchar *path,
                        const gchar *iface, const gchar *method, GVariant *params,
                        GDBusMethodInvocation *inv, gpointer user_data)
{
    (void)c; (void)sender; (void)path; (void)iface;
    int id = GPOINTER_TO_INT(user_data);

    if (!g_strcmp0(method, "GetManagedObjects")) {
        GVariantBuilder objs;
        g_variant_builder_init(&objs, G_VARIANT_TYPE("a{oa{sa{sv}}}"));
        add_object(&objs, SERVICE_PATH, OBJ_SERVICE, "org.bluez.GattService1");
        add_object(&objs, RX_PATH, OBJ_RX, "org.bluez.GattCharacteristic1");
        add_object(&objs, TX_PATH, OBJ_TX, "org.bluez.GattCharacteristic1");
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@a{oa{sa{sv}}})",
                                              g_variant_builder_end(&objs)));
    } else if (!g_strcmp0(method, "WriteValue") && id == OBJ_RX) {
        handle_write(params);
        g_dbus_method_invocation_return_value(inv, NULL);
    } else if (!g_strcmp0(method, "ReadValue")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(@ay)", empty_bytes()));
    } else if (!g_strcmp0(method, "StartNotify") && id == OBJ_TX) {
        g_dbus_method_invocation_return_value(inv, NULL);
        set_connected(1);
        printf("[BLE] phone subscribed (connected)\n");
        fflush(stdout);
    } else if (!g_strcmp0(method, "StopNotify") && id == OBJ_TX) {
        g_dbus_method_invocation_return_value(inv, NULL);
        set_connected(0);
        printf("[BLE] phone unsubscribed/disconnected\n");
        fflush(stdout);
    } else if (!g_strcmp0(method, "Release")) {
        g_dbus_method_invocation_return_value(inv, NULL);
    } else {
        g_dbus_method_invocation_return_dbus_error(inv, "org.bluez.Error.NotSupported",
                                                   "Not supported");
    }
}

static GVariant *get_property(GDBusConnection *c, const gchar *sender, const gchar *path,
                              const gchar *iface, const gchar *name, GError **err,
                              gpointer user_data)
{
    (void)c; (void)sender; (void)path; (void)iface;
    GVariant *v = object_property(GPOINTER_TO_INT(user_data), name);
    if (!v) g_set_error(err, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No property %s", name);
    return v;
}

static const GDBusInterfaceVTable vtable = { method_call, get_property, NULL, { 0 } };

/* Backup disconnect detection: BlueZ Device1.Connected -> false for our phone. */
static void device_props_changed(GDBusConnection *c, const gchar *sender, const gchar *path,
                                 const gchar *iface, const gchar *signal, GVariant *params,
                                 gpointer user_data)
{
    (void)c; (void)sender; (void)iface; (void)signal; (void)user_data;
    const gchar *changed_iface = NULL;
    GVariant *changed = NULL, *invalidated = NULL;
    gboolean connected = TRUE;

    g_variant_get(params, "(&s@a{sv}@as)", &changed_iface, &changed, &invalidated);
    if (!g_strcmp0(changed_iface, "org.bluez.Device1") &&
        g_variant_lookup(changed, "Connected", "b", &connected) && !connected) {
        int ours;
        pthread_mutex_lock(&tx_lock);
        ours = device_path[0] && !g_strcmp0(device_path, path);
        pthread_mutex_unlock(&tx_lock);
        if (ours) {
            printf("[BLE] phone disconnected\n");
            fflush(stdout);
            set_connected(0);
        }
    }
    g_variant_unref(changed);
    g_variant_unref(invalidated);
}

/* ------------------------------------------------------------ registration */

static void register_done(GObject *src, GAsyncResult *res, gpointer what)
{
    GError *err = NULL;
    GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);
    if (r) {
        printf("[BLE] %s registered\n", (const char *)what);
        g_variant_unref(r);
    } else {
        fprintf(stderr, "[BLE] %s registration failed: %s\n", (const char *)what,
                err ? err->message : "?");
        g_clear_error(&err);
    }
    fflush(stdout);
}

static int export_object(int slot, const char *path, const char *iface, int id)
{
    GError *err = NULL;
    GDBusInterfaceInfo *info = g_dbus_node_info_lookup_interface(node_info, iface);
    reg_ids[slot] = g_dbus_connection_register_object(bus, path, info, &vtable,
                                                      GINT_TO_POINTER(id), NULL, &err);
    if (!reg_ids[slot]) {
        fprintf(stderr, "[BLE] export %s failed: %s\n", path, err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    return 0;
}

static void *ble_thread_main(void *arg)
{
    (void)arg;
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    loop = g_main_loop_new(ctx, FALSE);

    GError *err = NULL;
    bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &err);
    if (!bus) {
        fprintf(stderr, "[BLE] cannot connect to system bus: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        goto out;
    }

    if (export_object(0, APP_PATH, "org.freedesktop.DBus.ObjectManager", OBJ_APP) ||
        export_object(1, SERVICE_PATH, "org.bluez.GattService1", OBJ_SERVICE) ||
        export_object(2, RX_PATH, "org.bluez.GattCharacteristic1", OBJ_RX) ||
        export_object(3, TX_PATH, "org.bluez.GattCharacteristic1", OBJ_TX) ||
        export_object(4, ADV_PATH, "org.bluez.LEAdvertisement1", OBJ_ADV))
        goto out;

    device_sub_id = g_dbus_connection_signal_subscribe(
        bus, BLUEZ, "org.freedesktop.DBus.Properties", "PropertiesChanged", NULL, NULL,
        G_DBUS_SIGNAL_FLAGS_NONE, device_props_changed, NULL, NULL);

    /* Make sure the adapter is powered. */
    g_dbus_connection_call(bus, BLUEZ, ADAPTER_PATH, "org.freedesktop.DBus.Properties", "Set",
                           g_variant_new("(ssv)", "org.bluez.Adapter1", "Powered",
                                         g_variant_new_boolean(TRUE)),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

    g_dbus_connection_call(bus, BLUEZ, ADAPTER_PATH, "org.bluez.GattManager1",
                           "RegisterApplication",
                           g_variant_new("(o@a{sv})", APP_PATH,
                                         g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0)),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, register_done,
                           "GATT application");
    g_dbus_connection_call(bus, BLUEZ, ADAPTER_PATH, "org.bluez.LEAdvertisingManager1",
                           "RegisterAdvertisement",
                           g_variant_new("(o@a{sv})", ADV_PATH,
                                         g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0)),
                           NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, register_done,
                           "LE advertisement");

    printf("[BLE] advertising as \"%s\"\n", LOCAL_NAME);
    fflush(stdout);
    g_main_loop_run(loop);

out:
    if (bus) {
        for (int i = 0; i < 5; i++)
            if (reg_ids[i]) g_dbus_connection_unregister_object(bus, reg_ids[i]);
        if (device_sub_id) g_dbus_connection_signal_unsubscribe(bus, device_sub_id);
        g_object_unref(bus);
        bus = NULL;
    }
    g_main_context_pop_thread_default(ctx);
    if (loop) { g_main_loop_unref(loop); loop = NULL; }
    g_main_context_unref(ctx);
    return NULL;
}

/* -------------------------------------------------------------- public API */

int ble_server_start(ble_line_cb on_line, ble_conn_cb on_conn, int secure)
{
    GError *err = NULL;
    line_cb = on_line;
    conn_cb = on_conn;
    use_secure = secure;
    node_info = g_dbus_node_info_new_for_xml(introspection_xml, &err);
    if (!node_info) {
        fprintf(stderr, "[BLE] bad introspection XML: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    if (pthread_create(&ble_thread, NULL, ble_thread_main, NULL) != 0) return -1;
    thread_started = 1;
    return 0;
}

void ble_server_stop(void)
{
    if (!thread_started) return;
    if (loop) g_main_loop_quit(loop);
    pthread_join(ble_thread, NULL);
    thread_started = 0;
    if (node_info) { g_dbus_node_info_unref(node_info); node_info = NULL; }
}

int ble_is_connected(void)
{
    return notifying;
}

int ble_send_line(const char *line)
{
    int rc = -1;
    pthread_mutex_lock(&tx_lock);
    if (notifying && bus) {
        char msg[512];
        int len = snprintf(msg, sizeof(msg), "%s\n", line);
        if (len > (int)sizeof(msg) - 1) len = (int)sizeof(msg) - 1;
        int chunk = att_mtu - 3;
        if (chunk < 20) chunk = 20;
        if (chunk > 180) chunk = 180;
        for (int off = 0; off < len; off += chunk) {
            int n = len - off < chunk ? len - off : chunk;
            GVariantBuilder changed;
            g_variant_builder_init(&changed, G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(&changed, "{sv}", "Value",
                                  g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, msg + off, n, 1));
            g_dbus_connection_emit_signal(bus, NULL, TX_PATH,
                                          "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                          g_variant_new("(sa{sv}as)", "org.bluez.GattCharacteristic1",
                                                        &changed, NULL),
                                          NULL);
        }
        rc = 0;
    }
    pthread_mutex_unlock(&tx_lock);
    return rc;
}
