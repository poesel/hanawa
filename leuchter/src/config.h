/*
 * Hanawa Leuchter – Konfiguration
 *
 * ESP8266 (Wemos D1 mini) with APA102 LED strip, Hyperion UDP receive.
 */

#ifndef LEUCHTER_CONFIG_H
#define LEUCHTER_CONFIG_H

/* ---------------- WLAN ---------------- */
/* Credentials live in secrets.h (git-ignored); copy secrets.h.example as
   starting point. */
#include "secrets.h"

/* ---------------- UDP ---------------- */
#define UDP_PORT      1900              // Hyperion default port
#ifndef MAX_RX_LEDS
#define MAX_RX_LEDS   150               // longest received frame: 4 + 3*150 bytes
#endif
#define UDP_BUF_RX    460               // 4 + 3*MAX_RX_LEDS = 454 bytes + headroom
#define UDP_BUF_TX    8

/* ---------------- LEDs ---------------- */
#ifndef LED_COUNT
#define LED_COUNT     42                // physical stripe length
#endif
#if LED_COUNT > MAX_RX_LEDS
#error "LED_COUNT must be <= MAX_RX_LEDS"
#endif

/* Stripe wiring: native APA102 frames over the ESP8266's fixed HSPI unit,
   CLOCK = GPIO14, DATA = GPIO13. The stripe's two wires are connected so that
   the clock wire lands on GPIO14; measured with the GPIO probe in probe_pins/
   (before the wires were swapped it sat the other way round, which the
   hardware SPI unit cannot use). */
#define LED_BRIGHTNESS     31               // APA102 global brightness (0..31)

/* Hyperion health/sync byte */
#define HEALTH_BYTE   0x80

/* ---------------- OTA ---------------- */
/* ArduinoOTA listens on this port (default 8266) and offers the compiled
   sketch over WLAN; see README.md for the upload command. No password, so
   any device on the LAN that knows the hostname could flash it. */
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME  "hanawa-leuchter"
#endif
#ifndef OTA_PORT
#define OTA_PORT      8266
#endif

#endif // LEUCHTER_CONFIG_H
