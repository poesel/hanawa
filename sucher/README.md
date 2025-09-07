# Hanawa Sucher

Der Sucher ist die Farbanalyse-Komponente des Hanawa Ambient Lighting Systems. Er verwendet eine ESP32CAM, um die Farben eines Fernsehers zu analysieren und die Daten an den Leuchter zu senden.

## Hardware-Anforderungen

- **ESP32CAM Modul** mit OV2640 Kamera
- **USB-zu-TTL Konverter** für Programmierung
- **WLAN-Verbindung** für Kommunikation

## Installation

### 1. Arduino IDE vorbereiten

1. Arduino IDE installieren (Version 1.8.x oder neuer)
2. ESP32 Board Support Package hinzufügen:
   - File → Preferences → Additional Board Manager URLs
   - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Board Manager: "ESP32" installieren
4. Board auswählen: "AI Thinker ESP32-CAM"

### 2. Benötigte Bibliotheken

Installiere folgende Bibliothek über den Library Manager:

- **ArduinoJson** (von Benoit Blanchon)

**Hinweis**: Die ESP32 Camera Bibliothek ist bereits im ESP32 Board Package enthalten und muss nicht separat installiert werden.

### 3. Konfiguration

1. Öffne `config.h` und passe die WLAN-Einstellungen an:
   ```cpp
   #define WIFI_SSID "DEIN_WLAN_NAME"
   #define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"
   ```

2. Stelle sicher, dass die IP-Adressen nicht mit deinem Router kollidieren:
   ```cpp
   #define SUCHER_IP "192.168.1.100"
   #define LEUCHTER_IP "192.168.1.101"
   ```

### 4. Programmierung

1. Verbinde ESP32CAM mit USB-zu-TTL Konverter:
   - **5V** → 5V
   - **GND** → GND
   - **U0R** → TX
   - **U0T** → RX
   - **GPIO0** → GND (für Download-Modus)

2. Halte GPIO0 auf GND und drücke Reset
3. Lade das Programm hoch
4. Entferne GPIO0-Verbindung und drücke Reset

## Verwendung

### 1. Erste Inbetriebnahme

1. Schließe ESP32CAM an Stromversorgung an
2. Öffne Serial Monitor (115200 baud)
3. Warte auf WLAN-Verbindung
4. Öffne Browser: `http://192.168.1.100`

### 2. Kalibrierung

1. **Kamera positionieren**: Richte die Kamera auf den Fernseher (2-3m Abstand)
2. **Ecken definieren**: Klicke auf die 4 Ecken des Fernsehers im Web-Interface:
   - Punkt 1: Oben links
   - Punkt 2: Oben rechts
   - Punkt 3: Unten rechts
   - Punkt 4: Unten links
3. **Parameter einstellen**: Setze horizontale und vertikale Teilung
4. **Aktivierung**: Nach der Kalibrierung startet die Farbanalyse automatisch

### 3. Web-Interface

Das Web-Interface bietet folgende Funktionen:

- **Livebild**: Zeigt das Kamerabild in Echtzeit
- **Kalibrierung**: Interaktive Fernseher-Ecken-Definition
- **Parameter**: Einstellung der Teilungen
- **Status**: Aktueller Betriebszustand

## Technische Details

### Farbanalyse

- **Auflösung**: 640x480 Pixel (VGA)
- **Format**: RGB565 für bessere Performance
- **Segmentierung**: Automatische Aufteilung entlang der Fernseher-Kanten
- **Farbberechnung**: Durchschnittliche RGB-Werte pro Segment
- **Helligkeit**: Luminance-Berechnung (299R + 587G + 114B) / 1000

### Kommunikation

- **Protokoll**: UDP
- **Format**: JSON mit Farb- und Helligkeitsdaten
- **Frequenz**: 10 FPS (konfigurierbar)
- **Datenstruktur**:
  ```json
  {
    "segments": 64,
    "colors": [
      {"r": 255, "g": 128, "b": 64, "brightness": 180},
      ...
    ]
  }
  ```

### Performance-Optimierung

- **Framerate**: Reduziere bei Performance-Problemen
- **Auflösung**: Kann auf QVGA (320x240) reduziert werden
- **JPEG-Qualität**: Anpassbar in `config.h`
- **Segment-Anzahl**: Weniger Segmente = bessere Performance

## Troubleshooting

### WLAN-Verbindung funktioniert nicht

1. Prüfe WLAN-Einstellungen in `config.h`
2. Stelle sicher, dass die IP-Adressen frei sind
3. Teste mit `ping 192.168.1.100`

### Kamera zeigt kein Bild

1. Prüfe Kameramodul-Verbindung
2. Stelle sicher, dass genügend Licht vorhanden ist
3. Teste mit einfachem Kameratest-Sketch

### Farben stimmen nicht überein

1. Kalibriere Fernseher-Ecken neu
2. Passe Teilungsparameter an
3. Prüfe Kameraposition und Beleuchtung

### Performance-Probleme

1. Reduziere `ANALYSIS_FPS` in `config.h`
2. Verwende kleinere `CAMERA_FRAME_SIZE`
3. Reduziere Anzahl der Segmente

## Konfigurationsoptionen

### `config.h` Parameter

| Parameter | Beschreibung | Standard |
|-----------|-------------|----------|
| `WIFI_SSID` | WLAN-Name | - |
| `WIFI_PASSWORD` | WLAN-Passwort | - |
| `CAMERA_FRAME_SIZE` | Kamerauflösung | FRAMESIZE_VGA |
| `ANALYSIS_FPS` | Analyse-Framerate | 10 |
| `DEFAULT_HORIZONTAL_DIVISIONS` | Standard horizontale Teilung | 20 |
| `DEFAULT_VERTICAL_DIVISIONS` | Standard vertikale Teilung | 12 |

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz. Siehe LICENSE-Datei für Details.
