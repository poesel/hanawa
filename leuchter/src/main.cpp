/*
 * Hanawa Leuchter - UDP LED-Empfang for ESP8266 (Wemos D1 mini)
 *
 * Receives color frames via UDP from a sender (Sucher or test-program),
 * applies them to the APA102 LED stripe and sends back the health byte
 * 0x80 after every applied frame (frame-sync for the sender).
 *
 * Hyperion UDP LED protocol (n = LED_COUNT LEDs):
 *   [0] 0xAA  (magic)
 *   [1] 0x00  (no command) / 0xAB (command byte follows at [4])
 *   [2] 0xFF  (magic)
 *   [3] checksum: low byte of sum(bytes[0..2] + all color bytes)
 *   [4..] R G B per LED (with 0xAB: starting at [5])
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
static int parseHyperion(const uint8_t *buf, int len) {
  if (len < 5) return 0;
  if (buf[0] != 0xAA || buf[2] != 0xFF) return 0;

  int off = 4;
  if (buf[1] == 0xAB) off = 5;  // skip command byte
  else if (buf[1] != 0x00) return 0;

  int colors = (len - off) / 3;
  if (colors <= 0) return 0;
  if (colors > LED_COUNT) colors = LED_COUNT;  // truncate too-long frames

  uint8_t sum = 0;
  for (int i = 0; i < 3; i++) sum += buf[i];        // header without checksum byte
  for (int i = off; i < off + colors * 3; i++) sum += buf[i];
  if (sum != buf[3]) return 0;  // wrong checksum

  return colors;
}

/* -------- FastLED -------- */

static void renderFrame(const uint8_t *rgb, int colors) {
  for (int i = 0; i < LED_COUNT; i++) {
    if (i < colors) {
      stripe.setPixel(i, rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    } else {
      stripe.setPixel(i, 0, 0, 0);  // not-sent LEDs stay black
    }
  }
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

  // native APA102 stripe via HSPI (SCK=GPIO14/D5, MOSI=GPIO13/D7)
  stripe.begin(LED_COUNT);
  stripe.setBrightness(LED_BRIGHTNESS);   // 0..31

  // Boot-Testcycle: shows if the SPI-Write reaches the stripe
  Serial.println("LED test cycle: white/red/green/blue/off (1.5s each)");
  for (int i = 0; i < LED_COUNT; i++) stripe.setPixel(i, 255, 255, 255);
  stripe.show(); delay(1500);
  for (int i = 0; i < LED_COUNT; i++) stripe.setPixel(i, 255, 0, 0);
  stripe.show(); delay(1500);
  for (int i = 0; i < LED_COUNT; i++) stripe.setPixel(i, 0, 255, 0);
  stripe.show(); delay(1500);
  for (int i = 0; i < LED_COUNT; i++) stripe.setPixel(i, 0, 0, 255);
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
  if (len <= 0) {
    delay(1);
    return;
  }

  // one datagram max UDP_BUF_RX bytes, read it completely
  if (len > UDP_BUF_RX) len = UDP_BUF_RX;
  int got = udp.read(rxbuf, len);

  // remember sender so the health byte can go back to him
  IPAddress sender = udp.remoteIP();
  uint16_t sport = udp.remotePort();

  int colors = parseHyperion(rxbuf, got);
  if (colors == 0) {
    Serial.print("invalid UDP packet (");
    Serial.print(got);
    Serial.println(" bytes), rejected");
  } else {
    renderFrame(rxbuf + 4, colors);
    stripe.show();
    ack.beginPacket(sender, sport);
    ack.write(HEALTH_BYTE);
    ack.endPacket();
    Serial.print("frame: ");
    Serial.print(colors);
    Serial.println(" LEDs -> ack 0x80");
  }

  delay(1);
}
