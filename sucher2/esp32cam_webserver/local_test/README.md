# Lokale Test-Version - ESP32-CAM Ambilight

Diese lokale Version ermöglicht es, die Ambilight-Funktionalität ohne ESP32-Hardware zu testen und weiterzuentwickeln.

## Verwendung

### 1. Testbild vorbereiten

Du benötigst ein Testbild mit dem Namen `testimage.jpg` (640x480 Pixel) im gleichen Verzeichnis wie die HTML-Datei.

**Option A: Eigenes Bild verwenden**
- Screenshot eines Fernsehbildschirms
- Beliebiges Bild auf 640x480 skalieren
- Als `testimage.jpg` speichern

**Option B: Testbild generieren**
```bash
python3 generate_testimage.py
```

### 2. Lokale Version starten

**Option A: Einfacher Python-Server**
```bash
cd local_test
python3 -m http.server 8000
```
Dann öffne: `http://localhost:8000/ambilight_test.html`

**Option B: Direkt im Browser öffnen**
- Doppelklick auf `ambilight_test.html`
- Funktioniert in den meisten Browsern (Firefox, Chrome, Safari)

### 3. Ambilight testen

1. **4 Eckpunkte setzen**: Klicke auf das Bild um die Ecken des zu analysierenden Bereichs zu markieren (z.B. Fernsehbildschirm)
2. **Parameter anpassen**: 
   - Horizontale Segmente: Anzahl der Fenster oben/unten
   - Vertikale Segmente: Anzahl der Fenster links/rechts
3. **Ambilight berechnen**: Klicke auf den Button - die Farbberechnung erfolgt lokal im Browser
4. **Ergebnis prüfen**: Die Rechtecke werden mit den berechneten Durchschnittsfarben gefüllt

## Technische Details

### Lokale Farbberechnung
Die Farbberechnung erfolgt vollständig in JavaScript und verwendet:
- Canvas ImageData API für Pixel-Zugriff
- RMS (Root Mean Square) für Farbmittelwerte
- Identische Geometrie-Berechnung wie auf dem ESP32

### Unterschiede zur ESP32-Version
- **Stream**: Statisches Bild statt Live-Stream
- **API**: Lokale JavaScript-Funktion statt `fetch('/api/ambilight')`
- **Berechnung**: Im Browser statt auf dem ESP32

### Übertragbare Komponenten
Folgende Teile können direkt auf den ESP32 übertragen werden:
- HTML-Struktur und CSS
- UI-Event-Handler
- Canvas-Visualisierung
- Rechteck-Geometrie-Algorithmus

## Rückportierung auf ESP32

Nach erfolgreichem Test der lokalen Version:

1. **HTML/CSS/JavaScript extrahieren**
   - Relevante Teile aus `ambilight_test.html` kopieren
   - In `../src/index_html.h` einfügen

2. **API-Calls wiederherstellen**
   - Lokale `processAmbilightLocal()` entfernen
   - `fetch('/api/ambilight')` wiederherstellen

3. **Backend testen**
   - C++-Backend in `../src/windows.cpp` bleibt unverändert
   - Firmware flashen und testen

## Browser-Kompatibilität

Getestet mit:
- ✅ Chrome/Chromium
- ✅ Firefox
- ✅ Safari
- ✅ Edge

## Troubleshooting

**"Testbild konnte nicht geladen werden"**
- Stelle sicher, dass `testimage.jpg` im gleichen Verzeichnis liegt
- Verwende einen lokalen Webserver (Python) statt direktem Datei-Zugriff

**CORS-Fehler im Browser**
- Verwende einen lokalen Webserver
- Oder öffne Chrome mit: `--allow-file-access-from-files`

**Farben sind falsch**
- Prüfe ob das Testbild korrekt geladen wurde
- Öffne die Browser-Konsole (F12) für Debug-Ausgaben

## Entwicklung

Die lokale Version eignet sich ideal für:
- UI/UX-Experimente
- Visualisierungs-Tests
- Debugging der Geometrie-Berechnung
- Performance-Optimierung

Alle Änderungen können später ohne Anpassungen auf den ESP32-CAM übertragen werden.
