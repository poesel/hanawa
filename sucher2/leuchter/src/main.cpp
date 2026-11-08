#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include <ArduinoJson.h>
#include "apa102.h"
#include "protocol.h"
#include "index_html.h"

// ============================================
// KONFIGURATION
// ============================================

// WiFi-Konfiguration
const char* WIFI_SSID = "Zippen 24";
const char* WIFI_PASSWORD = "Boyzoneanker24";

// Feste IP (optional)
IPAddress local_IP(192, 168, 0, 121);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

// LED-Konfiguration
#define LED_COUNT 32                // Anzahl LEDs (muss mit Sucher übereinstimmen)
#define DEFAULT_BRIGHTNESS 50       // Standard-Helligkeit (0-100%)

// ============================================
// GLOBALE OBJEKTE
// ============================================

APA102 leds;
ESP8266WebServer server(80);

// Helligkeit (Web-UI)
int g_brightness_percent = DEFAULT_BRIGHTNESS;

// Testmodus
bool g_test_mode = false;
uint32_t g_test_mode_last_update = 0;

// ============================================
// ESP-NOW CALLBACK
// ============================================

// ESP8266 ESP-NOW Callback hat unterschiedliche Signatur
void on_esp_now_recv(uint8_t* mac_addr, uint8_t* data, uint8_t len) {
    // Im Testmodus: Pakete verwerfen
    if (g_test_mode) {
        Serial.println("[ESP-NOW] Paket im Testmodus verworfen");
        return;
    }
    
    // Weiterleitung an Protokoll-Handler
    protocol_on_receive(mac_addr, data, len);
    
    // Wenn Frame komplett: LEDs aktualisieren
    if (g_frame.valid) {
        int num_rectangles = protocol_get_total_rectangles();
        
        // RGB-Daten auf LEDs mappen (direkt, uhrzeigersinn)
        for (int i = 0; i < LED_COUNT && i < num_rectangles; i++) {
            uint8_t r = g_frame.rgb_data[i * 3 + 0];
            uint8_t g = g_frame.rgb_data[i * 3 + 1];
            uint8_t b = g_frame.rgb_data[i * 3 + 2];
            leds.setPixel(i, r, g, b);
        }
        
        // Nicht genutzte LEDs ausschalten
        for (int i = num_rectangles; i < LED_COUNT; i++) {
            leds.setPixel(i, 0, 0, 0);
        }
        
        leds.show();
        
        // Frame als verarbeitet markieren, damit der nächste Frame empfangen werden kann
        g_frame.valid = false;
    }
}

// ============================================
// WEB-SERVER HANDLER
// ============================================

void handle_root() {
    server.send(200, "text/html", INDEX_HTML);
}

void handle_brightness() {
    if (server.method() == HTTP_POST) {
        // ESP8266WebServer liest Body mit arg("plain")
        String body = server.arg("plain");
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        int brightness = doc["brightness"] | DEFAULT_BRIGHTNESS;
        brightness = constrain(brightness, 0, 100);
        g_brightness_percent = brightness;
        
        // In APA102-Helligkeit umrechnen (0-31)
        uint8_t apa102_brightness = (brightness * 31) / 100;
        leds.setBrightness(apa102_brightness);
        
        Serial.printf("[Web] Helligkeit gesetzt: %d%% (APA102: %d/31)\n", 
                      brightness, apa102_brightness);
        
        DynamicJsonDocument response(128);
        response["brightness"] = brightness;
        response["success"] = true;
        
        String json;
        serializeJson(response, json);
        server.send(200, "application/json", json);
    } else {
        server.send(405, "text/plain", "Method Not Allowed");
    }
}

void handle_testmode() {
    if (server.method() == HTTP_POST) {
        String body = server.arg("plain");
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, body);
        
        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        g_test_mode = doc["testmode"] | false;
        
        Serial.printf("[Web] Testmodus: %s\n", g_test_mode ? "EIN" : "AUS");
        
        if (!g_test_mode) {
            // Testmodus beendet: LEDs ausschalten
            leds.clear();
            leds.show();
        }
        
        DynamicJsonDocument response(128);
        response["testmode"] = g_test_mode;
        response["success"] = true;
        
        String json;
        serializeJson(response, json);
        server.send(200, "application/json", json);
    } else {
        server.send(405, "text/plain", "Method Not Allowed");
    }
}

void handle_status() {
    DynamicJsonDocument doc(2048);
    
    doc["num_leds"] = LED_COUNT;
    doc["frames"] = g_frame.frame_counter;
    doc["fps"] = protocol_get_fps();
    doc["receiving"] = protocol_is_receiving();
    doc["brightness"] = g_brightness_percent;
    doc["testmode"] = g_test_mode;
    doc["h_segments"] = g_frame.h_segments;
    doc["v_segments"] = g_frame.v_segments;
    
    // LED-Farben für Vorschau
    JsonArray colors = doc.createNestedArray("colors");
    int num_rectangles = protocol_get_total_rectangles();
    
    for (int i = 0; i < LED_COUNT && i < num_rectangles; i++) {
        JsonObject color = colors.createNestedObject();
        color["r"] = g_frame.rgb_data[i * 3 + 0];
        color["g"] = g_frame.rgb_data[i * 3 + 1];
        color["b"] = g_frame.rgb_data[i * 3 + 2];
    }
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

void handle_not_found() {
    server.send(404, "text/plain", "404: Not Found");
}

// ============================================
// SETUP
// ============================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=================================");
    Serial.println("ESP8266 Leuchter (D1 Mini) - v1.0");
    Serial.println("=================================");
    
    // === APA102 LEDs initialisieren ===
    leds.begin(LED_COUNT);
    leds.setBrightness((DEFAULT_BRIGHTNESS * 31) / 100);
    
    // Startanimation: Alle LEDs kurz weiß
    for (int i = 0; i < LED_COUNT; i++) {
        leds.setPixel(i, 10, 10, 10);
    }
    leds.show();
    delay(500);
    leds.clear();
    leds.show();
    
    Serial.println("[LEDs] Initialisiert");
    
    // === WiFi initialisieren ===
    WiFi.mode(WIFI_STA);
    
    // Feste IP konfigurieren (optional)
    if (!WiFi.config(local_IP, gateway, subnet)) {
        Serial.println("[WiFi] Fehler bei Konfiguration fester IP");
    }
    
    Serial.printf("[WiFi] Verbinde mit %s...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Verbunden!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("[WiFi] MAC: ");
        Serial.println(WiFi.macAddress());
    } else {
        Serial.println("[WiFi] FEHLER: Keine Verbindung!");
    }
    
    // === ESP-NOW initialisieren ===
    // ESP8266 verwendet andere API als ESP32
    if (esp_now_init() != 0) {
        Serial.println("[ESP-NOW] FEHLER: Initialisierung fehlgeschlagen!");
        return;
    }
    
    Serial.println("[ESP-NOW] Initialisiert");
    
    // Callback registrieren (ESP8266 nutzt esp_now_set_self_role + esp_now_register_recv_cb)
    esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);  // Empfänger
    esp_now_register_recv_cb(on_esp_now_recv);
    Serial.println("[ESP-NOW] Callback registriert");
    
    // === Protokoll initialisieren ===
    protocol_init();
    
    // === Web-Server initialisieren ===
    server.on("/", HTTP_GET, handle_root);
    server.on("/api/brightness", HTTP_POST, handle_brightness);
    server.on("/api/testmode", HTTP_POST, handle_testmode);
    server.on("/api/status", HTTP_GET, handle_status);
    server.onNotFound(handle_not_found);
    
    server.begin();
    Serial.println("[Server] Gestartet auf Port 80");
    
    Serial.println("=================================");
    Serial.println("Bereit!");
    Serial.println("=================================\n");
}

// ============================================
// LOOP
// ============================================

void loop() {
    // Web-Server handling
    server.handleClient();
    
    // Testmodus: Regenbogen-Animation
    if (g_test_mode) {
        if (millis() - g_test_mode_last_update > 50) {  // 20 Hz Update
            g_test_mode_last_update = millis();
            
            static int offset = 0;
            for (int i = 0; i < LED_COUNT; i++) {
                // Regenbogen-Farben berechnen
                int hue = (i * 360 / LED_COUNT + offset) % 360;
                uint8_t r, g, b;
                
                // HSV zu RGB (vereinfacht, nur für Regenbogen)
                if (hue < 60) {
                    r = 255; g = hue * 255 / 60; b = 0;
                } else if (hue < 120) {
                    r = (120 - hue) * 255 / 60; g = 255; b = 0;
                } else if (hue < 180) {
                    r = 0; g = 255; b = (hue - 120) * 255 / 60;
                } else if (hue < 240) {
                    r = 0; g = (240 - hue) * 255 / 60; b = 255;
                } else if (hue < 300) {
                    r = (hue - 240) * 255 / 60; g = 0; b = 255;
                } else {
                    r = 255; g = 0; b = (360 - hue) * 255 / 60;
                }
                
                leds.setPixel(i, r, g, b);
            }
            leds.show();
            
            offset = (offset + 5) % 360;  // Animation
        }
    } else {
        // Timeout-Check für unvollständige Frames
        protocol_check_timeout();
    }
    
    // Kurze Pause
    delay(10);
}
