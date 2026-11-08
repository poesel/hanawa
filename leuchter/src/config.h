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
#define UDP_BUF_RX    320               // 4 + 3*99 = 301 bytes + headroom
#define UDP_BUF_TX    8

/* ---------------- LEDs ---------------- */
#ifndef LED_COUNT
#define LED_COUNT     42                // physical stripe length (n < 100!)
#endif
#if LED_COUNT >= 100
#error "LED_COUNT must be < 100"
#endif

/* Stripe wiring: native APA102 frames over the ESP8266's fixed HSPI unit,
   CLOCK = GPIO14, DATA = GPIO13. The stripe's two wires are connected so that
   the clock wire lands on GPIO14; measured with the GPIO probe in probe_pins/
   (before the wires were swapped it sat the other way round, which the
   hardware SPI unit cannot use). */
#define LED_BRIGHTNESS     31               // APA102 global brightness (0..31)

/* Hyperion health/sync byte */
#define HEALTH_BYTE   0x80

#endif // LEUCHTER_CONFIG_H
