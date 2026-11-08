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
#define LED_COUNT     60                // n < 100!
#endif
#if LED_COUNT >= 100
#error "LED_COUNT must be < 100"
#endif

#define MAX_BRIGHTNESS 200              // FastLED global brightness (0..255)

/* LED-SPI-Pins (Wemos D1 mini: D0=GPIO16, D1=GPIO5) */
#define LED_SPI_DATA_PIN   16
#define LED_SPI_CLOCK_PIN  5

/* Hyperion health/sync byte */
#define HEALTH_BYTE   0x80

#endif // LEUCHTER_CONFIG_H
