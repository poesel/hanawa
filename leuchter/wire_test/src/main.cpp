/*
 * Hanawa Leuchter - wire test (APA102 blink + wire-swap)
 *
 * The strip is mounted at its final place, so the ESP can only be reached over
 * the air (ArduinoOTA) - no serial console. The strip showed nothing at all, so
 * the first suspect is the stripe's two control wires sitting the other way
 * round in the HSPI pads (clock should be GPIO14/pad D5, data GPIO13/pad D7).
 *
 * What it does, forever:
 *   - 10 s: first 10 LEDs blink WHITE/off, written with clock = GPIO14, data = GPIO13
 *   - 10 s: first 10 LEDs blink RED/off,  written with clock = GPIO13, data = GPIO14
 * Everything is bit-banged, so both wire arrangements are reachable from one
 * binary (the core SPI library is locked to the HSPI pads). The two colours
 * only make the two halves recognisable from the outside.
 *
 * How to read the strip:
 *   white blink, non-stop   -> the wires are as the production sketch assumes
 *   red blink, non-stop     -> the wires are swapped; production must bit-bang
 *                             or the two stripe wires have to be exchanged
 *   white/red alternating
 *              every 10 s   -> both pairings light the strip (odd: two working
 *                             connections, e.g. the strip continues somewhere)
 *   never anything          -> not a wiring problem: check the strip's own PSU
 *                             and the solder joints of the first segment
 *
 * ArduinoOTA stays in this sketch, so the production build can be pushed back
 * over the air again once the correct wiring is known.
 */

#include <Arduino.h>
#include <Esp.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include "secrets.h"

/* the two pads the stripe's wires sit in */
#define WIRE_A 14 // pad D5
#define WIRE_B 13 // pad D7

#define SWITCH_MS 10000 // swap clock/data roles after this long
#define BLINK_MS 500    // half period of the white/off blink
#define TEST_LEDS 10    // LEDs that blink
#define STRIPE_LEDS 108 // total stripe length (gets explicit black blocks)
#define BRIGHTNESS 31   // APA102 global brightness 0..31

/* --------------- bit-bang APA102 (works on any GPIO pair) ------------------
 * Same bit timing as the proven probe in probe_pins/: SPI_MODE0 style, the
 * data bit is changed while the clock pin is low and then latched with one
 * high pulse. */

static inline void setPin(uint8_t pin, uint8_t val) { digitalWrite(pin, val); }

static void bitOf(uint8_t mosi, uint8_t sck, uint8_t bit) {
  setPin(sck, LOW);
  setPin(mosi, bit);
  delayMicroseconds(3);
  setPin(sck, HIGH);
  delayMicroseconds(3);
  setPin(sck, LOW);
  delayMicroseconds(3);
}

static void sendBits(uint8_t mosi, uint8_t sck, uint8_t value, int count) {
  for (int i = count - 1; i >= 0; i--) bitOf(mosi, sck, (value >> i) & 1);
}

/* One full native APA102 frame: start, TEST_LEDS lit LEDs, the rest of the
 * stripe explicitly black, end frame. r/g/b = 0 means "off". */
static void showFrame(uint8_t mosi, uint8_t sck, uint8_t r, uint8_t g,
                      uint8_t b) {
  for (int i = 0; i < 32; i++) bitOf(mosi, sck, 0);  // 32-bit start frame
  for (int i = 0; i < STRIPE_LEDS; i++) {
    uint8_t rr = (i < TEST_LEDS) ? r : 0;
    uint8_t gg = (i < TEST_LEDS) ? g : 0;
    uint8_t bb = (i < TEST_LEDS) ? b : 0;
    sendBits(mosi, sck, 0xE0 | BRIGHTNESS, 8);  // 111 + 5-bit brightness
    sendBits(mosi, sck, bb, 8);                 // APA102 byte order: B G R
    sendBits(mosi, sck, gg, 8);
    sendBits(mosi, sck, rr, 8);
  }
  for (int i = 0; i < 4; i++) sendBits(mosi, sck, 0xFF, 8);  // end frame
}

/* --------------- WLAN + OTA (so this sketch can be replaced over OTA) ------ */

static void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NET, WIFI_PASS);
  int dots = 0;
  while (!WiFi.isConnected()) {
    delay(500);
    if (++dots >= 40) {
      Serial.println("WLAN FAILED - reboot");
      ESP.restart();
    }
  }
  Serial.print("local IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  Serial.println("== wire_test boot ==");

  // both stripe pads idle while not writing
  pinMode(WIRE_A, INPUT);
  pinMode(WIRE_B, INPUT);

  connectWifi();

  ArduinoOTA.setHostname("hanawa-leuchter");
  ArduinoOTA.setPort(8266);
  ArduinoOTA.onStart([]() { Serial.println("OTA start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA end, rebooting"); });
  ArduinoOTA.begin();
  Serial.println("wire_test ready, OTA at :8266");
}

/* --------------- the alternating test ------------------------------------- */

static int mode = 0;        // 0 = clock WIRE_A(14), 1 = clock WIRE_B(13)
static bool lit = false;    // blink phase
static unsigned long phaseStart = 0;

void loop() {
  ArduinoOTA.handle();  // still replaceable over the air

  uint8_t sck = (mode == 0) ? WIRE_A : WIRE_B;
  uint8_t mosi = (mode == 0) ? WIRE_B : WIRE_A;

  pinMode(sck, OUTPUT);
  pinMode(mosi, OUTPUT);
  if (lit) {
    // mode A blinks white, mode B blinks red
    if (mode == 0)
      showFrame(mosi, sck, 255, 255, 255);
    else
      showFrame(mosi, sck, 255, 0, 0);
  } else {
    showFrame(mosi, sck, 0, 0, 0);
  }
  pinMode(sck, INPUT);
  pinMode(mosi, INPUT);

  Serial.print("mode ");
  Serial.print(mode);
  Serial.print(" clock=GPIO");
  Serial.println(sck);
  Serial.flush();

  lit = !lit;
  delay(BLINK_MS);

  if (millis() - phaseStart >= SWITCH_MS) {
    phaseStart = millis();
    mode ^= 1;
    lit = true;  // first frame of the new mode lights the LEDs
  }
}
