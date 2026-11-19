#include "udp_sender.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>

// Hyperion UDP-Sender for the Sucher (ESP32CAM)
// Speaks the Hyperion UDP LED protocol (see leuchter/PLAN.md): a frame is
//   AA 00 FF <chk> R G B ...   (3 Bytes per LED, up to MAX_SEND_LEDS LEDs),
// sent as ONE UDP datagram to LEUCHTER_IP:UDP_PORT. The Leuchter sends the
// health byte 0x80 back to our IP on UDP_PORT - received as sync/stats only,
// the next frame is not waited for (see doc/AMBILIGHT_PROTOCOL.md: no retries,
// the next frame comes 100ms later anyway).

// Statistiken
static uint32_t g_sent_frames = 0;
static uint32_t g_acked_frames = 0;
static uint32_t g_failed_frames = 0;

WiFiUDP tx;   // Hyperion-frames to the Leuchter
WiFiUDP ack;  // health byte 0x80 back from the Leuchter

void udp_sender_init() {
  // send side: our IP as source, frames go to the Leuchter
  tx.begin(UDP_PORT);
  // receive side: the Leuchter sends 0x80 to our IP on the same UDP_PORT
  ack.begin(UDP_PORT);

  Serial.print("[UDP] Hyperion sender ready -> ");
  Serial.printf("%d.%d.%d.%d:%d, waiting for health byte 0x%02x\n",
                LEUCHTER_IP1, LEUCHTER_IP2, LEUCHTER_IP3, LEUCHTER_IP4,
                UDP_PORT, HEALTH_BYTE);
}

void udp_send_ambilight(const AmbilightResult& result) {
  if (!result.isValid) {
    return;  // Keine gültigen Daten
  }

  // Anzahl RGB-Tripels = Anzahl Rechtecke (clockwise order, see header)
  int n = (int)result.topColors.size() + (int)result.rightColors.size() +
          (int)result.bottomColors.size() + (int)result.leftColors.size();
  if (n <= 0) {
    return;
  }
  if (n > MAX_SEND_LEDS) {
    // the Leuchter rejects frames with more triplets than MAX_LEDS -
    // do not send a frame that is thrown away, warn only once
    static bool warned = false;
    if (!warned) {
      Serial.printf("[UDP] WARN: %d rects > MAX_SEND_LEDS %d, frame not sent\n",
                    n, MAX_SEND_LEDS);
      warned = true;
    }
    g_failed_frames++;
    return;
  }

  // Hyperion-frame: magic + checksum + RGB triplets, whole frame in one
  // datagram (max 4 + 3 * 150 = 454 Bytes, fits into one UDP packet)
  static uint8_t pkt[4 + MAX_SEND_LEDS * 3];
  int off = 4;

  auto push = [&](const RGB& c) {
    pkt[off++] = c.r;
    pkt[off++] = c.g;
    pkt[off++] = c.b;
  };

  // Clockwise loop, but starting at the strip's LED 0 at the TOP RIGHT corner:
  // Right (oben → unten) → Bottom (rechts → links) → Left (unten → oben) → Top (links → rechts)
  // (the stripe was remounted with LED 0 top right: LED 0..23 down the right
  //  edge, ..bottom right→left, ..left bottom→top, ..top left→right)
  for (const auto& c : result.rightColors) push(c);
  for (auto it = result.bottomColors.rbegin(); it != result.bottomColors.rend(); ++it) push(*it);
  for (auto it = result.leftColors.rbegin(); it != result.leftColors.rend(); ++it) push(*it);
  for (const auto& c : result.topColors) push(c);

  pkt[0] = 0xAA;  // magic
  pkt[1] = 0x00;  // no command
  pkt[2] = 0xFF;  // magic

  // checksum: low byte of sum(bytes[0..2] + all color bytes)
  // (the Leuchter's parseHyperion verifies exactly this)
  uint8_t sum = 0;
  sum += pkt[0] + pkt[1] + pkt[2];
  for (int i = 4; i < off; i++) sum += pkt[i];
  pkt[3] = sum;

  if (!tx.beginPacket(IPAddress(LEUCHTER_IP1, LEUCHTER_IP2,
                                  LEUCHTER_IP3, LEUCHTER_IP4), UDP_PORT)) {
    Serial.println("[UDP] ERROR: beginPacket failed");
    g_failed_frames++;
    return;
  }

  size_t written = tx.write(pkt, off);
  if (!tx.endPacket() || written != (size_t)off) {
    Serial.println("[UDP] Send failed");
    g_failed_frames++;
    return;
  }

  g_sent_frames++;
}

// Non-blocking: has the health byte 0x80 from the Leuchter arrived?
void udp_sender_poll_ack() {
  int len = ack.parsePacket();
  if (len <= 0) {
    return;
  }
  uint8_t b[8];
  int got = ack.read(b, len < 8 ? len : 8);
  if (got > 0 && b[0] == HEALTH_BYTE) {
    g_acked_frames++;
  }
}

uint32_t udp_sender_get_sent_frames() {
  return g_sent_frames;
}

uint32_t udp_sender_get_acked_frames() {
  return g_acked_frames;
}

uint32_t udp_sender_get_failed_frames() {
  return g_failed_frames;
}
