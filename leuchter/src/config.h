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

/* Capacity limit: the longest frame we accept and the longest stripe the
   driver can address. The number of LEDs that actually gets lit is decided at
   runtime by the frame itself - see the "LEDs" section below. */
#ifndef MAX_LEDS
#define MAX_LEDS      150
#endif

#ifndef UDP_BUF_RX
#define UDP_BUF_RX    (4 + 3 * MAX_LEDS + 6)  // header + all triplets + headroom
#endif
#define UDP_BUF_TX    8

/* ---------------- LEDs ---------------- */
/* The number of addressed LEDs equals the number of RGB triplets in the
   received datagram (Hyperion's / the sender's --leds): a frame with 10
   triplets sends 10 LED blocks and leaves the rest of the stripe black, a
   frame with 150 triplets sends 150 LED blocks. Blocks beyond the physical
   stripe are simply dropped by the stripe's connector, so a stripe of any
   length up to MAX_LEDS works without rebuilding.

   An APA102 stripe is a one-way pass-through (SPI bits in, forwarded bits out)
   and has no read-back channel, so it cannot tell the ESP how long it is. The
   length only matters as a capacity for the driver's pixel buffer (3 bytes per
   LED) and as the receive limit. */

/* Stripe wiring: native APA102 frames over the ESP8266's fixed HSPI unit,
   CLOCK = GPIO14, DATA = GPIO13. The stripe's two wires are connected so that
   the clock wire lands on GPIO14; measured with the GPIO probe in probe_pins/
   (before the wires were swapped it sat the other way round, which the
   hardware SPI unit cannot use). */
#define LED_BRIGHTNESS     31               // APA102 global brightness (0..31)

/* Hyperion health/sync byte */
#define HEALTH_BYTE   0x80

/* ---------------- Fade ---------------- */
/* A datagram may carry a trailing byte = fade duration in units of 100 ms
   (see main.cpp protocol doc). The ESP interpolates cur -> new on this
   timer; the tick is the animation resolution (40 ms = 25 fps). */
#ifndef FADE_TICK_MS
#define FADE_TICK_MS  40
#endif

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
