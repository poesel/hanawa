# Leuchter - ESP8266 D1 Mini Ambilight LED-Steuerung

## Übersicht

Der **Leuchter** ist der zweite Teil des Ambilight-Systems. Er empfängt die vom **Sucher** (ESP32-CAM) berechneten Farbdaten via ESP-NOW und steuert damit einen APA102 LED-Strip. Zusätzlich bietet er ein Web-Interface zur Helligkeitssteuerung.

**Hardware:** ESP8266 D1 Mini (statt ESP32)

## Hardware

### Komponenten

- **ESP8266 D1 Mini**
- **APA102 LED-Strip** (empfohlen: max. 50 LEDs wegen begrenztem RAM)
- **Stromversorgung** (5V, ausreichend für LED-Anzahl)

### Verkabelung

```
D1 Mini        APA102 LED-Strip
--------       ----------------
D7 (GPIO13) -> DATA (DI)
D5 (GPIO14) -> CLOCK (CI)
GND         -> GND
5V          -> 5V (über externe Stromversorgung!)
```

**D1 Mini Pinout:**
- **D5** = GPIO14 = SCK (Clock)
- **D7** = GPIO13 = MOSI (Data)

**WICHTIG:** 
- Verwenden Sie eine externe Stromversorgung für die LEDs (nicht über D1 Mini!)
- Verbinden Sie alle GNDs (D1 Mini + LEDs + Netzteil)
- APA102 benötigt 5V Logik-Pegel (ESP8266 liefert 3.3V, funktioniert aber meist)
- **ESP8266 Einschränkungen:**
  - Weniger RAM (~80KB) → Max. 50-60 LEDs empfohlen
  - ESP-NOW + WiFi kann instabiler sein als auf ESP32

### LED-Anordnung

Die LEDs werden **im Uhrzeigersinn** hinter dem Fernseher angebracht, beginnend **oben links**:

```
    ┌─────────────────────┐
    │  →  →  →  →  →  →  ↓ 
    ↑                     ↓  Fernseher
    ↑  ←  ←  ←  ←  ←  ←  ↓
    └─────────────────────┘
```

Die Datenrichtung des LED-Strips muss dieser Anordnung folgen.

## Software-Installation

### 1. Voraussetzungen

- [PlatformIO](https://platformio.org/) installiert
- ESP8266 D1 Mini via USB verbunden

### 2. Konfiguration

Bearbeiten Sie `src/main.cpp` und passen Sie folgende Werte an:

```cpp
// WiFi-Zugangsdaten
const char* WIFI_SSID = "IhrWLANName";
const char* WIFI_PASSWORD = "IhrWLANPasswort";

// Feste IP (optional, anpassen!)
IPAddress local_IP(192, 168, 0, 121);

// Anzahl LEDs (muss mit Sucher übereinstimmen!)
#define LED_COUNT 32
```

### 3. Kompilieren und Hochladen

```bash
cd leuchter
pio run --target upload
```

### 4. Serielle Konsole öffnen

```bash
pio device monitor
```

Sie sollten folgende Ausgabe sehen:

```
=================================
ESP8266 Leuchter (D1 Mini) - v1.0
=================================
[LEDs] Initialisiert
[WiFi] Verbunden!
[WiFi] IP: 192.168.0.121
[WiFi] MAC: XX:XX:XX:XX:XX:XX
[ESP-NOW] Initialisiert
[ESP-NOW] Callback registriert
[Protocol] Initialisiert
[Server] Gestartet auf Port 80
=================================
Bereit!
=================================
```

## Verwendung

### Web-Interface

1. Öffnen Sie im Browser: `http://192.168.0.121` (oder die konfigurierte IP)
2. Sie sehen:
   - **Status:** Verbindungsstatus zum Sucher
   - **LEDs:** Anzahl konfigurierter LEDs
   - **Frames:** Anzahl empfangener Frames
   - **FPS:** Empfangsrate
   - **Helligkeit:** Slider (0-100%)
   - **LED-Vorschau:** Aktuelle Farben aller LEDs

### Helligkeit anpassen

- Bewegen Sie den Slider im Web-Interface
- Die Helligkeit wird sofort übernommen
- Standard: 50%

## Integration mit Sucher

### ESP-NOW Verbindung

Der Leuchter empfängt automatisch Daten vom Sucher via ESP-NOW. Keine Konfiguration der MAC-Adresse erforderlich (empfängt von allen Sendern).

### Protokoll

Das Ambilight-Protokoll ist in `../esp32cam_webserver/doc/AMBILIGHT_PROTOCOL.md` dokumentiert.

**Kurz:**
- Sender (Sucher) sendet alle 100ms Farbdaten
- Daten können auf mehrere Pakete aufgeteilt sein (ESP-NOW Limit: 250 Bytes)
- Empfänger validiert Pakete strikt (Reihenfolge, Timeout 200ms)

### LED-Mapping

Die RGB-Daten vom Sucher werden **direkt** auf die LED-Indizes gemappt:

- Index 0: Oben links
- Index 1-N: Im Uhrzeigersinn um den Bildschirm

Die Anzahl der Rechtecke (= LEDs) ergibt sich aus der Sucher-Konfiguration:

```
Anzahl = 2 * h_segments + 2 * (v_segments - 2)

Beispiel: 10 horizontal, 8 vertikal
Anzahl = 2*10 + 2*(8-2) = 20 + 12 = 32 LEDs
```

## Fehlerbehebung

### LEDs leuchten nicht

1. **Stromversorgung prüfen:** LEDs benötigen externe 5V
2. **Verkabelung prüfen:** D7/GPIO13 (DATA) und D5/GPIO14 (CLOCK)
3. **LED-Typ prüfen:** Programm ist für APA102, nicht WS2812!
4. **Serielle Konsole prüfen:** Zeigt `[LEDs] Initialisiert`?

### Keine Verbindung zum Sucher

1. **WiFi-Verbindung prüfen:** Beide ESPs im gleichen WLAN?
2. **Serielle Konsole (Leuchter):** Zeigt `[ESP-NOW] Initialisiert`?
3. **Serielle Konsole (Sucher):** Sendet der Sucher Daten?
4. **Web-Interface:** Status sollte "Verbunden" zeigen, wenn Daten empfangen werden

### Falsche Farben

1. **LED-Anzahl prüfen:** `LED_COUNT` im Leuchter == Rechteck-Anzahl im Sucher?
2. **LED-Anordnung prüfen:** Uhrzeigersinn, Start oben links?

### Web-Interface nicht erreichbar

1. **IP-Adresse prüfen:** Serielle Konsole zeigt die richtige IP
2. **Browser-URL:** `http://192.168.0.121` (kein `https://`!)
3. **Firewall:** Eventuell Port 80 blockiert?

## Test-Modi

### Test 1: LEDs einzeln durchschalten

Fügen Sie in `setup()` nach `leds.begin()` ein:

```cpp
for (int i = 0; i < LED_COUNT; i++) {
    leds.clear();
    leds.setPixel(i, 255, 0, 0);  // Rot
    leds.show();
    delay(200);
}
leds.clear();
leds.show();
```

Alle LEDs leuchten nacheinander rot auf → Verkabelung OK

### Test 2: Regenbogen-Muster

```cpp
for (int i = 0; i < LED_COUNT; i++) {
    int hue = (i * 360) / LED_COUNT;
    // RGB aus HSV berechnen (vereinfacht)
    uint8_t r = (hue < 120) ? 255 : 0;
    uint8_t g = (hue >= 120 && hue < 240) ? 255 : 0;
    uint8_t b = (hue >= 240) ? 255 : 0;
    leds.setPixel(i, r, g, b);
}
leds.show();
delay(5000);
```

Regenbogen-Farben → LED-Treiber OK

## API-Referenz

### GET `/`
- Liefert HTML-Interface

### POST `/api/brightness`
- Body: `{"brightness": 0-100}`
- Response: `{"success": true, "brightness": 50}`

### GET `/api/status`
- Response:
```json
{
  "num_leds": 32,
  "frames": 1234,
  "fps": 9.8,
  "receiving": true,
  "brightness": 50,
  "h_segments": 10,
  "v_segments": 8,
  "colors": [
    {"r": 255, "g": 0, "b": 0},
    ...
  ]
}
```

## Technische Details

### APA102 Protokoll

- **Start Frame:** 4 Bytes (0x00)
- **LED Frames:** 4 Bytes pro LED:
  - Byte 0: `0b111BBBBB` (3 Einsen + 5 Bit Brightness)
  - Byte 1: Blue
  - Byte 2: Green
  - Byte 3: Red
- **End Frame:** `(num_leds + 15) / 16` Bytes (0xFF)

### SPI-Konfiguration

- Frequenz: 10 MHz
- Mode: SPI_MODE0
- Pins: MOSI=D7/GPIO13, SCK=D5/GPIO14

### ESP-NOW

- Kanal: Automatisch (WiFi-Kanal des Netzwerks)
- Reichweite: ~50m (Sichtlinie)
- Latenz: ~1-3ms pro Frame

## Lizenz

MIT License - siehe `../LICENSE`

## Support

Bei Problemen:
1. Serielle Konsole prüfen (beide ESPs)
2. Web-Interface Status prüfen
3. LED-Verkabelung prüfen
4. GitHub Issues öffnen
