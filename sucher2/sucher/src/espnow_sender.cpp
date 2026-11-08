#include "espnow_sender.h"
#include <esp_now.h>
#include <WiFi.h>

// Statistiken
static uint32_t g_sent_frames = 0;
static uint32_t g_failed_frames = 0;

// Broadcast MAC-Adresse (sendet an alle Empfänger)
static uint8_t broadcast_address[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ESP-NOW Send-Callback
void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        // Erfolg - nichts zu tun
    } else {
        Serial.println("[ESP-NOW] Send failed");
    }
}

void espnow_sender_init() {
    // ESP-NOW initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] ERROR: Init failed");
        return;
    }
    
    Serial.println("[ESP-NOW] Initialized");
    
    // Callback registrieren
    esp_now_register_send_cb(on_data_sent);
    
    // Peer (Broadcast) hinzufügen
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, broadcast_address, 6);
    peerInfo.channel = 0;  // Aktueller WiFi-Kanal
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] ERROR: Failed to add peer");
        return;
    }
    
    Serial.println("[ESP-NOW] Broadcast peer added");
}

void espnow_send_ambilight(const AmbilightResult& result, int h_seg, int v_seg) {
    if (!result.isValid) {
        return;  // Keine gültigen Daten
    }
    
    // Berechne Gesamtanzahl Rechtecke (im Uhrzeigersinn)
    int total_rectangles = 2 * h_seg + 2 * (v_seg - 2);
    
    // RGB-Daten in lineares Array packen (Uhrzeigersinn: Top → Right → Bottom → Left)
    uint8_t rgb_data[total_rectangles * 3];
    int offset = 0;
    
    // Top (links nach rechts)
    for (size_t i = 0; i < result.topColors.size(); i++) {
        rgb_data[offset++] = result.topColors[i].r;
        rgb_data[offset++] = result.topColors[i].g;
        rgb_data[offset++] = result.topColors[i].b;
    }
    
    // Right (oben nach unten, ohne Ecken)
    for (size_t i = 0; i < result.rightColors.size(); i++) {
        rgb_data[offset++] = result.rightColors[i].r;
        rgb_data[offset++] = result.rightColors[i].g;
        rgb_data[offset++] = result.rightColors[i].b;
    }
    
    // Bottom (rechts nach links, rückwärts!)
    for (int i = result.bottomColors.size() - 1; i >= 0; i--) {
        rgb_data[offset++] = result.bottomColors[i].r;
        rgb_data[offset++] = result.bottomColors[i].g;
        rgb_data[offset++] = result.bottomColors[i].b;
    }
    
    // Left (unten nach oben, rückwärts!)
    for (int i = result.leftColors.size() - 1; i >= 0; i--) {
        rgb_data[offset++] = result.leftColors[i].r;
        rgb_data[offset++] = result.leftColors[i].g;
        rgb_data[offset++] = result.leftColors[i].b;
    }
    
    // Paket-Größe berechnen
    // ESP-NOW Limit: 250 Bytes pro Paket
    // Paket 0: packet_num(1) + total_packets(1) + h_seg(1) + v_seg(1) = 4 Bytes Header
    // Folgepaket: packet_num(1) + total_packets(1) = 2 Bytes Header
    
    const int max_payload = 250;
    const int packet0_header = 4;
    const int packet_header = 2;
    const int packet0_data_size = max_payload - packet0_header;  // 246 Bytes
    const int packet_data_size = max_payload - packet_header;    // 248 Bytes
    
    int total_data = offset;  // total_rectangles * 3
    int packet0_payload = min(packet0_data_size, total_data);
    int remaining = total_data - packet0_payload;
    
    int total_packets = 1;  // Mindestens 1 Paket
    if (remaining > 0) {
        total_packets += (remaining + packet_data_size - 1) / packet_data_size;
    }
    
    // Sende Pakete
    uint8_t packet[max_payload];
    int data_offset = 0;
    
    for (int packet_num = 0; packet_num < total_packets; packet_num++) {
        int header_size;
        int payload_size;
        
        if (packet_num == 0) {
            // Paket 0: Enthält h_seg und v_seg
            packet[0] = 0;  // packet_num
            packet[1] = total_packets;
            packet[2] = h_seg;
            packet[3] = v_seg;
            header_size = packet0_header;
            payload_size = min(packet0_data_size, total_data - data_offset);
        } else {
            // Folgepaket
            packet[0] = packet_num;
            packet[1] = total_packets;
            header_size = packet_header;
            payload_size = min(packet_data_size, total_data - data_offset);
        }
        
        // RGB-Daten kopieren
        memcpy(&packet[header_size], &rgb_data[data_offset], payload_size);
        data_offset += payload_size;
        
        // Paket senden
        esp_err_t result = esp_now_send(broadcast_address, packet, header_size + payload_size);
        
        if (result != ESP_OK) {
            Serial.printf("[ESP-NOW] Send error packet %d: %d\n", packet_num, result);
            g_failed_frames++;
            return;
        }
        
        // Kurze Pause zwischen Paketen (ESP-NOW braucht Zeit)
        if (packet_num < total_packets - 1) {
            delay(1);
        }
    }
    
    g_sent_frames++;
    
    if (total_packets > 1) {
        Serial.printf("[ESP-NOW] Sent %d packets (%d bytes)\n", total_packets, total_data);
    }
}

uint32_t espnow_get_sent_frames() {
    return g_sent_frames;
}

uint32_t espnow_get_failed_frames() {
    return g_failed_frames;
}
