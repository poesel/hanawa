#include "windows.h"
#include <ArduinoJson.h>
#include <vector>
#include <cmath>
#include "esp_camera.h"
#include "img_converters.h"

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

// Berechnet alle Ambilight-Fenster basierend auf den 4 Eckpunkten
// Entspricht der Logik aus ambivios.py (Zeilen 86-120)
void calculateAmbilightWindows(
    float topLeft[], float topRight[], float botLeft[], float botRight[],
    int xwindows, int ywindows, int depth,
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
        // Top
        int x1 = topLeft[0] + round(i * xtopwinwidth);
        int y1 = topLeft[1] + round(i * topslope);
        int x2 = x1 + round(xtopwinwidth);
        int y2 = y1 + depth;
        topRects.push_back({x1, y1, x2, y2});

        // Bottom
        x1 = botLeft[0] + round(i * xbotwinwidth);
        y1 = botLeft[1] + round(i * bottomslope) - depth;
        x2 = x1 + round(xbotwinwidth);
        y2 = y1 + depth;
        bottomRects.push_back({x1, y1, x2, y2});
    }

    // Vertikale Fenster (left und right)
    for (int i = 0; i < ywindows; i++) {
        // Left
        int x1 = topLeft[0] + round(i * leftslope);
        int y1 = topLeft[1] + round(i * yleftwinheight);
        int x2 = x1 + depth;
        int y2 = y1 + round(yleftwinheight);
        leftRects.push_back({x1, y1, x2, y2});

        // Right
        x1 = topRight[0] + round(i * rightslope) - depth;
        y1 = topRight[1] + round(i * yrightwinheight);
        x2 = x1 + depth;
        y2 = y1 + round(yrightwinheight);
        rightRects.push_back({x1, y1, x2, y2});
    }
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

    int xwindows = doc["hSeg"].as<int>() | 16;
    int ywindows = doc["vSeg"].as<int>() | 10;
    int depth = doc["depth"].as<int>() | 30;
    
    Serial.print("[processAmbilight] Parameter: hSeg=");
    Serial.print(xwindows);
    Serial.print(", vSeg=");
    Serial.print(ywindows);
    Serial.print(", depth=");
    Serial.println(depth);

    // Eckpunkte extrahieren
    float topLeft[2] = {pts[0]["x"].as<float>(), pts[0]["y"].as<float>()};
    float topRight[2] = {pts[1]["x"].as<float>(), pts[1]["y"].as<float>()};
    float botRight[2] = {pts[2]["x"].as<float>(), pts[2]["y"].as<float>()};
    float botLeft[2] = {pts[3]["x"].as<float>(), pts[3]["y"].as<float>()};
    
    Serial.print("[processAmbilight] Eckpunkte: TL(");
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
        xwindows, ywindows, depth,
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
    int width = fb->width;
    int height = fb->height;
    
    // RGB565 benötigt 2 Bytes pro Pixel
    size_t rgb_len = width * height * 2;
    Serial.print("[processAmbilight] Allokiere RGB-Buffer: ");
    Serial.print(rgb_len);
    Serial.println(" bytes");
    
    uint8_t *rgb_buf = (uint8_t*)malloc(rgb_len);
    
    if (!rgb_buf) {
        Serial.println("[processAmbilight] ERROR: Failed to allocate RGB buffer");
        esp_camera_fb_return(fb);
        return "{\"error\":\"Memory allocation failed\"}";
    }
    Serial.println("[processAmbilight] RGB-Buffer allokiert");
    
    bool converted = jpg2rgb565(fb->buf, fb->len, rgb_buf, JPG_SCALE_NONE);
    
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

