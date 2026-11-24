/*
 * Hanawa Leuchter - UDP LED-Empfang for ESP8266 (Wemos D1 mini)
 *
 * Receives color frames via UDP from a sender (Sucher or test-program),
 * applies them to the APA102 LED stripe and sends back the health byte
 * 0x80 after every applied frame (frame-sync for the sender).
 *
 * Hyperion UDP LED protocol (n = number of RGB triplets in the datagram,
 *   up to MAX_LEDS; the sender's --leds must be <= MAX_LEDS):
 *   [0] 0xAA  (magic)
 *   [1] 0x00  (no command) / 0xAB (command byte follows at [4])
 *   [2] 0xFF  (magic)
 *   [3] checksum: low byte of sum(bytes[0..2] + all color bytes + fade byte)
 *   [4..] R G B per LED (with 0xAB: starting at [5])
 *   optional trailing byte after the last triplet: FADE in units of 50 ms
 *     (1 = 0.1 s .. 255 = 25.5 s). If present and > 0 the stripe does NOT
 *     jump to the new colors: it interpolates from whatever it currently
 *     shows to the new colors over that duration, on its own timer (a new
 *     frame arriving mid-fade restarts the fade from the current mix).
 *     Absent/0 = old instant behaviour, so sucher2 frames are unaffected.
 *
 * The checksum always covers the whole received payload. The frame decides how
 * many LEDs are addressed: a datagram with n triplets sends exactly n APA102
 * LED blocks, so a stripe of any length up to MAX_LEDS works with the same
 * binary. Blocks the stripe does not have are dropped by its connector; LEDs
 * behind a frame that is shorter than the stripe stay black. An APA102 stripe
 * cannot report its length to the ESP (SPI is one-way), which is why the length
 * is never asked for.
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Esp.h>
#include <ArduinoOTA.h>
#include "apa102.h"
#include "config.h"

WiFiUDP udp;      // receives at UDP_PORT
WiFiUDP ack;      // sends health byte back to the sender

APA102 stripe;
uint8_t rxbuf[UDP_BUF_RX];

/* -------- Hyperion-Protokol -------- */

// Validate header and checksum. Returns number of RGB triplets, 0 = invalid.
// On success *rgb_out points at the first color byte (behind the optional
// 0xAB command byte) and *fade_out holds the trailing fade byte (0 if the
// frame has no tail, i.e. instant).
static int parseHyperion(const uint8_t *buf, int len, const uint8_t **rgb_out,
                        uint8_t *fade_out) {
  if (len < 5) return 0;
  if (buf[0] != 0xAA || buf[2] != 0xFF) return 0;

  int off = 4;
  if (buf[1] == 0xAB) off = 5;  // skip command byte
  else if (buf[1] != 0x00) return 0;

  int rem = len - off;
  int colors = rem / 3;
  int tail = rem % 3;  // 0 = no fade byte, 1 = trailing fade byte, 2 = broken
  if (tail == 2) return 0;
  if (colors <= 0) return 0;
  if (colors > MAX_LEDS) return 0;  // longer than the buffer/stripe capacity allows

  uint8_t sum = 0;
  for (int i = 0; i < 3; i++) sum += buf[i];  // header without checksum byte
  for (int i = off; i < off + colors * 3 + tail; i++) sum += buf[i];
  if (sum != buf[3]) return 0;  // wrong checksum

  *rgb_out = buf + off;
  *fade_out = tail ? buf[off + colors * 3] : 0;
  return colors;
}

/* -------- LED frame + fade state -------- */

// cur = what the stripe shows right now, tgt = newest target, start = mix
// base of the running fade. All three are RGB bytes, LED 0..MAX_LEDS-1.
static uint8_t curC[MAX_LEDS * 3];
static uint8_t tgtC[MAX_LEDS * 3];
static uint8_t startC[MAX_LEDS * 3];
static int frameLeds = 0;        // LEDs addressed by cur/tgt
static uint32_t fadeDurMs = 0;   // running fade duration, 0 = nothing running
static uint32_t fadeElapsedMs = 0;

static void renderCur() {
  for (int i = 0; i < frameLeds; i++) {
    stripe.setPixel(i, curC[i * 3], curC[i * 3 + 1], curC[i * 3 + 2]);
  }
  stripe.setNumLeds(frameLeds);
  stripe.show();
}

static void newFrame(const uint8_t *rgb, int colors, uint8_t fadeByte) {
  if (colors > frameLeds) frameLeds = colors;
  if (fadeByte == 0) {  // instant: old behaviour
    for (int i = 0; i < colors * 3; i++) {
      curC[i] = tgtC[i] = rgb[i];
      startC[i] = rgb[i];
    }
    // LEDs the PREVIOUS frame addressed but this one doesn't must not
    // keep their old colour ("stuck LEDs") -> they go off
    for (int i = colors * 3; i < frameLeds * 3; i++) {
      curC[i] = tgtC[i] = startC[i] = 0;
    }
    fadeDurMs = 0;
    renderCur();
    return;
  }
  // start the fade from the CURRENT mix (also valid mid-fade)
  for (int i = 0; i < colors * 3; i++) {
    startC[i] = curC[i];
    tgtC[i] = rgb[i];
  }
  for (int i = colors * 3; i < frameLeds * 3; i++) {
    startC[i] = curC[i];   // shrinking frames fade the extras to black,
    tgtC[i] = 0;           // never leave them stuck at an old colour
  }
  fadeDurMs = (uint32_t)fadeByte * 50;  // tail byte = 50 ms units
  fadeElapsedMs = 0;
}

// advance the animation by dtMs; returns true if something was shown
static bool stepFade(uint32_t dtMs) {
  if (fadeDurMs == 0) return false;
  fadeElapsedMs += dtMs;
  if (fadeElapsedMs >= fadeDurMs) fadeElapsedMs = fadeDurMs;
  for (int i = 0; i < frameLeds * 3; i++) {
    int a = startC[i], b = tgtC[i];
    curC[i] = (uint8_t)(a + (int64_t)(b - a) * fadeElapsedMs / fadeDurMs);
  }
  if (fadeElapsedMs >= fadeDurMs) fadeDurMs = 0;  // arrived, stop animating
  renderCur();
  return true;
}

/* -------- WLAN-Statusdrucken -------- */

// wl_status_t codes of the ESP8266 core, see wl_definitions.h
static void printWifiDiag() {
  int st = WiFi.status();
  Serial.print("WiFi status=");
  Serial.print(st);
  switch (st) {
    case 0: Serial.println(" (idle/still trying)"); break;
    case 1: Serial.println(" (SSID not found!)"); break;
    case 3: Serial.println(" (CONNECTED)"); break;
    case 4: Serial.println(" (connect failed)"); break;
    case 5: Serial.println(" (connection lost)"); break;
    case 6: Serial.println(" (WRONG PASSWORD!)"); break;
    default: Serial.println(" (?)"); break;
  }
}

/* -------- Setup / Loop -------- */

void setup() {
  Serial.begin(115200);
  Serial.println("== Hanawa Leuchter boot ==");

  // native APA102 stripe via HSPI (SCK=GPIO14/D5, MOSI=GPIO13/D7).
  // begin() only allocates the pixel buffer for the longest frame we accept;
  // every frame states how many LEDs it addresses (see renderFrame).
  stripe.begin(MAX_LEDS);
  stripe.setBrightness(LED_BRIGHTNESS);   // 0..31

  // Boot-Testcycle: shows if the SPI-Write reaches the stripe. MAX_LEDS blocks
  // are sent, a shorter stripe simply ignores the rest.
  Serial.println("LED test cycle: white/red/green/blue/off (1.5s each)");
  for (int i = 0; i < MAX_LEDS; i++) stripe.setPixel(i, 255, 255, 255);
  stripe.show(); delay(1500);
  for (int i = 0; i < MAX_LEDS; i++) stripe.setPixel(i, 255, 0, 0);
  stripe.show(); delay(1500);
  for (int i = 0; i < MAX_LEDS; i++) stripe.setPixel(i, 0, 255, 0);
  stripe.show(); delay(1500);
  for (int i = 0; i < MAX_LEDS; i++) stripe.setPixel(i, 0, 0, 255);
  stripe.show(); delay(1500);
  stripe.clear(); stripe.show();
  Serial.println("test cycle done");

  WiFi.begin(WIFI_NET, WIFI_PASS);
  Serial.println("WLAN connect...");
  uint8_t dots = 0;
  while (!WiFi.isConnected()) {
    delay(500);
    Serial.println(".");
    if (++dots % 8 == 0) printWifiDiag();
    if (dots >= 40) {
      Serial.println("WLAN FAILED after 20s - reboot, check secrets.h!");
      ESP.restart();
    }
  }
  printWifiDiag();
  Serial.print("local IP: ");
  Serial.println(WiFi.localIP());

  if (!udp.begin(UDP_PORT)) {
    Serial.println("UDP port failed, halt");
    while (true) delay(1000);
  }
  Serial.print("listening for UDP at :");
  Serial.println(UDP_PORT);

  // OTA: new versions of this sketch are pushed over WLAN (see README.md).
  // The first OTA-capable build still has to be flashed over serial.
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPort(OTA_PORT);
  ArduinoOTA.onStart([]() {
    Serial.print("OTA start: ");
    Serial.println(ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA finished, rebooting into new sketch");
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.print("OTA error ");
    Serial.println((int)err);
  });
  ArduinoOTA.begin();
  Serial.print("OTA ready, upload to this ESP at :");
  Serial.println(OTA_PORT);
}

void loop() {
  ArduinoOTA.handle();  // no-op until an OTA upload arrives; also keeps mDNS alive

  int len = udp.parsePacket();
  if (len > 0) {
    // one datagram max UDP_BUF_RX bytes, read it completely
    if (len > UDP_BUF_RX) len = UDP_BUF_RX;
    int got = udp.read(rxbuf, len);

    // remember sender so the health byte can go back to him
    IPAddress sender = udp.remoteIP();
    uint16_t sport = udp.remotePort();

    const uint8_t *rgb = nullptr;
    uint8_t fadeByte = 0;
    int colors = parseHyperion(rxbuf, got, &rgb, &fadeByte);
    if (colors == 0) {
      Serial.print("invalid UDP packet (");
      Serial.print(got);
      Serial.println(" bytes), rejected");
    } else {
      newFrame(rgb, colors, fadeByte);  // instant frames show right away
      ack.beginPacket(sender, sport);
      ack.write(HEALTH_BYTE);
      ack.endPacket();
      Serial.print("frame: ");
      Serial.print(colors);
      Serial.print(" LEDs, fade ");
      Serial.print(fadeByte);  // 50ms units, 0 = instant
      Serial.println(" -> ack 0x80");
    }
  }

  // fade animation ticks even when no new datagram arrives
  static uint32_t lastAnim = 0;
  uint32_t now = millis();
  if (now - lastAnim >= FADE_TICK_MS) {
    stepFade(now - lastAnim);
    lastAnim = now;
  }

  delay(1);
}
