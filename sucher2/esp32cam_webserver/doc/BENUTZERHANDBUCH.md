# ESP32-CAM Webserver – Benutzerhandbuch

## 1. Einleitung
Dieses Handbuch beschreibt die Installation, Inbetriebnahme und Bedienung der Firmware *esp32cam_webserver*. Die Software verwandelt ein AI-Thinker **ESP32-CAM**-Modul in eine WLAN-fähige Kamera, die einen MJPEG-Stream sowie eine einfache JSON-API zur Verfügung stellt.

## 2. Voraussetzungen

| Komponente              | Empfehlung                                     |
|-------------------------|-------------------------------------------------|
| Hardware                | AI-Thinker ESP32-CAM (andere Boards ungetestet) |
| USB-Seriell-Adapter     | 5 V-TTL, z. B. CH340 oder FTDI                  |
| Software                | VS Code + PlatformIO Extension **oder** `pio` CLI |
| Micro-USB-Kabel         | Datenfähig                                      |

## 3. Projekt-Aufbau

```
esp32cam_webserver/
├── doc/                  ← Dokumentation
├── src/                  ← Quellcode
│   ├── main.cpp          ← Einstiegspunkt der Firmware
│   ├── config.h          ← WLAN-Konfiguration anpassen!
│   └── index_html.h      ← Eingebettete Weboberfläche
└── platformio.ini        ← Build- und Flash-Einstellungen
```

## 4. WLAN-Konfiguration
Bearbeite vor dem Flashen die Datei `src/config.h` und trage dein WLAN-Netz ein:

```cpp
#define WIFI_SSID      "MeinWLAN"
#define WIFI_PASSWORD  "SuperGeheim"
```

> **Sicherheitshinweis:** Bewahre dein Repository privat auf oder nutze Platzhalter, wenn du die Zugangsdaten veröffentlichst.

## 5. Kompilieren & Flashen
1. **VS Code / PlatformIO**
   - Öffne das Projektverzeichnis mit VS Code.
   - In der Statusleiste *“PlatformIO: ESP32 Dev Module”* prüfen.
   - Klicke auf **Build** (Häkchen) → **Upload** (Pfeil).
2. **CLI**
   ```bash
   # im Projektverzeichnis
   pio run --target upload
   ```
   Wähle bei der ersten Ausführung den richtigen Port (z. B. `/dev/ttyUSB0`).

Während des Flash-Vorgangs den **BOOT-Taster** gedrückt halten (abhängig vom Adapter).

## 6. Inbetriebnahme
1. Versorge das Board mit 5 V.
2. Die serielle Konsole zeigt den Verbindungsstatus an (`115200 baud`).
3. Nach erfolgreicher WLAN-Verbindung erscheint die IP-Adresse, z. B. `192.168.1.120`.

### Serielle Konsole

1. PlatformIO in VS Code +
Rechts unten auf das Steckersymbol („Serial Monitor“) klicken.
Alternativ über die Command Palette: PlatformIO: Serial Monitor.
Stimmt die Baudrate nicht, erscheint oben eine gelbe Leiste – dort „115200“ auswählen.

2. PlatformIO-CLI im Terminal
   pio device monitor -b 115200
Falls mehrere Ports verfügbar sind, kannst du einen explizit angeben:
   pio device monitor -b 115200 -p /dev/ttyUSB0

3. Arduino-IDE
Werkzeuge → Serieller Monitor (oder Ctrl+Shift+M).
Unten rechts die Baudrate auf 115 200 stellen.

4. Reines Terminal (Linux/macOS)
   screen /dev/ttyUSB0 115200

## 7. Weboberfläche
Rufe im Browser `http://<IP-Adresse>/` auf.

| Pfad               | Methode | Beschreibung                          |
|--------------------|---------|----------------------------------------|
| `/`                | GET     | Eingebettete HTML-Seite mit Videostream |
| `/stream`          | GET     | MJPEG-Stream (multipart/x-mixed)        |
| `/api/grid`        | POST    | JSON-API zur Rasterberechnung          |
| `/api/ambilight`   | POST    | JSON-API für Ambilight-Farbberechnung  |

### 7.1 MJPEG-Stream
Der Stream kann z. B. in **VLC** eingebunden werden:
```
Medien → Netzwerkstream öffnen → URL: http://<IP>/stream
```

### 7.2 JSON-API `/api/grid`
Request-Body (Beispiel):
```json
{
  "points": [
    { "x": 10, "y": 20 },
    { "x": 630, "y": 25 },
    { "x": 620, "y": 470 },
    { "x": 15, "y": 465 }
  ],
  "hSeg": 4,
  "vSeg": 3
}
```
Antwort: Liste interpolierter Punkte zwischen den Ecken.

### 7.3 JSON-API `/api/ambilight` (NEU)
Diese API berechnet RGB-Durchschnittswerte für Rechtecke entlang der vier Ränder eines trapezförmigen Bereichs – ideal für Ambilight-Projekte.

**Verwendung im Web-UI:**
1. Setze 4 Eckpunkte durch Klick auf den Video-Stream (z. B. Fernsehbildschirm)
2. Konfiguriere Segmente und Fenstertiefe
3. Klicke auf "Ambilight berechnen"
4. Die Rechtecke werden mit den berechneten Farben visualisiert

**Request-Body (Beispiel):**
```json
{
  "points": [
    { "x": 77, "y": 29 },
    { "x": 248, "y": 79 },
    { "x": 255, "y": 185 },
    { "x": 76, "y": 200 }
  ],
  "hSeg": 16,
  "vSeg": 10,
  "depth": 30
}
```

**Parameter:**
- `points`: Array mit 4 Eckpunkten (top-left, top-right, bottom-right, bottom-left)
- `hSeg`: Anzahl horizontaler Segmente (Standard: 16)
- `vSeg`: Anzahl vertikaler Segmente (Standard: 10)
- `depth`: Tiefe der Fenster in Pixel (Standard: 30)

**Response:**
```json
{
  "top": [[r,g,b], [r,g,b], ...],
  "bottom": [[r,g,b], ...],
  "left": [[r,g,b], ...],
  "right": [[r,g,b], ...],
  "topRects": [{"x1":..., "y1":..., "x2":..., "y2":...}, ...],
  ...
}
```

Die Antwort enthält RGB-Werte (0-255) für jedes Fenster sowie die Rechteck-Koordinaten zur Visualisierung.

## 8. Fehlersuche
| Problem | Lösung |
|---------|--------|
| Keine Ausgabe in der Konsole | Baudrate 115200 prüfen, Board-Pinout kontrollieren |
| WLAN verbindet nicht | SSID/Passwort korrekt? 2,4 GHz? Abstand zum Router |
| Kamerabild schwarz | Richtige Kamera-FPC eingesteckt? Lichtverhältnisse |

## 9. Lizenz
Dieses Projekt steht unter der MIT-Lizenz. Siehe `LICENSE` für Details.

---
© 2025 Markus | Beiträge willkommen!
