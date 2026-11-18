#ifndef CONFIG_H
#define CONFIG_H

// WLAN-Daten anpassen
#define WIFI_SSID      "Zippen 24"
#define WIFI_PASSWORD  "Boyzoneanker24"

/* ---------------- Leuchter (UDP target) ----------------
 * IP of the Leuchter as printed on its console at boot
 * (see leuchter/README.md: the router assigned 192.168.0.107,
 *   the fixed IPs from the root README are only a wish).
 * Can be overridden with build flags (-DLEUCHTER_IP1=...). */
#ifndef LEUCHTER_IP1
#define LEUCHTER_IP1   192
#endif
#ifndef LEUCHTER_IP2
#define LEUCHTER_IP2   168
#endif
#ifndef LEUCHTER_IP3
#define LEUCHTER_IP3   0
#endif
#ifndef LEUCHTER_IP4
#define LEUCHTER_IP4   107
#endif

/* ---------------- Hyperion UDP protocol ----------------
 * Sender side of the UDP receiver in leuchter/src/main.cpp.
 * One datagram per frame: AA 00 FF <chk> RGB... (see udp_sender.h). */
#ifndef UDP_PORT
#define UDP_PORT        1900    // Hyperion default, same on the Leuchter
#endif
#ifndef MAX_SEND_LEDS
#define MAX_SEND_LEDS   150     // must equal MAX_LEDS on the Leuchter:
                                // longest frame (RGB triplets) we send
#endif
#ifndef HEALTH_BYTE
#define HEALTH_BYTE     0x80    // 1-byte health/sync signal the Leuchter
                                // sends back after every applied frame
#endif

/* ---------------- OTA (Over-The-Air updates) ----------------
 * ArduinoOTA (bundled in the ESP32 core) lets new builds be pushed
 * over WLAN after one last serial flash; see doc/BENUTZERHANDBUCH.md. */
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME    "hanawa-sucher"
#endif
#ifndef OTA_PORT
#define OTA_PORT        8266    // ArduinoOTA's own port, does not clash
                                // with the UDP sender at :1900
#endif

#endif // CONFIG_H
