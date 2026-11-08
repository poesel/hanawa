#include "protocol.h"

// Globaler Frame-State
AmbilightFrame g_frame = {0};

void protocol_init() {
    memset(&g_frame, 0, sizeof(AmbilightFrame));
    g_frame.expected_next_packet = 0;
    g_frame.valid = false;
    
    Serial.println("[Protocol] Initialisiert");
}

bool protocol_validate_packet(const uint8_t* data, int len, uint8_t packet_num) {
    // Minimale Länge prüfen
    if (packet_num == 0) {
        if (len < 4) {
            Serial.println("[Protocol] ERROR: Paket 0 zu kurz (min. 4 Bytes)");
            return false;
        }
    } else {
        if (len < 2) {
            Serial.println("[Protocol] ERROR: Folgepaket zu kurz (min. 2 Bytes)");
            return false;
        }
    }
    
    uint8_t total_packets = data[1];
    
    // Paket-Nummer gültig?
    if (packet_num >= total_packets) {
        Serial.printf("[Protocol] ERROR: packet_num (%d) >= total_packets (%d)\n", 
                      packet_num, total_packets);
        return false;
    }
    
    // Mindestens 1 Paket
    if (total_packets == 0) {
        Serial.println("[Protocol] ERROR: total_packets ist 0");
        return false;
    }
    
    // Bei Paket 0: Segment-Werte prüfen
    if (packet_num == 0) {
        uint8_t h_segments = data[2];
        uint8_t v_segments = data[3];
        
        if (h_segments == 0 || v_segments < 2) {
            Serial.printf("[Protocol] ERROR: Ungültige Segmente: h=%d, v=%d\n", 
                          h_segments, v_segments);
            return false;
        }
        
        int total_rectangles = 2 * h_segments + 2 * (v_segments - 2);
        if (total_rectangles > MAX_RECTANGLES) {
            Serial.printf("[Protocol] ERROR: Zu viele Rechtecke: %d (max: %d)\n",
                          total_rectangles, MAX_RECTANGLES);
            return false;
        }
    }
    
    return true;
}

// ESP8266 nutzt unterschiedliche Signatur
void protocol_on_receive(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (!data || len < 2) {
        Serial.println("[Protocol] ERROR: Ungültige Daten");
        return;
    }
    
    uint8_t packet_num = data[0];
    uint8_t total_packets = data[1];
    
    // Validierung
    if (!protocol_validate_packet(data, len, packet_num)) {
        return;
    }
    
    // === PAKET 0 (Neuer Frame Start) ===
    if (packet_num == 0) {
        uint8_t h_segments = data[2];
        uint8_t v_segments = data[3];
        
        // Frame-State zurücksetzen
        g_frame.h_segments = h_segments;
        g_frame.v_segments = v_segments;
        g_frame.total_packets = total_packets;
        g_frame.expected_next_packet = 0;
        g_frame.data_offset = 0;
        g_frame.valid = false;
        g_frame.first_packet_time = millis();
        
        // RGB-Daten aus Paket 0 kopieren (ab Byte 4)
        int payload_len = len - 4;
        memcpy(&g_frame.rgb_data[0], &data[4], payload_len);
        g_frame.data_offset += payload_len;
        g_frame.expected_next_packet = 1;
        
        Serial.printf("[Protocol] Frame Start: %dx%d Segmente, %d Pakete, %d Bytes Payload\n",
                      h_segments, v_segments, total_packets, payload_len);
        
        // Wenn nur 1 Paket: Sofort gültig
        if (total_packets == 1) {
            g_frame.valid = true;
            g_frame.last_update_time = millis();
            g_frame.frame_counter++;
            Serial.printf("[Protocol] Single-Paket Frame komplett (Frame #%u)\n", 
                          g_frame.frame_counter);
        }
        return;
    }
    
    // === FOLGEPAKET (packet_num > 0) ===
    
    // Validierung: Erwarten wir dieses Paket?
    if (packet_num != g_frame.expected_next_packet) {
        Serial.printf("[Protocol] ERROR: Erwarte Paket %d, erhalten %d -> Frame verworfen\n",
                      g_frame.expected_next_packet, packet_num);
        g_frame.valid = false;
        g_frame.expected_next_packet = 0;
        return;
    }
    
    // Validierung: Stimmt total_packets überein?
    if (total_packets != g_frame.total_packets) {
        Serial.printf("[Protocol] ERROR: total_packets mismatch (%d != %d) -> Frame verworfen\n",
                      total_packets, g_frame.total_packets);
        g_frame.valid = false;
        g_frame.expected_next_packet = 0;
        return;
    }
    
    // RGB-Daten aus Folgepaket kopieren (ab Byte 2)
    int payload_len = len - 2;
    if (g_frame.data_offset + payload_len > MAX_RECTANGLES * 3) {
        Serial.println("[Protocol] ERROR: Buffer overflow -> Frame verworfen");
        g_frame.valid = false;
        g_frame.expected_next_packet = 0;
        return;
    }
    
    memcpy(&g_frame.rgb_data[g_frame.data_offset], &data[2], payload_len);
    g_frame.data_offset += payload_len;
    g_frame.expected_next_packet++;
    
    Serial.printf("[Protocol] Paket %d/%d empfangen (%d Bytes)\n", 
                  packet_num + 1, total_packets, payload_len);
    
    // === FRAME KOMPLETT? ===
    if (g_frame.expected_next_packet == g_frame.total_packets) {
        g_frame.valid = true;
        g_frame.last_update_time = millis();
        g_frame.frame_counter++;
        uint32_t duration = millis() - g_frame.first_packet_time;
        Serial.printf("[Protocol] Frame komplett nach %d ms (Frame #%u)\n", 
                      duration, g_frame.frame_counter);
    }
}

void protocol_check_timeout() {
    // Prüfe nur, wenn wir auf Folgepakete warten
    if (g_frame.expected_next_packet > 0 && 
        g_frame.expected_next_packet < g_frame.total_packets) {
        
        if (millis() - g_frame.first_packet_time > FRAME_TIMEOUT_MS) {
            Serial.printf("[Protocol] TIMEOUT: Nur %d/%d Pakete empfangen -> Frame verworfen\n",
                          g_frame.expected_next_packet, g_frame.total_packets);
            g_frame.valid = false;
            g_frame.expected_next_packet = 0;
        }
    }
}

int protocol_get_total_rectangles() {
    if (!g_frame.valid) return 0;
    return 2 * g_frame.h_segments + 2 * (g_frame.v_segments - 2);
}

float protocol_get_fps() {
    // Vereinfachte FPS-Berechnung basierend auf Frame-Counter
    static uint32_t last_check = 0;
    static uint32_t last_frame_count = 0;
    
    uint32_t now = millis();
    if (now - last_check > 1000) {
        float fps = (g_frame.frame_counter - last_frame_count) * 1000.0 / (now - last_check);
        last_check = now;
        last_frame_count = g_frame.frame_counter;
        return fps;
    }
    
    return 0.0; // Noch keine Messung
}

bool protocol_is_receiving() {
    // Empfangen wir aktiv Daten? (Letztes Update < 500ms)
    return (millis() - g_frame.last_update_time) < 500;
}
