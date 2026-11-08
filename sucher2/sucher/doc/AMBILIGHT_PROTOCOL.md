# Ambilight Datenprotokoll v1.0

## Übersicht

Dieses Dokument beschreibt das binäre Protokoll zur Übertragung von Ambilight-Farbdaten zwischen zwei ESP32-Geräten via ESP-NOW.

## Anwendungsfall

- **Sender**: ESP32-CAM berechnet Ambilight-Farben aus Videobild
- **Empfänger**: ESP32 steuert LED-Streifen basierend auf den empfangenen Farben
- **Übertragung**: Alle 100ms (10 FPS)
- **Transportprotokoll**: ESP-NOW (max. 250 Bytes pro Paket)

## Paket-Struktur

### Erstes Paket (packet_num = 0)

**Header (4 Bytes):**

| Byte | Name | Typ | Beschreibung |
|------|------|-----|--------------|
| 0 | `packet_num` | uint8_t | 0x00 (Erstes Paket) |
| 1 | `total_packets` | uint8_t | Gesamtanzahl der Pakete für diesen Frame |
| 2 | `h_segments` | uint8_t | Anzahl horizontale Rechtecke (Top + Bottom) |
| 3 | `v_segments` | uint8_t | Anzahl vertikale Rechtecke (Left + Right, ohne Ecken) |

**Payload:** RGB-Daten ab Byte 4

### Folgende Pakete (packet_num > 0)

**Header (2 Bytes):**

| Byte | Name | Typ | Beschreibung |
|------|------|-----|--------------|
| 0 | `packet_num` | uint8_t | Aktuelle Paketnummer (1, 2, 3, ...) |
| 1 | `total_packets` | uint8_t | Gesamtanzahl der Pakete (zur Validierung) |

**Payload:** RGB-Daten ab Byte 2

### Payload (Variable Länge)

Nach dem Header folgen RGB-Tripel (je 3 Bytes) für jedes Rechteck:

| Bytes | Name | Typ | Beschreibung |
|-------|------|-----|--------------|
| n | `rgb[k]` | uint8_t[3] | Rechteck k: R, G, B (0-255) |
| n+3 | `rgb[k+1]` | uint8_t[3] | Rechteck k+1: R, G, B |
| ... | ... | ... | ... |

## Rechteck-Reihenfolge (Uhrzeigersinn)

Die Rechtecke werden in folgender Reihenfolge übertragen, beginnend **links oben** im Uhrzeigersinn:

```
    TOP: [0] [1] [2] [3] [4] [5] [6] [7] [8] [9]
         ↓                                      ↓
LEFT: [26]                                   RIGHT: [10]
      [27]                                         [11]
      [28]                                         [12]
      [29]                                         [13]
      [30]                                         [14]
      [31]                                         [15]
         ↑                                      ↓
    BOTTOM: [25] [24] [23] [22] [21] [20] [19] [18] [17] [16]
            ←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←←
```

### Detaillierte Indizes (Beispiel: 10h x 8v)

| Seite | Array im Code | Reihenfolge | Start-Index | End-Index | Anzahl |
|-------|--------------|-------------|-------------|-----------|--------|
| **Top** | `topColors[0...9]` | Links → Rechts | 0 | 9 | 10 |
| **Right** | `rightColors[0...5]` | Oben → Unten | 10 | 15 | 6 |
| **Bottom** | `bottomColors[9...0]` | Rechts → Links (rückwärts!) | 16 | 25 | 10 |
| **Left** | `leftColors[5...0]` | Unten → Oben (rückwärts!) | 26 | 31 | 6 |

**Wichtig**: Bottom und Left Arrays werden **rückwärts** durchlaufen!

### Berechnung der Gesamtanzahl

```
total_rectangles = 2 * h_segments + 2 * (v_segments - 2)
```

**Beispiel**: `h=10, v=8` → `2*10 + 2*(8-2)` = `20 + 12` = **32 Rechtecke**

**Warum `v_segments - 2`?**  
Die Ecken gehören zu Top/Bottom → Left/Right überspringen erste und letzte Zeile

### Paket-Kapazität Referenz

| Rechtecke | Bytes | Pakete | Paket 0 | Paket 1 | Paket 2 |
|-----------|-------|--------|---------|---------|---------|
| 32 | 100 | 1 | 4+96 | - | - |
| 50 | 154 | 1 | 4+150 | - | - |
| 82 | 250 | 1 | 4+246 | - | - |
| 83 | 253 | 2 | 4+246 | 2+1 | - |
| 100 | 304 | 2 | 4+246 | 2+54 | - |
| 150 | 454 | 2 | 4+246 | 2+204 | - |
| 165 | 499 | 2 | 4+246 | 2+249 | - |
| 166 | 502 | 3 | 4+246 | 2+248 | 2+4 |

**Maximale Rechtecke**:
- **1 Paket**: 82 Rechtecke (246 Bytes)
- **2 Pakete**: 164 Rechtecke (246 + 248 = 494 Bytes)
- **3 Pakete**: 246 Rechtecke (246 + 248 + 248 = 742 Bytes)

## Paket-Fragmentierung

### Berechnung der Pakete

**Erstes Paket (packet_num = 0):**
```
bytes_per_packet = 250 (ESP-NOW Maximum)
header_size = 4 bytes (inkl. h_segments, v_segments)
payload_size = 250 - 4 = 246 bytes
rectangles_per_packet = 246 / 3 = 82 Rechtecke
```

**Folgende Pakete (packet_num > 0):**
```
header_size = 2 bytes (nur packet_num, total_packets)
payload_size = 250 - 2 = 248 bytes
rectangles_per_packet = 248 / 3 = 82 Rechtecke (+ 2 Bytes übrig)
```

### Beispiel: 32 Rechtecke

- **Gesamtdatenmenge**: 4 Byte Header + 32 * 3 Byte RGB = 100 Bytes
- **Erforderliche Pakete**: 1
  - Paket 0: Header (4) + 32 Rechtecke (96 Bytes) = 100 Bytes

### Beispiel: 100 Rechtecke

- **Gesamtdatenmenge**: 4 + (100*3) = 304 Bytes
- **Erforderliche Pakete**: 2
  - Paket 0: Header (4) + 82 Rechtecke (246 Bytes) = 250 Bytes
  - Paket 1: Header (2) + 18 Rechtecke (54 Bytes) = 56 Bytes

## Beispiel-Pakete

### Single-Paket (32 Rechtecke, 10h x 8v)

```
Byte    Hex           Beschreibung
----    ---           ------------
0       0x00          packet_num = 0 (erstes Paket)
1       0x01          total_packets = 1 (nur ein Paket)
2       0x0A          h_segments = 10
3       0x08          v_segments = 8

=== TOP (10 Rechtecke, links→rechts) ===
4-6     0xFF 0x00 0x00    Rechteck 0 (Top[0]): Rot
7-9     0xFF 0x40 0x00    Rechteck 1 (Top[1]): Orange
10-12   0xFF 0x80 0x00    Rechteck 2 (Top[2]): Gelb-Orange
...
31-33   0x00 0xFF 0xFF    Rechteck 9 (Top[9]): Cyan

=== RIGHT (6 Rechtecke, oben→unten) ===
34-36   0x00 0x80 0xFF    Rechteck 10 (Right[0]): Hell-Blau
37-39   0x00 0x60 0xFF    Rechteck 11 (Right[1]): Blau
...
49-51   0x80 0x00 0xFF    Rechteck 15 (Right[5]): Magenta

=== BOTTOM (10 Rechtecke, rechts→links RÜCKWÄRTS!) ===
52-54   0xFF 0x00 0xFF    Rechteck 16 (Bottom[9]): Magenta
55-57   0xFF 0x00 0xC0    Rechteck 17 (Bottom[8]): Rosa
...
79-81   0xFF 0x00 0x00    Rechteck 25 (Bottom[0]): Rot

=== LEFT (6 Rechtecke, unten→oben RÜCKWÄRTS!) ===
82-84   0xFF 0x20 0x00    Rechteck 26 (Left[5]): Rot-Orange
85-87   0xFF 0x30 0x00    Rechteck 27 (Left[4]): Orange
...
97-99   0xFF 0x60 0x00    Rechteck 31 (Left[0]): Orange-Gelb

Gesamtgröße: 4 + 32*3 = 100 Bytes
```

### Multi-Paket (100 Rechtecke)

**Paket 0 (Erstes Paket - mit Segment-Info):**
```
Byte    Hex     Beschreibung
----    ---     ------------
0       0x00    packet_num = 0
1       0x02    total_packets = 2
2       0x32    h_segments = 50
3       0x0A    v_segments = 10

4-6     ...     Rechteck 0
7-9     ...     Rechteck 1
...
247-249 ...     Rechteck 81

Größe: 250 Bytes (4 Byte Header + 82 Rechtecke)
```

**Paket 1 (Folgepaket - OHNE Segment-Info):**
```
Byte    Hex     Beschreibung
----    ---     ------------
0       0x01    packet_num = 1
1       0x02    total_packets = 2

2-4     ...     Rechteck 82
5-7     ...     Rechteck 83
...
53-55   ...     Rechteck 99

Größe: 56 Bytes (2 Byte Header + 18 Rechtecke)
```

## Sender-Implementierung

### Pseudocode

```cpp
void sendAmbilightData() {
    // Berechne Gesamtanzahl Rechtecke
    int h_seg = 10;
    int v_seg = 8;
    int total_rects = 2 * h_seg + 2 * (v_seg - 2); // 32 Rechtecke
    int total_bytes = total_rects * 3; // 96 Bytes RGB
    
    // Berechne erforderliche Pakete
    int first_packet_capacity = 250 - 4; // 246 Bytes (Header: 4 Bytes)
    int next_packet_capacity = 250 - 2;  // 248 Bytes (Header: 2 Bytes)
    
    int total_packets;
    if (total_bytes <= first_packet_capacity) {
        total_packets = 1;
    } else {
        int remaining = total_bytes - first_packet_capacity;
        total_packets = 1 + ((remaining + next_packet_capacity - 1) / next_packet_capacity);
    }
    
    // === PAKET 0 SENDEN ===
    uint8_t packet0[250];
    packet0[0] = 0;              // packet_num
    packet0[1] = total_packets;  // total_packets
    packet0[2] = h_seg;          // h_segments
    packet0[3] = v_seg;          // v_segments
    
    // RGB-Daten in Uhrzeigersinn kopieren
    int offset = 4;
    int rgb_index = 0;
    
    // Top (links → rechts)
    for (int i = 0; i < h_seg && offset < 250; i++) {
        packet0[offset++] = topColors[i].r;
        packet0[offset++] = topColors[i].g;
        packet0[offset++] = topColors[i].b;
        rgb_index++;
    }
    
    // Right (oben → unten)
    for (int i = 0; i < (v_seg - 2) && offset < 250; i++) {
        packet0[offset++] = rightColors[i].r;
        packet0[offset++] = rightColors[i].g;
        packet0[offset++] = rightColors[i].b;
        rgb_index++;
    }
    
    // Bottom (rechts → links)
    for (int i = h_seg - 1; i >= 0 && offset < 250; i--) {
        packet0[offset++] = bottomColors[i].r;
        packet0[offset++] = bottomColors[i].g;
        packet0[offset++] = bottomColors[i].b;
        rgb_index++;
    }
    
    // Left (unten → oben)
    for (int i = (v_seg - 2) - 1; i >= 0 && offset < 250; i--) {
        packet0[offset++] = leftColors[i].r;
        packet0[offset++] = leftColors[i].g;
        packet0[offset++] = leftColors[i].b;
        rgb_index++;
    }
    
    esp_now_send(receiverMAC, packet0, offset);
    
    // === FOLGEPAKETE (falls nötig) ===
    if (total_packets > 1) {
        for (int pkt = 1; pkt < total_packets; pkt++) {
            uint8_t packet[250];
            packet[0] = pkt;             // packet_num
            packet[1] = total_packets;   // total_packets
            
            int pkt_offset = 2;
            // ... weitere RGB-Daten kopieren ...
            
            esp_now_send(receiverMAC, packet, pkt_offset);
        }
    }
}
```

**Wichtig**: 
- Sender sendet jeden Frame **genau einmal**
- Keine Retries bei Paketverlust
- Bei 10 FPS kommt nächster Frame in 100ms

### Praktische Implementierung (C++)

```cpp
// Aus bestehenden Vektoren (g_ambilightResult) ein Paket erstellen
void createAmbilightPacket(uint8_t* packet, int* packet_size) {
    int h_seg = g_ambilightConfig.hSeg;
    int v_seg = g_ambilightConfig.vSeg;
    
    // Header
    packet[0] = 0;  // packet_num (immer 0 für Single-Paket)
    packet[1] = 1;  // total_packets
    packet[2] = h_seg;
    packet[3] = v_seg;
    
    int offset = 4;
    
    // 1. Top (links → rechts)
    for (int i = 0; i < h_seg; i++) {
        packet[offset++] = g_ambilightResult.topColors[i].r;
        packet[offset++] = g_ambilightResult.topColors[i].g;
        packet[offset++] = g_ambilightResult.topColors[i].b;
    }
    
    // 2. Right (oben → unten)
    for (int i = 0; i < (v_seg - 2); i++) {
        packet[offset++] = g_ambilightResult.rightColors[i].r;
        packet[offset++] = g_ambilightResult.rightColors[i].g;
        packet[offset++] = g_ambilightResult.rightColors[i].b;
    }
    
    // 3. Bottom (rechts → links, RÜCKWÄRTS!)
    for (int i = h_seg - 1; i >= 0; i--) {
        packet[offset++] = g_ambilightResult.bottomColors[i].r;
        packet[offset++] = g_ambilightResult.bottomColors[i].g;
        packet[offset++] = g_ambilightResult.bottomColors[i].b;
    }
    
    // 4. Left (unten → oben, RÜCKWÄRTS!)
    for (int i = (v_seg - 2) - 1; i >= 0; i--) {
        packet[offset++] = g_ambilightResult.leftColors[i].r;
        packet[offset++] = g_ambilightResult.leftColors[i].g;
        packet[offset++] = g_ambilightResult.leftColors[i].b;
    }
    
    *packet_size = offset;
}
```

## Empfänger-Implementierung

### Empfangsregeln

1. **Nur erstes Paket enthält Segment-Info**: `h_segments` und `v_segments` sind nur in Paket 0
2. **Strikte Reihenfolge**: Pakete müssen in der Reihenfolge 0, 1, 2, ... empfangen werden
3. **Keine Duplikate**: Jedes `packet_num` darf nur einmal empfangen werden
4. **Atomare Gültigkeit**: Daten werden erst bei Empfang ALLER Pakete gültig
5. **Fehlerbehandlung**: Bei falscher Reihenfolge → ALLE Pakete verwerfen
6. **Keine Retries**: Sender sendet jeden Frame nur einmal

### Pseudocode

```cpp
struct AmbilightFrame {
    uint8_t h_segments;
    uint8_t v_segments;
    uint8_t total_packets;
    uint8_t expected_next_packet;  // Erwartet: 0, 1, 2, ...
    uint8_t rgb_data[MAX_RECTANGLES * 3];
    int data_offset;
    bool valid;
    uint32_t first_packet_time;
};

AmbilightFrame frame = {0};

void onReceivePacket(uint8_t* data, int len) {
    uint8_t packet_num = data[0];
    uint8_t total_packets = data[1];
    
    // === PAKET 0 (Neuer Frame) ===
    if (packet_num == 0) {
        if (len < 4) {
            Serial.println("ERROR: Paket 0 zu kurz");
            return;
        }
        
        // Frame-State zurücksetzen
        frame.h_segments = data[2];
        frame.v_segments = data[3];
        frame.total_packets = total_packets;
        frame.expected_next_packet = 0;
        frame.data_offset = 0;
        frame.valid = false;
        frame.first_packet_time = millis();
        
        // RGB-Daten aus Paket 0 kopieren (ab Byte 4)
        int payload_len = len - 4;
        memcpy(&frame.rgb_data[0], &data[4], payload_len);
        frame.data_offset += payload_len;
        frame.expected_next_packet = 1;
        
        Serial.printf("Frame Start: %dx%d, %d Pakete\n", 
                      frame.h_segments, frame.v_segments, total_packets);
        
        // Wenn nur 1 Paket: Sofort gültig
        if (total_packets == 1) {
            frame.valid = true;
            updateLEDs();
        }
        return;
    }
    
    // === FOLGEPAKET (packet_num > 0) ===
    
    // Validierung: Erwarten wir dieses Paket?
    if (packet_num != frame.expected_next_packet) {
        Serial.printf("ERROR: Erwarte Paket %d, erhalten %d -> Frame verworfen\n",
                      frame.expected_next_packet, packet_num);
        frame.valid = false;
        frame.expected_next_packet = 0; // Warte auf neuen Frame
        return;
    }
    
    // Validierung: Stimmt total_packets überein?
    if (total_packets != frame.total_packets) {
        Serial.printf("ERROR: total_packets mismatch -> Frame verworfen\n");
        frame.valid = false;
        frame.expected_next_packet = 0;
        return;
    }
    
    // RGB-Daten aus Folgepaket kopieren (ab Byte 2)
    int payload_len = len - 2;
    memcpy(&frame.rgb_data[frame.data_offset], &data[2], payload_len);
    frame.data_offset += payload_len;
    frame.expected_next_packet++;
    
    Serial.printf("Paket %d/%d empfangen\n", packet_num + 1, total_packets);
    
    // === FRAME KOMPLETT? ===
    if (frame.expected_next_packet == frame.total_packets) {
        frame.valid = true;
        uint32_t duration = millis() - frame.first_packet_time;
        Serial.printf("Frame komplett nach %d ms\n", duration);
        updateLEDs();
    }
}

void updateLEDs() {
    if (!frame.valid) {
        Serial.println("ERROR: Frame ungültig, LEDs nicht aktualisiert");
        return;
    }
    
    int total_rects = 2 * frame.h_segments + 2 * (frame.v_segments - 2);
    Serial.printf("Update %d LEDs\n", total_rects);
    
    for (int i = 0; i < total_rects; i++) {
        uint8_t r = frame.rgb_data[i * 3 + 0];
        uint8_t g = frame.rgb_data[i * 3 + 1];
        uint8_t b = frame.rgb_data[i * 3 + 2];
        // ... LED setzen ...
    }
}
```

## Fehlerbehandlung

### Empfänger-Regeln

1. **Strikte Sequenz**: Pakete MÜSSEN in Reihenfolge 0, 1, 2, ... ankommen
2. **Keine Retries**: Sender sendet nur einmal → bei Verlust ist Frame ungültig
3. **Verwerfen bei Fehler**: 
   - Falsches `packet_num` → Frame verwerfen, auf Paket 0 warten
   - `total_packets` Mismatch → Frame verwerfen
   - Timeout (>200ms) → Frame verwerfen, auf Paket 0 warten
4. **Atomare Gültigkeit**: LEDs werden NUR bei vollständigem Frame aktualisiert

### Validierung

```cpp
bool validatePacket(uint8_t* data, int len, uint8_t packet_num) {
    // Minimale Länge prüfen
    if (packet_num == 0) {
        if (len < 4) {
            Serial.println("ERROR: Paket 0 zu kurz (min. 4 Bytes)");
            return false;
        }
    } else {
        if (len < 2) {
            Serial.println("ERROR: Folgepaket zu kurz (min. 2 Bytes)");
            return false;
        }
    }
    
    uint8_t total_packets = data[1];
    
    // Paket-Nummer gültig?
    if (packet_num >= total_packets) {
        Serial.println("ERROR: packet_num >= total_packets");
        return false;
    }
    
    // Mindestens 1 Paket
    if (total_packets == 0) {
        Serial.println("ERROR: total_packets ist 0");
        return false;
    }
    
    // Bei Paket 0: Segment-Werte prüfen
    if (packet_num == 0) {
        uint8_t h_segments = data[2];
        uint8_t v_segments = data[3];
        
        if (h_segments == 0 || v_segments < 2) {
            Serial.println("ERROR: Ungültige Segment-Werte");
            return false;
        }
        
        int total_rectangles = 2 * h_segments + 2 * (v_segments - 2);
        if (total_rectangles > 250) {
            Serial.println("ERROR: Zu viele Rechtecke");
            return false;
        }
    }
    
    return true;
}
```

### Timeout-Behandlung

```cpp
// In loop() aufrufen
void checkFrameTimeout() {
    if (frame.expected_next_packet > 0 && frame.expected_next_packet < frame.total_packets) {
        // Wir warten auf ein Folgepaket
        if (millis() - frame.first_packet_time > 200) {
            Serial.printf("TIMEOUT: Nur %d/%d Pakete empfangen -> Frame verworfen\n",
                          frame.expected_next_packet, frame.total_packets);
            frame.valid = false;
            frame.expected_next_packet = 0; // Warte auf neues Paket 0
        }
    }
}
```

## Performance

### Typische Werte (10h x 8v = 32 Rechtecke)

- **Rechtecke gesamt**: 32
- **Datengröße**: 4 Byte Header + 96 Byte RGB = **100 Bytes**
- **Pakete**: 1 (passt komplett in ein ESP-NOW Paket)
- **Übertragungszeit (ESP-NOW)**: ~1-3ms pro Frame
- **Frequenz**: 10 FPS (100ms Intervall)
- **Bandbreite**: 100 Bytes * 10 Hz = **1 kB/s**
- **Erfolgsrate**: ~99% bei stabilem WiFi-Kanal

### Performance bei größeren Konfigurationen

**Beispiel: 50h x 10v = 116 Rechtecke**

- **Datengröße**: 4 + 2 + (116*3) = **354 Bytes**
- **Pakete**: 2 (Paket 0: 250 Bytes, Paket 1: 104 Bytes)
- **Übertragungszeit**: ~3-6ms (beide Pakete)
- **Risiko**: Paketverlust steigt mit Anzahl der Pakete
- **Empfehlung**: Für 100ms Intervall maximal 2-3 Pakete

## Implementierungs-Hinweise

### LED-Verkabelung

Die Uhrzeigersinn-Reihenfolge entspricht typischer LED-Strip-Verkabelung:

```
LED-Strip physisch (32 LEDs in Serie):

Data In ─► [0][1][2][3][4][5][6][7][8][9]
           ┌─────────────────────────────┘
          [10]
          [11]
          [12]
          [13]
          [14]
          [15]
           └─────────────────────────────┐
           [25][24][23][22][21][20][19][18][17][16]
         ┌─────────────────────────────┘
        [26]
        [27]
        [28]
        [29]
        [30]
        [31]

Total: 32 LEDs, durchgehend verkabelt
```

### Debugging

Zum Testen der korrekten Reihenfolge:

```cpp
// Test-Pattern: Index als Farbe
void sendTestPattern() {
    uint8_t packet[100];
    packet[0] = 0;    // packet_num
    packet[1] = 1;    // total_packets
    packet[2] = 10;   // h_segments
    packet[3] = 8;    // v_segments
    
    for (int i = 0; i < 32; i++) {
        packet[4 + i*3 + 0] = i * 8;     // R = Index * 8
        packet[4 + i*3 + 1] = 0;         // G = 0
        packet[4 + i*3 + 2] = 255 - i*8; // B = invertiert
    }
    
    esp_now_send(receiverMAC, packet, 100);
}
// Ergebnis: Farbverlauf Rot(0)→Blau(31) läuft im Uhrzeigersinn um Bildschirm
```

## Erweiterungen (Zukünftig)

### Version 2.0 - Optionen

```
Byte 0: packet_num
Byte 1: total_packets
Byte 2: protocol_version (0x02)
Byte 3: flags (compression, encoding, ...)
Byte 4: h_segments (nur Paket 0)
Byte 5: v_segments (nur Paket 0)
Bytes 6+: RGB data oder komprimierte Daten
```

### Kompression (Optional)

Bei vielen ähnlichen Farben könnte **Run-Length Encoding** verwendet werden:
```
[count][R][G][B] = count aufeinanderfolgende Rechtecke mit gleicher Farbe
```

**Vorteil**: Weniger Daten bei homogenen Farben (z.B. schwarzer Bildschirmrand)  
**Nachteil**: Komplexere Implementierung, variabler Overhead  
**Empfehlung**: Erst bei >100 Rechtecken sinnvoll

## Zusammenfassung

### Protokoll-Eigenschaften

- ✅ **Minimaler Overhead**: Nur 4 Bytes für Paket 0, 2 Bytes für Folgepakete
- ✅ **Multi-Paket-Unterstützung** für große Rechteck-Arrays (>82 Rechtecke)
- ✅ **Effizient**: Segment-Info nur einmal übertragen (Paket 0)
- ✅ **Uhrzeigersinn-Reihenfolge** für intuitive LED-Verkabelung
- ✅ **Atomare Gültigkeit**: Daten nur bei vollständigem Frame gültig

### Design-Prinzipien

- 🚫 **Keine Retries**: Sender sendet einmal, bei Verlust warten auf nächsten Frame (100ms)
- ⚠️ **Strikte Reihenfolge**: Falsche Sequenz → sofortiges Verwerfen aller Pakete
- ✅ **Fail-Fast**: Bei Fehler schnell verwerfen statt auf Timeout warten
- ✅ **Einfach**: Empfänger-State-Machine hat nur 2 Zustände (warte auf Paket 0 / empfange Frame)
- 📊 **Best-Effort**: 10 FPS bedeutet: 1 verlorener Frame = 100ms Verzögerung, akzeptabel für LED-Anzeige

### Warum keine Retries?

Bei 10 FPS (100ms pro Frame):
- **Retry-Zeit**: ~10-20ms für Retry-Logik
- **Nächster Frame**: Kommt sowieso in 100ms
- **Overhead**: Retry-Mechanismus kompliziert Code erheblich
- **Erfolgsrate**: ESP-NOW hat bereits ~99% Erfolgsrate bei 1 Paket

**Fazit**: Einfacher auf nächsten Frame warten als komplexe Retry-Logik zu implementieren

### Typischer Anwendungsfall (10h x 8v)

```
32 Rechtecke = 100 Bytes → 1 Paket → 1-3ms Latenz
10 FPS (alle 100ms) → 1 kB/s Bandbreite
Erfolgsrate: ~99% (bei Verlust: nächster Frame in 100ms)
```

## Siehe auch

- `BENUTZERHANDBUCH.md` - Allgemeine Bedienung
- `windows.cpp` - Ambilight-Berechnung auf ESP32-CAM
- ESP-NOW Dokumentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
