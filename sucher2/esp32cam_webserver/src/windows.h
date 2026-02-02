#ifndef WINDOWS_H
#define WINDOWS_H

#include <Arduino.h>
#include "esp_camera.h"

// Struktur für Rechteck-Koordinaten
struct WindowRect {
    int x1, y1, x2, y2;
};

// Struktur für RGB-Farbwerte
struct RGB {
    uint8_t r, g, b;
};

// Hauptfunktion: verarbeitet Ambilight-Request und gibt JSON-Response zurück
String processAmbilight(const String& jsonInput);

// Hilfsfunktionen (intern verwendet)
// calculateMeanRGB  - RMS-Mittelwert (Original aus ambivios.py)
// calculateMeanRGB2 - Gamma-korrigierter Mittelwert (visuell korrekt, basierend auf linear.txt)
RGB calculateMeanRGB(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2);
RGB calculateMeanRGB2(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2);

#endif // WINDOWS_H

