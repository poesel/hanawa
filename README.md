# Hanawa - Lichtkranz für den Fernseher

Ambientes Licht rund um Deinen Fernseher, das sich automatisch an die Bildschirmfarben anpasst.

## Übersicht

Das Projekt besteht aus zwei Teilen:
- **Sucher**: ESP32CAM, der den Fernseher beobachtet und Farben analysiert
- **Leuchter**: ESP32 mit LED-Lichterkette, der die Farbdaten umsetzt

## Hardware-Anforderungen

### Sucher (ESP32CAM)
- ESP32CAM Modul
- USB-zu-TTL Konverter für Programmierung
- WLAN-Verbindung

### Leuchter (ESP32)
- ESP32 Development Board
- APA102 LED-Streifen (mindestens 60 LEDs empfohlen)
- 5V Stromversorgung für LEDs
- WLAN-Verbindung

## Installation und Setup

### 1. Arduino IDE vorbereiten
- Arduino IDE installieren (Version 1.8.x oder neuer)
- ESP32 Board Support Package hinzufügen:
  - File → Preferences → Additional Board Manager URLs
  - `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Board Manager: "ESP32" installieren

### 2. Benötigte Bibliotheken
```
- ESP32 Camera
- FastLED (für APA102 LEDs)
- WiFi
- WebServer
- ArduinoJson
```

### 3. WLAN-Konfiguration
Die IP-Adressen werden im Programm festgelegt:
- Sucher: `192.168.1.100`
- Leuchter: `192.168.1.101`

**Wichtig**: Stelle sicher, dass dein Router diese IPs nicht vergibt.

## Sucher - Konfiguration

### Hardware-Setup
1. ESP32CAM mit USB-zu-TTL Konverter verbinden
2. Kamera auf Fernseher ausrichten (idealerweise 2-3m Abstand)
3. WLAN-Verbindung herstellen

### Kalibrierung
1. Web-Interface öffnen: `http://192.168.1.100`
2. Livebild der Kamera wird angezeigt
3. **Fernseher-Ecken definieren**:
   - Punkt 1: Oben links
   - Punkt 2: Oben rechts  
   - Punkt 3: Unten rechts
   - Punkt 4: Unten links

### Parameter-Einstellung
- **Horizontale Teilung**: Anzahl Segmente entlang der horizontalen Kanten (z.B. 20)
- **Vertikale Teilung**: Anzahl Segmente entlang der vertikalen Kanten (z.B. 12)


**Beispiel**: Bei einer horizontalen Kante von 120 Pixeln und einer Teilung von 5 ergibt sich eine Breite von 24 Pixeln pro kleinem Viereck.

### Funktionsweise
Die horizontalen Kanten (12 und 34) werden durch den horizontalen Teiler in gleichlange Strecken geteilt. Vertikale Kanten entsprechend.

**Berechnung**: Kante in Pixeln ÷ Teilung = Breite/Höhe eines kleinen Vierecks

Im großen Viereck werden kleine Vierecke gezeichnet, die mindestens eine Seite an den Kanten haben. Für jedes kleine Viereck wird die mittlere Farbe und Helligkeit ermittelt und an den Leuchter gesendet.

## Leuchter - Konfiguration

### Hardware-Setup
1. ESP32 mit Stromversorgung verbinden
2. APA102 LED-Streifen an ESP32 anschließen:
   - DATA: GPIO 23
   - CLOCK: GPIO 18
   - VCC: 5V
   - GND: GND

### LED-Anordnung
Die LEDs werden hinter dem Fernseher im Uhrzeigersinn angebracht:
- Zählung beginnt oben links
- Horizontale LEDs: oben und unten
- Vertikale LEDs: links und rechts

Das Projekt besteht aus zwei Teilen: dem Sucher, der den Fernseher beobachtet und die Farben ausrechnet und dem Leuchter, der die Farbdaten empfängt und den Fernseher beleuchtet.

Beide Programme werden in der Arduino IDE programmiert und von dort auf die Microcontroller hochgeladen. Die Microcontroller erhalten eine feste IP im WLAN.
Die IP wird im Program festgelegt.

## Sucher

Besteht aus einer ESP32CAM, die mit der Arduino IDE programmiert wird. Er hängt im WLAN und hat ein Webinterface.

Darüber zeigt er das Livebild der Kamera an. Die Kamera wird auf den Fernseher gerichtet. Durch Mausklicks kann man die 4 Ecken des Fernsehers definieren. Daurch wird ein Viereck definiert. +
Der Punkt, der am weitesteten links und oben ist, ist Punkt 1. Punkt 2 ist dann oben rechts, punkt 3 unten rechts, Punkt 4 unten links.

Die Kante, die Punkt 1 und 2 verbindet, nennt man Kante 12. Entsprechend bei den anderen. +
Die Länge der Kanten wird in Pixeln gemessen.

Kante 12 und 34 sind die horizontalen Kanten. 23 und 41 die vertikalen.

Auf dem Web-Interface kann man als Parameter die horizontale und die vertikale Teilung eingeben.

Die horizontalen Kanten werden durch den horizontalen Teiler in Strecken gleicher Länge geteilt (auf glatte Pixel gerundet). Vertikale entsprechend. +
Nun werden in das innere des großen Vierecks kleine Vierecke gezeichnet. Diese kleinen Vierecke berühren mit mindestens einer Seite die Kanten des großen Vierecks. Es gibt nur eine Reihe von kleinen Vierecke. Das innere bleibt leer.

Die Höhe und Breite der inneren Vierecke ergeben sich aus der Teilung. Die Innenwinkel der inneren und äußeren Vierecke sind gleich.

Die Vierecke sollen im Livebild angezeigt werden.

Für jedes kleine Viereck wird die mittlere Farbe sowie die Helligkeit ermittelt. Diese Information wird an den Leuchter übermittelt. +
Sucher und Leuchter kommunizieren per WLAN miteinander.

## Leuchter

Der Leuchter besteht aus einem ESP32 der eine LED Lichterkette ansteuert. Die LEDs sind vom Typ APA102.

Die LEDS werden hinter dem Fernseher am Rand angebracht. Die Zählung der LEDs beginnt in der linken, oberen Ecke und verläuft im Uhrzeigersinn. +
Es wird definiert, wieviele LEDs horizontal und vertikal vorhanden sind.

Die Daten vom Sucher werden dann auf die entsprechenden LEDs gemappt. Die LEDs werden mit der Farbe & Helligkeit angesteuert, die vom Sucher übermittelt werden.

Der Leuchter hat ein Web-Interface mit dem die Helligkeit verändert werden kann.
