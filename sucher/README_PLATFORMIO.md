# Hanawa Sucher - PlatformIO Version

Der Sucher ist die Farbanalyse-Komponente des Hanawa Ambient Lighting Systems, jetzt als PlatformIO-Projekt.

## Vorteile von PlatformIO

- ✅ **Automatische Bibliotheksverwaltung**
- ✅ **Professionelle Projektstruktur**
- ✅ **Bessere Debugging-Funktionen**
- ✅ **CI/CD Integration**
- ✅ **Multi-Board Support**

## Projektstruktur

```
sucher/
├── platformio.ini          # PlatformIO-Konfiguration
├── src/
│   ├── main.cpp            # Hauptprogramm
│   ├── config.h            # Konfiguration
│   └── webpage.h           # HTML-Interface
├── lib/                    # Lokale Bibliotheken
├── include/                # Header-Dateien
└── README_PLATFORMIO.md    # Diese Datei
```

## Installation

### 1. PlatformIO installieren

**Option A: VS Code Extension (empfohlen)**
1. VS Code installieren
2. PlatformIO IDE Extension installieren
3. Projekt öffnen

**Option B: PlatformIO CLI**
```bash
pip install platformio
```

### 2. Projekt öffnen

```bash
cd sucher
pio project init --board esp32cam
```

### 3. Konfiguration anpassen

Öffne `src/config.h` und passe die WLAN-Einstellungen an:
```cpp
#define WIFI_SSID "DEIN_WLAN_NAME"
#define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"
#define SUCHER_IP "192.168.0.80"
#define LEUCHTER_IP "192.168.0.81"
```

## Verwendung

### Kompilieren
```bash
pio run
```

### Upload
```bash
pio run --target upload
```

### Serial Monitor
```bash
pio device monitor
```

### Alles zusammen
```bash
pio run --target upload --target monitor
```

## PlatformIO-Konfiguration

### `platformio.ini` Einstellungen

- **Board**: ESP32CAM
- **Framework**: Arduino
- **Bibliotheken**: ArduinoJson (automatisch installiert)
- **Upload Speed**: 115200 baud
- **Monitor Speed**: 115200 baud
- **Debug**: ESP32 Exception Decoder aktiviert

### Automatische Bibliotheksverwaltung

PlatformIO installiert automatisch:
- ArduinoJson (über lib_deps)
- ESP32 Camera (im Framework enthalten)
- WiFi, WebServer, WiFiUdp (im Framework enthalten)

## Hardware-Verbindung

### ESP32CAM mit USB-zu-TTL Konverter:
```
USB-zu-TTL    →   ESP32CAM
5V            →   5V
GND           →   GND
TX            →   U0R (GPIO3)
RX            →   U0T (GPIO1)
```

### Download-Modus:
- **GPIO0** → **GND** (während Upload)
- **Reset** drücken

## Debugging

### Serial Monitor
```bash
pio device monitor
```

### Exception Decoder
Automatisch aktiviert in `platformio.ini`

### Hardware Debugging (optional)
```ini
debug_tool = esp-prog
debug_init_break = tbreak setup
```

## Troubleshooting

### Upload funktioniert nicht
1. Prüfe Hardware-Verbindung
2. GPIO0 auf GND während Upload
3. Reset nach Upload

### Bibliotheken nicht gefunden
```bash
pio lib install "ArduinoJson"
```

### Build-Fehler
```bash
pio run --verbose
```

## VS Code Integration

### PlatformIO IDE Extension
- Automatische IntelliSense
- Build-Tasks
- Upload-Tasks
- Serial Monitor
- Debugging

### Tastenkombinationen
- `Ctrl+Alt+U`: Upload
- `Ctrl+Alt+S`: Serial Monitor
- `Ctrl+Alt+B`: Build

## CI/CD Integration

PlatformIO unterstützt automatische Builds:
- GitHub Actions
- GitLab CI
- Travis CI

## Lizenz

Dieses Projekt steht unter der MIT-Lizenz.
