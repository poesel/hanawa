#ifndef UDP_SENDER_H
#define UDP_SENDER_H

#include <Arduino.h>
#include "windows.h"
#include "config.h"

// Hyperion UDP-Sender for the Sucher (ESP32CAM)
// Implements the sender side of the UDP protocol the Leuchter
// receives (see leuchter/PLAN.md and leuchter/src/main.cpp):
//
// One UDP datagram per frame to UDP_PORT at LEUCHTER_IP:
//   [0] 0xAA (magic) [1] 0x00 (no command) [2] 0xFF (magic)
//   [3] checksum (low byte of sum of bytes[0..2] + all color bytes)
//   [4..] RGB triplets, LED 0..n-1
//
// The RGB order is clockwise, but starts at the strip's LED 0 at the TOP
// RIGHT corner (final mounting): Right top→bottom, Bottom right→left,
// Left bottom→top, Top left→right. With hSeg=40, vSeg=26 the 128-LED
// stripe gets exactly one triplet per LED (40+40 top/bottom, 24+24 sides).
// The Leuchter answers each applied frame with a one-byte UDP packet
// 0x80 (HEALTH_BYTE) back to the sender IP + sender port → counted
// as stats for frame-sync. No ESP-NOW and no packet split any more.

void udp_sender_init();

// Sendet Ambilight-Daten as one Hyperion UDP frame to the Leuchter
void udp_send_ambilight(const AmbilightResult& result);

// Checks whether the health byte 0x80 came back since the last call
// (call regularly in loop())
void udp_sender_poll_ack();

// Brightness slider (0..100 %, 0 = off). The sender scales every RGB
// triplet of a frame by this factor before packing, so the Leuchter gets
// dimmed or black frames (0 % = all LEDs off). Set from the web UI via
// POST /api/brightness.
void udp_sender_set_brightness(int percent);
int udp_sender_get_brightness();

// Gibt Statistiken zurück
uint32_t udp_sender_get_sent_frames();
uint32_t udp_sender_get_acked_frames();
uint32_t udp_sender_get_failed_frames();

#endif // UDP_SENDER_H
