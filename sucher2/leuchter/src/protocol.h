#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

// Ambilight-Protokoll v1.0
// Siehe: esp32cam_webserver/doc/AMBILIGHT_PROTOCOL.md

#define MAX_RECTANGLES 100  // Maximal unterstützte Rechtecke (reduziert für ESP8266)
#define FRAME_TIMEOUT_MS 200  // Timeout für unvollständige Frames

// Frame-Status
struct AmbilightFrame {
    uint8_t h_segments;          // Anzahl horizontale Segmente
    uint8_t v_segments;          // Anzahl vertikale Segmente
    uint8_t total_packets;       // Erwartete Anzahl Pakete
    uint8_t expected_next_packet; // Nächste erwartete Paketnummer
    uint8_t rgb_data[MAX_RECTANGLES * 3];  // RGB-Buffer
    int data_offset;             // Aktueller Schreib-Offset im Buffer
    bool valid;                  // Frame komplett und gültig?
    uint32_t first_packet_time;  // Timestamp erstes Paket (für Timeout)
    uint32_t last_update_time;   // Timestamp letzte erfolgreiche Aktualisierung
    uint32_t frame_counter;      // Anzahl erfolgreich empfangener Frames
};

// Globaler Frame-State
extern AmbilightFrame g_frame;

// Initialisierung
void protocol_init();

// ESP-NOW Callback (wird von main.cpp registriert)
void protocol_on_receive(const uint8_t* mac_addr, const uint8_t* data, int len);

// Prüft Timeout und verwirft unvollständige Frames
void protocol_check_timeout();

// Validierung
bool protocol_validate_packet(const uint8_t* data, int len, uint8_t packet_num);

// Statistik
int protocol_get_total_rectangles();
float protocol_get_fps();
bool protocol_is_receiving();

#endif // PROTOCOL_H
