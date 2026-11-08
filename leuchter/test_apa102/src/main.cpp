/*
 * Hanawa Leuchter - Minimal APA102/SPI test (no WiFi, no UDP)
 *
 * Flashed to find out whether the native APA102 stripe responds AT ALL
 * to the D1 mini's default SPI pins (SCK=GPIO14/D5, MOSI=GPIO13/D7).
 *
 * Test 1: 5 Hz white/red/green/blue cycle  -> if colors are visible but
 *         too fast/blinking, the SPI link is OK but timing matters.
 * Test 2: 2 Hz white/red/off cycle         -> must be clearly visible.
 *
 * If NOTHING ever changes: the stripe is NOT driven via GPIO13/14 on
 * this physical device.
 */

#include <Arduino.h>
#include <SPI.h>

// Stripe as it was during this test session (superseded by probe_pins/)
#define LEDS 40

static void ledFrame(uint8_t r, uint8_t g, uint8_t b) {
    // native APA102 frame: start (32x 0bit) + per-LED (111+5bit bright, B, G, R)
    // + end frame (0xFF per 16 LEDs)
    SPI.write(0x00);
    SPI.write(0x00);
    SPI.write(0x00);
    SPI.write(0x00);
    for (int i = 0; i < LEDS; i++) {
        SPI.write(0xE0 | 31);
        SPI.write(b);
        SPI.write(g);
        SPI.write(r);
    }
    SPI.write(0xFF);
    SPI.write(0xFF);
    SPI.write(0xFF);
    SPI.write(0xFF);
}

void setup() {
    Serial.begin(115200);
    SPI.begin();
    SPI.setFrequency(10000000);
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    Serial.println("APA102 test ready");
}

void loop() {
    Serial.println("cycle: white -> red -> off (2Hz)");
    ledFrame(255, 255, 255); delay(500);
    ledFrame(255, 0, 0);     delay(500);
    ledFrame(0, 0, 0);       delay(500);
    ledFrame(0, 255, 0);     delay(500);
    ledFrame(0, 0, 255);     delay(500);
    ledFrame(0, 0, 0);       delay(500);
}
