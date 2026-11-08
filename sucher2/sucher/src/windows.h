#ifndef WINDOWS_H
#define WINDOWS_H

#include <Arduino.h>
#include <vector>
#include "esp_camera.h"

// Struktur für Rechteck-Koordinaten
struct WindowRect {
    int x1, y1, x2, y2;
};

// Struktur für RGB-Farbwerte
struct RGB {
    uint8_t r, g, b;
};

// Struktur für Ambilight-Konfiguration (globaler State)
struct AmbilightConfig {
    float topLeft[2];
    float topRight[2];
    float botRight[2];
    float botLeft[2];
    int hSeg;
    int vSeg;
    bool isValid;
};

// Struktur für Ambilight-Ergebnis (globaler State)
struct AmbilightResult {
    std::vector<RGB> topColors;
    std::vector<RGB> bottomColors;
    std::vector<RGB> leftColors;
    std::vector<RGB> rightColors;
    std::vector<WindowRect> topRects;
    std::vector<WindowRect> bottomRects;
    std::vector<WindowRect> leftRects;
    std::vector<WindowRect> rightRects;
    unsigned long timestamp;
    bool isValid;
};

// Globaler State (extern deklariert, in windows.cpp definiert)
extern AmbilightConfig g_ambilightConfig;
extern AmbilightResult g_ambilightResult;

// Neue API-Funktionen für kontinuierliche Berechnung
void updateAmbilightConfig(const String& jsonInput);
void calculateAmbilightContinuous();
String getAmbilightResult();

// Alte Funktion (deprecated, wird durch neue Architektur ersetzt)
String processAmbilight(const String& jsonInput);

// Hilfsfunktionen (intern verwendet)
// calculateMeanRGB  - RMS-Mittelwert (Original aus ambivios.py)
// calculateMeanRGB2 - Gamma-korrigierter Mittelwert (visuell korrekt, basierend auf linear.txt)
RGB calculateMeanRGB(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2);
RGB calculateMeanRGB2(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2);

#endif // WINDOWS_H

