#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "index_html.h"
#include "windows.h"

// Kamera-Pinbelegung für AI-Thinker ESP32-CAM
// Quelle: https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/CameraWebServer.ino
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

static esp_err_t init_camera()
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if(psramFound()){
        config.frame_size = FRAMESIZE_VGA; // 640x480
        config.jpeg_quality = 12; // mittlere Qualität
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 15;
        config.fb_count = 1;
    }

    // Kameramodul initialisieren
    return esp_camera_init(&config);
}

void handle_root()
{
    Serial.println("[handle_root] Root page requested");
    server.send_P(200, "text/html", INDEX_HTML);
}

// Sendet einzelnes JPEG-Snapshot (ersetzt kontinuierlichen Stream)
void handle_snapshot()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[snapshot] ERROR: Failed to get camera frame");
        server.send(500, "text/plain", "Camera error");
        return;
    }
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "-1");
    server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
    
    esp_camera_fb_return(fb);
}

// Global request logger - wird für JEDEN Request aufgerufen
void logRequest() {
    Serial.print("[REQUEST] ");
    Serial.print(server.method() == HTTP_GET ? "GET" : "POST");
    Serial.print(" ");
    Serial.print(server.uri());
    Serial.print(" from ");
    Serial.println(server.client().remoteIP());
}

// API: empfängt 4 Punkte und Segmentzahlen, berechnet Zwischenpunkte
void handle_grid()
{
    Serial.println("[handle_grid] === GRID REQUEST RECEIVED ===");
    if (server.hasArg("plain") == false) {
        Serial.println("[handle_grid] No body");
        server.send(400, "text/plain", "No body");
        return;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        Serial.println("[handle_grid] JSON parse error");
        server.send(400, "text/plain", "JSON parse error");
        return;
    }

    JsonArray pts = doc["points"].as<JsonArray>();
    if (pts.size() != 4) {
        Serial.print("[handle_grid] pts size:"); Serial.println(pts.size());
        server.send(400, "text/plain", "Need 4 points");
        return;
    }

    int hSeg = doc["hSeg"].as<int>() | 1;
    int vSeg = doc["vSeg"].as<int>() | 1;

    struct P { float x; float y; };
    P p[4];
    for (int i = 0; i < 4; ++i) {
        p[i].x = pts[i]["x"].as<float>();
        p[i].y = pts[i]["y"].as<float>();
    }

    DynamicJsonDocument outDoc(2048);
    JsonArray arr = outDoc.createNestedArray("points");

    auto addIntermediates = [&](P a, P b, int count) {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        for (int i = 1; i < count; ++i) {
            float t = (float)i / count;
            JsonObject obj = arr.createNestedObject();
            obj["x"] = a.x + dx * t;
            obj["y"] = a.y + dy * t;
        }
    };

    // horizontale Linien 1-2 und 3-4
    addIntermediates(p[0], p[1], hSeg);
    addIntermediates(p[2], p[3], hSeg);
    // vertikale Linien 2-3 und 4-1
    addIntermediates(p[1], p[2], vSeg);
    addIntermediates(p[3], p[0], vSeg);

    serializeJsonPretty(outDoc, Serial);
    Serial.println();

    String response;
    serializeJson(outDoc, response);
    server.send(200, "application/json", response);
}

// API: Konfiguration setzen (ersetzt /api/grid)
void handle_config()
{
    Serial.println("[handle_config] === CONFIG REQUEST RECEIVED ===");
    
    if (server.hasArg("plain") == false) {
        Serial.println("[handle_config] No body");
        server.send(400, "text/plain", "No body");
        return;
    }
    
    updateAmbilightConfig(server.arg("plain"));
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    Serial.println("[handle_config] Config updated!");
}

// API: Ambilight-Daten abrufen (GET - gibt gecachte Daten zurück)
void handle_ambilight()
{
    Serial.println("[handle_ambilight] === AMBILIGHT GET REQUEST ===");
    
    String response = getAmbilightResult();
    
    server.send(200, "application/json", response);
}

void setup()
{
    Serial.begin(115200);
    Serial.setDebugOutput(false);

    if (init_camera() != ESP_OK) {
        Serial.println("Kamera-Initialisierung fehlgeschlagen");
        return;
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("Verbinde mit WLAN ...");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWLAN verbunden. IP: " + WiFi.localIP().toString());

    // Globaler Request-Logger für ALLE Requests
    server.onNotFound([]() {
        Serial.print("[NOT_FOUND] ");
        Serial.print(server.method() == HTTP_GET ? "GET" : "POST");
        Serial.print(" ");
        Serial.println(server.uri());
        server.send(404, "text/plain", "Not found");
    });

    server.on("/", HTTP_GET, []() {
        logRequest();
        handle_root();
    });
    server.on("/api/snapshot", HTTP_GET, []() {
        logRequest();
        handle_snapshot();
    });
    server.on("/api/grid", HTTP_POST, []() {
        logRequest();
        handle_grid();
    });
    server.on("/api/config", HTTP_POST, []() {
        logRequest();
        handle_config();
    });
    server.on("/api/ambilight", HTTP_GET, []() {
        logRequest();
        handle_ambilight();
    });
    server.begin();
    
    Serial.print("HTTP-Server gestartet. Free heap: ");
    Serial.println(ESP.getFreeHeap());
}

void loop()
{
    server.handleClient();
    
    // Kontinuierliche Ambilight-Berechnung (100ms = 10 FPS für anderen ESP)
    static unsigned long lastAmbilight = 0;
    unsigned long now = millis();
    if (now - lastAmbilight > 100) {
        calculateAmbilightContinuous();
        lastAmbilight = now;
    }
    
    // Heartbeat alle 10 Sekunden
    static unsigned long lastHeartbeat = 0;
    if (now - lastHeartbeat > 10000) {
        Serial.println("[loop] Heartbeat - Server läuft");
        lastHeartbeat = now;
    }
}
