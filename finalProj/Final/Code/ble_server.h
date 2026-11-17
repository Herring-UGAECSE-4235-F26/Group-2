/*
 * ble_server.h - PARMCO Bluetooth Low Energy (BLE) GATT server for the iPhone app.
 *
 * AI-generated (Claude). iOS apps cannot use Bluetooth Classic serial (RFCOMM),
 * so the RP4 also exposes a BLE service. It uses the same newline-terminated
 * text protocol as the RFCOMM server, so both apps share one command parser.
 *
 * Service : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E  (Nordic-UART-style layout)
 *   RX    : 6E400002-...  write / write-without-response  (phone -> RP4)
 *   TX    : 6E400003-...  notify                          (RP4 -> phone)
 *
 * Implemented on BlueZ's documented D-Bus GATT API (org.bluez.GattManager1 and
 * org.bluez.LEAdvertisingManager1) using GLib/GIO's GDBus.
 */
#ifndef BLE_SERVER_H
#define BLE_SERVER_H

/* Called (from the BLE thread) with each complete command line, without '\n'. */
typedef void (*ble_line_cb)(const char *line);
/* Called when a phone subscribes (1) or disconnects/unsubscribes (0). */
typedef void (*ble_conn_cb)(int connected);

/* Start the BLE server in its own thread. 'secure' != 0 requires an encrypted
 * (paired) link for writes. Returns 0 on success. */
int  ble_server_start(ble_line_cb on_line, ble_conn_cb on_conn, int secure);
void ble_server_stop(void);

/* Send one line to the phone (a '\n' is appended). Thread-safe.
 * Returns 0 if sent, -1 if no phone is subscribed. */
int  ble_send_line(const char *line);
int  ble_is_connected(void);

#endif
