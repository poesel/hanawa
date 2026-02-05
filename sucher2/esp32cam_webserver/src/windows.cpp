#include "windows.h"
#include <ArduinoJson.h>
#include <vector>
#include <cmath>
#include "esp_camera.h"
#include "img_converters.h"

// ============================================================================
// GLOBALER STATE FÜR KONTINUIERLICHE AMBILIGHT-BERECHNUNG
// ============================================================================

// Konfiguration (wird von Browser gesetzt)
AmbilightConfig g_ambilightConfig = {
    {25.0, 25.0},    // topLeft (skaliert für 320x240)
    {295.0, 25.0},   // topRight
    {295.0, 215.0},  // botRight
    {25.0, 215.0},   // botLeft
    10,              // hSeg (default)
    8,               // vSeg (default)
    true             // isValid (default Punkte sind gültig)
};

// Ergebnis (wird kontinuierlich aktualisiert)
AmbilightResult g_ambilightResult = {
    std::vector<RGB>(),        // topColors
    std::vector<RGB>(),        // bottomColors
    std::vector<RGB>(),        // leftColors
    std::vector<RGB>(),        // rightColors
    std::vector<WindowRect>(), // topRects
    std::vector<WindowRect>(), // bottomRects
    std::vector<WindowRect>(), // leftRects
    std::vector<WindowRect>(), // rightRects
    0,                         // timestamp
    false                      // isValid
};

// ============================================================================

// Berechnet den quadratischen Mittelwert der RGB-Werte in einem Rechteck
// Entspricht der mean_bgr() Funktion aus ambivios.py (Zeilen 54-67)
//
// Diese Funktion arbeitet mit RGB565-Daten (konvertiert aus JPEG)
RGB calculateMeanRGB(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2) {
    if (!rgb_buf) {
        return {0, 0, 0};
    }

    // Sicherstellen, dass die Koordinaten im gültigen Bereich liegen
    x1 = constrain(x1, 0, width - 1);
    x2 = constrain(x2, 0, width - 1);
    y1 = constrain(y1, 0, height - 1);
    y2 = constrain(y2, 0, height - 1);

    if (x1 >= x2 || y1 >= y2) {
        return {0, 0, 0};
    }

    // Sampling mit step=2 wie im Original
    const int step = 2;
    uint32_t sumR = 0, sumG = 0, sumB = 0;
    int pixelCount = 0;

    // RGB565: 2 Bytes pro Pixel
    for (int y = y1; y < y2; y += step) {
        for (int x = x1; x < x2; x += step) {
            size_t idx = (y * width + x) * 2; // RGB565 = 2 Bytes pro Pixel
            
            // RGB565 Format: RRRRR GGGGGG BBBBB (16 Bit)
            uint16_t pixel = (rgb_buf[idx] << 8) | rgb_buf[idx + 1];
            
            // Extrahiere RGB-Komponenten und skaliere auf 8 Bit
            uint8_t r = ((pixel >> 11) & 0x1F) << 3; // 5 Bit -> 8 Bit
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;  // 6 Bit -> 8 Bit
            uint8_t b = (pixel & 0x1F) << 3;         // 5 Bit -> 8 Bit
            
            // Quadratischer Mittelwert wie in Python
            sumR += (uint32_t)r * r;
            sumG += (uint32_t)g * g;
            sumB += (uint32_t)b * b;
            pixelCount++;
        }
    }

    if (pixelCount == 0) {
        return {128, 128, 128}; // Grau als Fallback
    }

    // Quadratische Wurzel des Durchschnitts (RMS), mit step-Korrektur
    uint8_t r = (uint8_t)round(sqrt((float)(step * step * sumR) / pixelCount));
    uint8_t g = (uint8_t)round(sqrt((float)(step * step * sumG) / pixelCount));
    uint8_t b = (uint8_t)round(sqrt((float)(step * step * sumB) / pixelCount));

    return {r, g, b};
}

// Visuell korrekte Farbmittelwert-Berechnung mit Gamma-Korrektur (sRGB → linear → sRGB)
// Basiert auf linear.txt - respektiert die menschliche Helligkeitswahrnehmung
RGB calculateMeanRGB2(uint8_t *rgb_buf, int width, int height, int x1, int y1, int x2, int y2) {
    if (!rgb_buf) {
        return {0, 0, 0};
    }

    // Sicherstellen, dass die Koordinaten im gültigen Bereich liegen
    x1 = constrain(x1, 0, width - 1);
    x2 = constrain(x2, 0, width - 1);
    y1 = constrain(y1, 0, height - 1);
    y2 = constrain(y2, 0, height - 1);

    if (x1 >= x2 || y1 >= y2) {
        return {0, 0, 0};
    }

    // Hilfsfunktion: sRGB (0-255) → linear RGB (0-1)
    auto srgbToLinear = [](uint8_t value) -> float {
        float v = value / 255.0f;
        if (v <= 0.04045f) {
            return v / 12.92f;
        } else {
            return pow((v + 0.055f) / 1.055f, 2.4f);
        }
    };

    // Hilfsfunktion: linear RGB (0-1) → sRGB (0-255)
    auto linearToSrgb = [](float value) -> uint8_t {
        float v;
        if (value <= 0.0031308f) {
            v = value * 12.92f;
        } else {
            v = 1.055f * pow(value, 1.0f/2.4f) - 0.055f;
        }
        return (uint8_t)round(v * 255.0f);
    };

    // Sampling mit step=2
    const int step = 2;
    float sumLinearR = 0, sumLinearG = 0, sumLinearB = 0;
    int pixelCount = 0;

    // RGB565: 2 Bytes pro Pixel
    for (int y = y1; y < y2; y += step) {
        for (int x = x1; x < x2; x += step) {
            size_t idx = (y * width + x) * 2;
            
            // RGB565 Format: RRRRR GGGGGG BBBBB (16 Bit)
            uint16_t pixel = (rgb_buf[idx] << 8) | rgb_buf[idx + 1];
            
            // Extrahiere RGB-Komponenten und skaliere auf 8 Bit
            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5) & 0x3F) << 2;
            uint8_t b = (pixel & 0x1F) << 3;
            
            // Konvertiere zu linear und summiere
            sumLinearR += srgbToLinear(r);
            sumLinearG += srgbToLinear(g);
            sumLinearB += srgbToLinear(b);
            pixelCount++;
        }
    }

    if (pixelCount == 0) {
        return {128, 128, 128};
    }

    // Mittelwert im linearen Raum
    float avgLinearR = sumLinearR / pixelCount;
    float avgLinearG = sumLinearG / pixelCount;
    float avgLinearB = sumLinearB / pixelCount;

    // Zurück zu sRGB konvertieren
    return {
        linearToSrgb(avgLinearR),
        linearToSrgb(avgLinearG),
        linearToSrgb(avgLinearB)
    };
}

// Berechnet alle Ambilight-Fenster basierend auf den 4 Eckpunkten
// Entspricht der Logik aus ambivios.py (Zeilen 86-120)
void calculateAmbilightWindows(
    float topLeft[], float topRight[], float botLeft[], float botRight[],
    int xwindows, int ywindows,
    std::vector<WindowRect>& topRects, std::vector<WindowRect>& bottomRects,
    std::vector<WindowRect>& leftRects, std::vector<WindowRect>& rightRects)
{
    // Fensterbreiten und -höhen berechnen
    float xtopwinwidth = (topRight[0] - topLeft[0]) / xwindows;
    float xbotwinwidth = (botRight[0] - botLeft[0]) / xwindows;
    float yleftwinheight = (botLeft[1] - topLeft[1]) / ywindows;
    float yrightwinheight = (botRight[1] - topRight[1]) / ywindows;

    // Slopes für Trapez-Verzerrung
    float topslope = (topRight[1] - topLeft[1]) / xwindows;
    float bottomslope = (botRight[1] - botLeft[1]) / xwindows;
    float leftslope = (botLeft[0] - topLeft[0]) / ywindows;
    float rightslope = (botRight[0] - topRight[0]) / ywindows;

    // Horizontale Fenster (top und bottom)
    for (int i = 0; i < xwindows; i++) {
        // Top - Höhe interpoliert zwischen links und rechts
        int tx1 = topLeft[0] + round(i * xtopwinwidth);
        int ty1 = topLeft[1] + round(i * topslope);
        int tx2 = tx1 + round(xtopwinwidth);
        // Lineare Interpolation der Fensterhöhe zwischen yleftwinheight und yrightwinheight
        float ty2 = ty1 + (1.0 - (float(i) / (xwindows - 1))) * yleftwinheight + (float(i) / (xwindows - 1)) * yrightwinheight;
        topRects.push_back({tx1, ty1, tx2, (int)ty2});

        // Bottom - Höhe interpoliert zwischen links und rechts
        int bx1 = botLeft[0] + round(i * xbotwinwidth);
        float by1 = botLeft[1] + round(i * bottomslope) - (1.0 - (float(i) / (xwindows - 1))) * yleftwinheight - (float(i) / (xwindows - 1)) * yrightwinheight;
        int bx2 = bx1 + round(xbotwinwidth);
        int by2 = botLeft[1] + round(i * bottomslope);
        bottomRects.push_back({bx1, (int)by1, bx2, by2});
    }

    // Vertikale Fenster (left und right) - Ecken werden übersprungen (i startet bei 1 und endet bei ywindows-2)
    Serial.print("[calculateWindows] Vertikale Fenster: ywindows=");
    Serial.print(ywindows);
    Serial.print(", Loop von i=1 bis i<");
    Serial.println(ywindows - 1);
    
    for (int i = 1; i < (ywindows - 1); i++) {
        // Left - Breite interpoliert zwischen oben und unten
        int lx1 = topLeft[0] + round(i * leftslope);
        int ly1 = topLeft[1] + round(i * yleftwinheight);
        // Lineare Interpolation der Fensterbreite zwischen xtopwinwidth und xbotwinwidth
        float lx2 = lx1 + (1.0 - (float(i) / (ywindows - 1))) * xtopwinwidth + (float(i) / (ywindows - 1)) * xbotwinwidth;
        int ly2 = ly1 + round(yleftwinheight);
        leftRects.push_back({lx1, ly1, (int)lx2, ly2});

        // Right - Breite interpoliert zwischen oben und unten
        float rx1 = topRight[0] + round(i * rightslope) - (1.0 - (float(i) / (ywindows - 1))) * xtopwinwidth - (float(i) / (ywindows - 1)) * xbotwinwidth;
        int ry1 = topRight[1] + round(i * yrightwinheight);
        int rx2 = topRight[0] + round(i * rightslope);
        int ry2 = ry1 + round(yrightwinheight);
        
        if (i == 1) {
            Serial.print("[calculateWindows] Right[0]: rx1=");
            Serial.print((int)rx1);
            Serial.print(", ry1=");
            Serial.print(ry1);
            Serial.print(", rx2=");
            Serial.print(rx2);
            Serial.print(", ry2=");
            Serial.println(ry2);
        }
        
        rightRects.push_back({(int)rx1, ry1, rx2, ry2});
    }
    
    Serial.print("[calculateWindows] Ergebnis: Top=");
    Serial.print(topRects.size());
    Serial.print(", Bottom=");
    Serial.print(bottomRects.size());
    Serial.print(", Left=");
    Serial.print(leftRects.size());
    Serial.print(", Right=");
    Serial.println(rightRects.size());
}

// Hauptfunktion: verarbeitet JSON-Input und gibt JSON-Response zurück
String processAmbilight(const String& jsonInput) {
    Serial.println("[processAmbilight] === START ===");
    
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, jsonInput);
    
    if (err) {
        Serial.println("[processAmbilight] ERROR: JSON parse error");
        return "{\"error\":\"JSON parse error\"}";
    }
    Serial.println("[processAmbilight] JSON erfolgreich geparst");

    JsonArray pts = doc["points"].as<JsonArray>();
    if (pts.size() != 4) {
        Serial.print("[processAmbilight] ERROR: Falsche Punktanzahl: ");
        Serial.println(pts.size());
        return "{\"error\":\"Need 4 points\"}";
    }

    int hSeg = doc["hSeg"].as<int>();
    int vSeg = doc["vSeg"].as<int>();
    int xwindows = (hSeg > 0) ? hSeg : 10;  // Default: 10
    int ywindows = (vSeg > 0) ? vSeg : 8;   // Default: 8
    
    Serial.print("[processAmbilight] Parameter: hSeg=");
    Serial.print(xwindows);
    Serial.print(", vSeg=");
    Serial.println(ywindows);

    // Eckpunkte extrahieren
    float topLeft[2] = {pts[0]["x"].as<float>(), pts[0]["y"].as<float>()};
    float topRight[2] = {pts[1]["x"].as<float>(), pts[1]["y"].as<float>()};
    float botRight[2] = {pts[2]["x"].as<float>(), pts[2]["y"].as<float>()};
    float botLeft[2] = {pts[3]["x"].as<float>(), pts[3]["y"].as<float>()};
    
    Serial.print("[processAmbilight] Eckpunkte (Original): TL(");
    Serial.print(topLeft[0]); Serial.print(","); Serial.print(topLeft[1]);
    Serial.print(") TR(");
    Serial.print(topRight[0]); Serial.print(","); Serial.print(topRight[1]);
    Serial.print(") BR(");
    Serial.print(botRight[0]); Serial.print(","); Serial.print(botRight[1]);
    Serial.print(") BL(");
    Serial.print(botLeft[0]); Serial.print(","); Serial.print(botLeft[1]);
    Serial.println(")");
    
    // Koordinaten für 2x-Skalierung halbieren (640x480 -> 320x240)
    // Dies reduziert den Speicherverbrauch von ~600KB auf ~150KB
    topLeft[0] /= 2.0; topLeft[1] /= 2.0;
    topRight[0] /= 2.0; topRight[1] /= 2.0;
    botRight[0] /= 2.0; botRight[1] /= 2.0;
    botLeft[0] /= 2.0; botLeft[1] /= 2.0;
    
    Serial.print("[processAmbilight] Eckpunkte (Skaliert): TL(");
    Serial.print(topLeft[0]); Serial.print(","); Serial.print(topLeft[1]);
    Serial.print(") TR(");
    Serial.print(topRight[0]); Serial.print(","); Serial.print(topRight[1]);
    Serial.print(") BR(");
    Serial.print(botRight[0]); Serial.print(","); Serial.print(botRight[1]);
    Serial.print(") BL(");
    Serial.print(botLeft[0]); Serial.print(","); Serial.print(botLeft[1]);
    Serial.println(")");

    // Rechtecke berechnen
    Serial.println("[processAmbilight] Berechne Fenster-Geometrie...");
    std::vector<WindowRect> topRects, bottomRects, leftRects, rightRects;
    calculateAmbilightWindows(
        topLeft, topRight, botLeft, botRight,
        xwindows, ywindows,
        topRects, bottomRects, leftRects, rightRects
    );
    
    Serial.print("[processAmbilight] Fenster berechnet: top=");
    Serial.print(topRects.size());
    Serial.print(", bottom=");
    Serial.print(bottomRects.size());
    Serial.print(", left=");
    Serial.print(leftRects.size());
    Serial.print(", right=");
    Serial.println(rightRects.size());

    // Kamera-Frame holen (JPEG)
    Serial.println("[processAmbilight] Hole Kamera-Frame...");
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[processAmbilight] ERROR: Failed to get camera frame");
        return "{\"error\":\"Camera frame failed\"}";
    }
    
    Serial.print("[processAmbilight] Frame erhalten: ");
    Serial.print(fb->width);
    Serial.print("x");
    Serial.print(fb->height);
    Serial.print(", ");
    Serial.print(fb->len);
    Serial.println(" bytes");

    // JPEG zu RGB565 konvertieren für Farbberechnung
    Serial.println("[processAmbilight] Konvertiere JPEG zu RGB565...");
    
    // Bild wird 2x skaliert (640x480 -> 320x240), um Speicher zu sparen
    // Original: 640 * 480 * 2 = 614.400 Bytes (~600 KB) - zu viel für ESP32!
    // Skaliert: 320 * 240 * 2 = 153.600 Bytes (~150 KB) - passt in RAM!
    int width = fb->width / 2;
    int height = fb->height / 2;
    
    // RGB565 benötigt 2 Bytes pro Pixel
    size_t rgb_len = width * height * 2;
    Serial.print("[processAmbilight] Allokiere RGB-Buffer (");
    Serial.print(width); Serial.print("x"); Serial.print(height);
    Serial.print("): ");
    Serial.print(rgb_len);
    Serial.println(" bytes");
    
    uint8_t *rgb_buf = (uint8_t*)malloc(rgb_len);
    
    if (!rgb_buf) {
        Serial.println("[processAmbilight] ERROR: Failed to allocate RGB buffer");
        esp_camera_fb_return(fb);
        return "{\"error\":\"Memory allocation failed\"}";
    }
    Serial.println("[processAmbilight] RGB-Buffer allokiert");
    
    bool converted = jpg2rgb565(fb->buf, fb->len, rgb_buf, JPG_SCALE_2X);
    
    if (!converted) {
        Serial.println("[processAmbilight] ERROR: JPEG conversion failed");
        free(rgb_buf);
        esp_camera_fb_return(fb);
        return "{\"error\":\"JPEG conversion failed\"}";
    }
    Serial.println("[processAmbilight] JPEG erfolgreich konvertiert");

    // JSON-Response erstellen
    Serial.println("[processAmbilight] Erstelle JSON-Response...");
    DynamicJsonDocument outDoc(8192); // Größerer Buffer für alle Farben
    JsonArray topColors = outDoc.createNestedArray("top");
    JsonArray bottomColors = outDoc.createNestedArray("bottom");
    JsonArray leftColors = outDoc.createNestedArray("left");
    JsonArray rightColors = outDoc.createNestedArray("right");

    // Zusätzlich: Rechteck-Koordinaten für Visualisierung
    JsonArray topRectsJson = outDoc.createNestedArray("topRects");
    JsonArray bottomRectsJson = outDoc.createNestedArray("bottomRects");
    JsonArray leftRectsJson = outDoc.createNestedArray("leftRects");
    JsonArray rightRectsJson = outDoc.createNestedArray("rightRects");

    // === FARBBERECHNUNG: Wähle zwischen zwei Methoden ===
    // Option 1: calculateMeanRGB  - RMS-Mittelwert (Original aus ambivios.py)
    // Option 2: calculateMeanRGB2 - Gamma-korrigierter Mittelwert (visuell korrekt)
    // Zum Wechseln: Ersetze "calculateMeanRGB" durch "calculateMeanRGB2" in den 4 Blöcken unten
    
    // Top-Farben berechnen
    Serial.println("[processAmbilight] Berechne Top-Farben...");
    for (const auto& rect : topRects) {
        RGB color = calculateMeanRGB(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        JsonArray colorArray = topColors.createNestedArray();
        colorArray.add(color.r);
        colorArray.add(color.g);
        colorArray.add(color.b);
        
        JsonObject rectObj = topRectsJson.createNestedObject();
        rectObj["x1"] = rect.x1;
        rectObj["y1"] = rect.y1;
        rectObj["x2"] = rect.x2;
        rectObj["y2"] = rect.y2;
    }

    Serial.println("[processAmbilight] Berechne Bottom-Farben...");
    // Bottom-Farben berechnen
    for (const auto& rect : bottomRects) {
        RGB color = calculateMeanRGB(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        JsonArray colorArray = bottomColors.createNestedArray();
        colorArray.add(color.r);
        colorArray.add(color.g);
        colorArray.add(color.b);
        
        JsonObject rectObj = bottomRectsJson.createNestedObject();
        rectObj["x1"] = rect.x1;
        rectObj["y1"] = rect.y1;
        rectObj["x2"] = rect.x2;
        rectObj["y2"] = rect.y2;
    }

    Serial.println("[processAmbilight] Berechne Left-Farben...");
    // Left-Farben berechnen
    for (const auto& rect : leftRects) {
        RGB color = calculateMeanRGB(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        JsonArray colorArray = leftColors.createNestedArray();
        colorArray.add(color.r);
        colorArray.add(color.g);
        colorArray.add(color.b);
        
        JsonObject rectObj = leftRectsJson.createNestedObject();
        rectObj["x1"] = rect.x1;
        rectObj["y1"] = rect.y1;
        rectObj["x2"] = rect.x2;
        rectObj["y2"] = rect.y2;
    }

    Serial.println("[processAmbilight] Berechne Right-Farben...");
    // Right-Farben berechnen
    for (const auto& rect : rightRects) {
        RGB color = calculateMeanRGB(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        JsonArray colorArray = rightColors.createNestedArray();
        colorArray.add(color.r);
        colorArray.add(color.g);
        colorArray.add(color.b);
        
        JsonObject rectObj = rightRectsJson.createNestedObject();
        rectObj["x1"] = rect.x1;
        rectObj["y1"] = rect.y1;
        rectObj["x2"] = rect.x2;
        rectObj["y2"] = rect.y2;
    }

    // Aufräumen
    Serial.println("[processAmbilight] Räume Speicher auf...");
    free(rgb_buf);
    esp_camera_fb_return(fb);

    // JSON serialisieren
    Serial.println("[processAmbilight] Serialisiere JSON-Response...");
    String response;
    serializeJson(outDoc, response);
    
    Serial.print("[processAmbilight] Response-Größe: ");
    Serial.print(response.length());
    Serial.println(" bytes");
    Serial.println("[processAmbilight] === SUCCESS ===");
    return response;
}

// ============================================================================
// NEUE API FÜR KONTINUIERLICHE BERECHNUNG
// ============================================================================

// Aktualisiert die Konfiguration aus JSON (ersetzt processAmbilight für Config-Update)
void updateAmbilightConfig(const String& jsonInput) {
    Serial.println("[updateConfig] Aktualisiere Ambilight-Konfiguration...");
    
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, jsonInput);
    
    if (err) {
        Serial.println("[updateConfig] ERROR: JSON parse error");
        g_ambilightConfig.isValid = false;
        return;
    }
    
    JsonArray pts = doc["points"].as<JsonArray>();
    if (pts.size() != 4) {
        Serial.print("[updateConfig] ERROR: Falsche Punktanzahl: ");
        Serial.println(pts.size());
        g_ambilightConfig.isValid = false;
        return;
    }
    
    // Eckpunkte extrahieren und direkt skalieren (640x480 -> 320x240)
    g_ambilightConfig.topLeft[0] = pts[0]["x"].as<float>() / 2.0;
    g_ambilightConfig.topLeft[1] = pts[0]["y"].as<float>() / 2.0;
    g_ambilightConfig.topRight[0] = pts[1]["x"].as<float>() / 2.0;
    g_ambilightConfig.topRight[1] = pts[1]["y"].as<float>() / 2.0;
    g_ambilightConfig.botRight[0] = pts[2]["x"].as<float>() / 2.0;
    g_ambilightConfig.botRight[1] = pts[2]["y"].as<float>() / 2.0;
    g_ambilightConfig.botLeft[0] = pts[3]["x"].as<float>() / 2.0;
    g_ambilightConfig.botLeft[1] = pts[3]["y"].as<float>() / 2.0;
    
    int hSeg = doc["hSeg"].as<int>();
    int vSeg = doc["vSeg"].as<int>();
    g_ambilightConfig.hSeg = (hSeg > 0) ? hSeg : 10;  // Default: 10
    g_ambilightConfig.vSeg = (vSeg > 0) ? vSeg : 8;   // Default: 8
    g_ambilightConfig.isValid = true;
    
    Serial.print("[updateConfig] Konfiguration gesetzt: TL(");
    Serial.print(g_ambilightConfig.topLeft[0]); Serial.print(","); Serial.print(g_ambilightConfig.topLeft[1]);
    Serial.print(") TR(");
    Serial.print(g_ambilightConfig.topRight[0]); Serial.print(","); Serial.print(g_ambilightConfig.topRight[1]);
    Serial.print(") BR(");
    Serial.print(g_ambilightConfig.botRight[0]); Serial.print(","); Serial.print(g_ambilightConfig.botRight[1]);
    Serial.print(") BL(");
    Serial.print(g_ambilightConfig.botLeft[0]); Serial.print(","); Serial.print(g_ambilightConfig.botLeft[1]);
    Serial.print(") hSeg=");
    Serial.print(g_ambilightConfig.hSeg);
    Serial.print(" vSeg=");
    Serial.println(g_ambilightConfig.vSeg);
}

// Führt eine Ambilight-Berechnung durch und speichert das Ergebnis im globalen State
void calculateAmbilightContinuous() {
    // Nur berechnen wenn Konfiguration gültig ist
    if (!g_ambilightConfig.isValid) {
        // Kein Log hier, sonst Spam in Console
        g_ambilightResult.isValid = false;
        return;
    }
    
    // Rechtecke berechnen
    std::vector<WindowRect> topRects, bottomRects, leftRects, rightRects;
    calculateAmbilightWindows(
        g_ambilightConfig.topLeft, g_ambilightConfig.topRight, 
        g_ambilightConfig.botLeft, g_ambilightConfig.botRight,
        g_ambilightConfig.hSeg, g_ambilightConfig.vSeg,
        topRects, bottomRects, leftRects, rightRects
    );
    
    // Kamera-Frame holen
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        // Kamera ist busy (wahrscheinlich vom Stream benutzt)
        // Behalte das letzte gültige Ergebnis bei, anstatt es zu invalidieren
        static unsigned long lastErrorLog = 0;
        if (millis() - lastErrorLog > 10000) {
            Serial.println("[calculateContinuous] Camera busy, keeping last result");
            lastErrorLog = millis();
        }
        return; // Beende ohne isValid zu ändern
    }
    
    // JPEG zu RGB565 konvertieren mit 2x Skalierung (640x480 -> 320x240)
    int width = fb->width / 2;
    int height = fb->height / 2;
    size_t rgb_len = width * height * 2;
    
    uint8_t *rgb_buf = (uint8_t*)malloc(rgb_len);
    if (!rgb_buf) {
        Serial.println("[calculateContinuous] ERROR: Out of memory");
        esp_camera_fb_return(fb);
        return; // Behalte letztes Ergebnis
    }
    
    bool converted = jpg2rgb565(fb->buf, fb->len, rgb_buf, JPG_SCALE_2X);
    if (!converted) {
        Serial.println("[calculateContinuous] ERROR: JPEG conversion failed");
        free(rgb_buf);
        esp_camera_fb_return(fb);
        return; // Behalte letztes Ergebnis
    }
    
    // Farben berechnen und in globalen Vektoren speichern
    g_ambilightResult.topColors.clear();
    g_ambilightResult.bottomColors.clear();
    g_ambilightResult.leftColors.clear();
    g_ambilightResult.rightColors.clear();
    
    for (const auto& rect : topRects) {
        RGB color = calculateMeanRGB2(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        g_ambilightResult.topColors.push_back(color);
    }
    
    for (const auto& rect : bottomRects) {
        RGB color = calculateMeanRGB2(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        g_ambilightResult.bottomColors.push_back(color);
    }
    
    for (const auto& rect : leftRects) {
        RGB color = calculateMeanRGB2(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        g_ambilightResult.leftColors.push_back(color);
    }
    
    for (const auto& rect : rightRects) {
        RGB color = calculateMeanRGB2(rgb_buf, width, height, rect.x1, rect.y1, rect.x2, rect.y2);
        g_ambilightResult.rightColors.push_back(color);
    }
    
    // Rechtecke speichern
    g_ambilightResult.topRects = topRects;
    g_ambilightResult.bottomRects = bottomRects;
    g_ambilightResult.leftRects = leftRects;
    g_ambilightResult.rightRects = rightRects;
    
    Serial.print("[calculateContinuous] Gespeichert: Top=");
    Serial.print(g_ambilightResult.topColors.size());
    Serial.print("/");
    Serial.print(g_ambilightResult.topRects.size());
    Serial.print(", Left=");
    Serial.print(g_ambilightResult.leftColors.size());
    Serial.print("/");
    Serial.print(g_ambilightResult.leftRects.size());
    Serial.print(", Right=");
    Serial.print(g_ambilightResult.rightColors.size());
    Serial.print("/");
    Serial.println(g_ambilightResult.rightRects.size());
    
    g_ambilightResult.timestamp = millis();
    g_ambilightResult.isValid = true;
    
    // Aufräumen
    free(rgb_buf);
    esp_camera_fb_return(fb);
}

// Gibt das gespeicherte Ergebnis als JSON zurück (ohne neue Berechnung)
String getAmbilightResult() {
    if (!g_ambilightResult.isValid) {
        return "{\"error\":\"No data available\"}";
    }
    
    Serial.print("[getResult] Serialisiere: Top=");
    Serial.print(g_ambilightResult.topColors.size());
    Serial.print(", Left=");
    Serial.print(g_ambilightResult.leftColors.size());
    Serial.print(", Right=");
    Serial.println(g_ambilightResult.rightColors.size());
    
    // DynamicJsonDocument für automatische Größenanpassung
    // Geschätzt: 32 Rechtecke * 100 Bytes = 3200 + Overhead = ~4000 Bytes
    DynamicJsonDocument doc(6144);
    
    // Top-Farben und Rechtecke
    JsonArray topColors = doc.createNestedArray("top");
    JsonArray topRectsJson = doc.createNestedArray("topRects");
    for (size_t i = 0; i < g_ambilightResult.topColors.size(); i++) {
        JsonArray colorArray = topColors.createNestedArray();
        colorArray.add(g_ambilightResult.topColors[i].r);
        colorArray.add(g_ambilightResult.topColors[i].g);
        colorArray.add(g_ambilightResult.topColors[i].b);
        
        JsonObject rectObj = topRectsJson.createNestedObject();
        rectObj["x1"] = g_ambilightResult.topRects[i].x1;
        rectObj["y1"] = g_ambilightResult.topRects[i].y1;
        rectObj["x2"] = g_ambilightResult.topRects[i].x2;
        rectObj["y2"] = g_ambilightResult.topRects[i].y2;
    }
    
    // Bottom-Farben und Rechtecke
    JsonArray bottomColors = doc.createNestedArray("bottom");
    JsonArray bottomRectsJson = doc.createNestedArray("bottomRects");
    for (size_t i = 0; i < g_ambilightResult.bottomColors.size(); i++) {
        JsonArray colorArray = bottomColors.createNestedArray();
        colorArray.add(g_ambilightResult.bottomColors[i].r);
        colorArray.add(g_ambilightResult.bottomColors[i].g);
        colorArray.add(g_ambilightResult.bottomColors[i].b);
        
        JsonObject rectObj = bottomRectsJson.createNestedObject();
        rectObj["x1"] = g_ambilightResult.bottomRects[i].x1;
        rectObj["y1"] = g_ambilightResult.bottomRects[i].y1;
        rectObj["x2"] = g_ambilightResult.bottomRects[i].x2;
        rectObj["y2"] = g_ambilightResult.bottomRects[i].y2;
    }
    
    // Left-Farben und Rechtecke
    JsonArray leftColors = doc.createNestedArray("left");
    JsonArray leftRectsJson = doc.createNestedArray("leftRects");
    for (size_t i = 0; i < g_ambilightResult.leftColors.size(); i++) {
        JsonArray colorArray = leftColors.createNestedArray();
        colorArray.add(g_ambilightResult.leftColors[i].r);
        colorArray.add(g_ambilightResult.leftColors[i].g);
        colorArray.add(g_ambilightResult.leftColors[i].b);
        
        JsonObject rectObj = leftRectsJson.createNestedObject();
        rectObj["x1"] = g_ambilightResult.leftRects[i].x1;
        rectObj["y1"] = g_ambilightResult.leftRects[i].y1;
        rectObj["x2"] = g_ambilightResult.leftRects[i].x2;
        rectObj["y2"] = g_ambilightResult.leftRects[i].y2;
    }
    
    // Right-Farben und Rechtecke
    JsonArray rightColors = doc.createNestedArray("right");
    JsonArray rightRectsJson = doc.createNestedArray("rightRects");
    for (size_t i = 0; i < g_ambilightResult.rightColors.size(); i++) {
        JsonArray colorArray = rightColors.createNestedArray();
        colorArray.add(g_ambilightResult.rightColors[i].r);
        colorArray.add(g_ambilightResult.rightColors[i].g);
        colorArray.add(g_ambilightResult.rightColors[i].b);
        
        JsonObject rectObj = rightRectsJson.createNestedObject();
        rectObj["x1"] = g_ambilightResult.rightRects[i].x1;
        rectObj["y1"] = g_ambilightResult.rightRects[i].y1;
        rectObj["x2"] = g_ambilightResult.rightRects[i].x2;
        rectObj["y2"] = g_ambilightResult.rightRects[i].y2;
    }
    
    doc["timestamp"] = g_ambilightResult.timestamp;
    
    String response;
    size_t jsonSize = serializeJson(doc, response);
    
    Serial.print("[getResult] JSON-Größe: ");
    Serial.print(jsonSize);
    Serial.print(" bytes (capacity: 6144, overflow: ");
    Serial.print(doc.overflowed() ? "JA!" : "nein");
    Serial.print(", free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(")");
    
    return response;
}
