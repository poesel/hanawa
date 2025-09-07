/*
 * Hanawa Sucher - ESP32CAM Farbanalyse für Ambient Lighting
 * 
 * Dieses Programm analysiert die Farben eines Fernsehers über die ESP32CAM
 * und sendet die Farbdaten an den Leuchter.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include "WebServer.h"
#include "ArduinoJson.h"
#include "WiFiUdp.h"
#include "config.h"
#include "webpage.h"

#define PART_BOUNDARY "123456789000000000000987654321"

// AI Thinker ESP32-CAM Pin-Konfiguration
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

// Fernseher-Kalibrierung
struct Point {
  int x, y;
  bool set = false;
};

// Farbdaten für Leuchter
struct ColorData {
  uint8_t r, g, b;
  uint8_t brightness;
};

// Segment-Daten für Visualisierung
struct Segment {
  int x1, y1, x2, y2;
  ColorData color;
};

// Globale Variablen
WiFiUDP udp;
WebServer server(80);
camera_fb_t * fb = NULL;

Point tvCorners[4]; // Punkt 1-4 im Uhrzeigersinn
int horizontalDivisions = DEFAULT_HORIZONTAL_DIVISIONS;
int verticalDivisions = DEFAULT_VERTICAL_DIVISIONS;
bool calibrationMode = true;

ColorData* colorSegments = nullptr;
Segment* visualSegments = nullptr;
int totalSegments = 0;
int currentPoint = 0;

// Funktionsdeklarationen
void setupWebServer();
void calculateSegments();
void analyzeColors();
void analyzeSegment(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, ColorData* colorData);
void sendColorData();
void drawSegmentsOnImage(uint8_t* buffer, int width, int height);
void drawQuadrilateral(uint8_t* buffer, int width, int height);
void drawLine(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b);
void drawRectangle(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, bool filled);

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req){
  if (DEBUG_STREAM) Serial.println("=== STREAM HANDLER START ===");
  
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  if (DEBUG_STREAM) Serial.println("Setze Content-Type...");
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){
    if (DEBUG_STREAM) Serial.printf("Content-Type Set fehlgeschlagen: 0x%x\n", res);
    return res;
  }
  if (DEBUG_STREAM) Serial.println("Content-Type erfolgreich gesetzt");

  int frame_count = 0;
  while(true){
    frame_count++;
    if (DEBUG_STREAM) Serial.printf("--- Frame %d ---\n", frame_count);
    
    if (DEBUG_STREAM) Serial.println("Hole Kamera-Frame...");
    fb = esp_camera_fb_get();
    if (!fb) {
      if (DEBUG_STREAM) Serial.println("❌ Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (DEBUG_STREAM) Serial.printf("✅ Frame erhalten: %dx%d, Format: %d, Länge: %d\n", 
                   fb->width, fb->height, fb->format, fb->len);
      uint8_t* rgb_buf = nullptr;

      if(fb->format == PIXFORMAT_RGB565){
        // Direkt auf Frame-Buffer zeichnen
        rgb_buf = fb->buf;
      } else {
        // Falls Kamera trotzdem JPEG liefert
        if (DEBUG_STREAM) Serial.println("JPEG -> RGB565 konvertieren...");
        rgb_buf = (uint8_t*)malloc(fb->width * fb->height * 2);
        jpg2rgb565(fb->buf, fb->len, rgb_buf, JPG_SCALE_NONE);
      }

      // Visualisierung einzeichnen
      if (!calibrationMode) {
        drawQuadrilateral(rgb_buf, fb->width, fb->height);
        drawSegmentsOnImage(rgb_buf, fb->width, fb->height);
      }

      // RGB565 -> JPEG kodieren
      if (DEBUG_STREAM) Serial.println("RGB565 -> JPEG komprimieren...");
      bool ok = fmt2jpg(rgb_buf, fb->width * fb->height * 2,
                        fb->width, fb->height, PIXFORMAT_RGB565,
                        80, &_jpg_buf, &_jpg_buf_len);
      if(!ok){
        if (DEBUG_STREAM) Serial.println("❌ fmt2jpg fehlgeschlagen");
        res = ESP_FAIL;
      }
      
      // Aufgeräumt
      if(fb->format != PIXFORMAT_RGB565){
        free(rgb_buf);
      }
      esp_camera_fb_return(fb);
      fb = NULL;
      }
    
    if(res == ESP_OK){
      if (DEBUG_STREAM) Serial.println("Sende HTTP Header...");
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
      if(res != ESP_OK && DEBUG_STREAM) {
        Serial.printf("❌ Header senden fehlgeschlagen: 0x%x\n", res);
      }
    }
    
    if(res == ESP_OK){
      if (DEBUG_STREAM) Serial.printf("Sende JPEG-Daten (%d Bytes)...\n", _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
      if(res != ESP_OK && DEBUG_STREAM) {
        Serial.printf("❌ JPEG-Daten senden fehlgeschlagen: 0x%x\n", res);
      }
    }
    
    if(res == ESP_OK){
      if (DEBUG_STREAM) Serial.println("Sende Boundary...");
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
      if(res != ESP_OK && DEBUG_STREAM) {
        Serial.printf("❌ Boundary senden fehlgeschlagen: 0x%x\n", res);
      }
    }
    
    if(fb){
      if (DEBUG_STREAM) Serial.println("Gebe Frame Buffer frei...");
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      if (DEBUG_STREAM) Serial.println("Gebe JPEG Buffer frei...");
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    
    if(res != ESP_OK){
      if (DEBUG_STREAM) Serial.printf("❌ Stream-Fehler: 0x%x\n", res);
      break;
    }
    
    if (DEBUG_STREAM) Serial.printf("✅ Frame %d erfolgreich gesendet\n", frame_count);
    delay(100); // Kleine Pause zwischen Frames
  }
  
  if (DEBUG_STREAM) Serial.println("=== STREAM HANDLER ENDE ===");
  return res;
}

void startCameraServer(){
  if (DEBUG_SERIAL) Serial.println("=== STARTE CAMERA SERVER ===");
  
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  if (DEBUG_SERIAL) Serial.printf("Server-Port: %d\n", config.server_port);

  httpd_uri_t index_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  if (DEBUG_SERIAL) Serial.println("Registriere URI Handler...");
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    if (DEBUG_SERIAL) Serial.println("✅ HTTP Server gestartet");
    httpd_register_uri_handler(stream_httpd, &index_uri);
    if (DEBUG_SERIAL) Serial.println("✅ URI Handler registriert");
  } else {
    if (DEBUG_SERIAL) Serial.println("❌ HTTP Server starten fehlgeschlagen");
  }
  
  if (DEBUG_SERIAL) Serial.println("=== CAMERA SERVER GESTARTET ===");
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(DEBUG_SERIAL);
  
  if (DEBUG_SERIAL) {
    Serial.println("\n\n=== HANAWA SUCHER STARTET ===");
    Serial.println("Version: Vollständig mit Kalibrierung");
    Serial.printf("Debug-Level: %d\n", DEBUG_LEVEL);
    Serial.println("================================");
  }
  
  // Brownout Detector deaktivieren
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  if (DEBUG_SERIAL) Serial.println("✅ Brownout Detector deaktiviert");
  
  if (DEBUG_SERIAL) Serial.println("=== KAMERA-KONFIGURATION ===");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
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
  config.pixel_format = CAMERA_FORMAT;
  
  if (DEBUG_SERIAL) {
    Serial.printf("Kamera-Format: %d\n", CAMERA_FORMAT);
    Serial.printf("Kamera-Qualität: %d\n", CAMERA_JPEG_QUALITY);
    Serial.printf("Frame-Buffer-Count: %d\n", CAMERA_FB_COUNT);
  }
  
  if(psramFound()){
    if (DEBUG_SERIAL) Serial.println("✅ PSRAM gefunden - verwende VGA");
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count = CAMERA_FB_COUNT;
  } else {
    if (DEBUG_SERIAL) Serial.println("⚠️  Kein PSRAM - verwende QVGA");
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count = CAMERA_FB_COUNT;
  }
  
  if (DEBUG_SERIAL) Serial.printf("Frame-Größe: %d\n", config.frame_size);
  
  // Kamera initialisieren
  if (DEBUG_SERIAL) Serial.println("Initialisiere Kamera...");
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    if (DEBUG_SERIAL) {
      Serial.printf("❌ Camera init failed with error 0x%x\n", err);
      Serial.println("Mögliche Ursachen:");
      Serial.println("1. Kamera-Modul nicht angeschlossen");
      Serial.println("2. Stromversorgung unzureichend");
      Serial.println("3. Hardware-Defekt");
    }
    return;
  }
  
  if (DEBUG_SERIAL) Serial.println("✅ Kamera-Initialisierung erfolgreich!");
  
  // WLAN verbinden
  if (DEBUG_SERIAL) {
    Serial.println("=== WLAN-VERBINDUNG ===");
    Serial.printf("SSID: %s\n", WIFI_SSID);
    Serial.println("Verbinde zu WLAN...");
  }
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    if (DEBUG_SERIAL) Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    if (DEBUG_SERIAL) {
      Serial.println("\n❌ WLAN-Verbindung fehlgeschlagen!");
      Serial.println("Prüfe SSID und Passwort");
    }
    return;
  }
  
  if (DEBUG_SERIAL) {
    Serial.println("\n✅ WLAN verbunden!");
    Serial.printf("IP-Adresse: %s\n", WiFi.localIP().toString().c_str());
  }
  
  // Statische IP setzen
  if (DEBUG_SERIAL) Serial.println("Setze statische IP...");
  IPAddress localIP;
  localIP.fromString(SUCHER_IP);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  
  if (DEBUG_SERIAL) {
    Serial.printf("Statische IP: %s\n", SUCHER_IP);
    Serial.printf("Gateway: 192.168.0.1\n");
    Serial.printf("Subnet: 255.255.255.0\n");
  }
  
  WiFi.config(localIP, gateway, subnet);
  
  if (DEBUG_SERIAL) Serial.println("Warte auf IP-Konfiguration...");
  delay(2000);
  
  if (DEBUG_SERIAL) Serial.printf("Finale IP-Adresse: %s\n", WiFi.localIP().toString().c_str());
  
  // Streaming Web Server starten
  startCameraServer();
  
  // Web-Server Routen
  setupWebServer();
  
  // UDP initialisieren
  udp.begin(SUCHER_PORT);
  if (DEBUG_SERIAL) Serial.printf("UDP-Server gestartet auf Port %d\n", SUCHER_PORT);
  
  if (DEBUG_SERIAL) {
    Serial.println("=== SETUP ABGESCHLOSSEN ===");
    Serial.printf("🎥 Live-Stream verfügbar unter: http://%s\n", SUCHER_IP);
    Serial.printf("🌐 Web-Interface verfügbar unter: http://%s\n", SUCHER_IP);
    Serial.println("================================");
  }
}

void loop() {
  server.handleClient();
  
  if (!calibrationMode && totalSegments > 0) {
    analyzeColors();
    sendColorData();
    delay(1000 / ANALYSIS_FPS); // Konfigurierbare FPS
  }
  
  // Status alle 10 Sekunden
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 10000 && DEBUG_SERIAL) {
    Serial.println("=== STATUS UPDATE ===");
    Serial.printf("WLAN-Status: %d\n", WiFi.status());
    Serial.printf("IP-Adresse: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Freier RAM: %d Bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime: %lu Sekunden\n", millis() / 1000);
    Serial.printf("Kalibrierungsmodus: %s\n", calibrationMode ? "Ja" : "Nein");
    Serial.printf("Segmente: %d\n", totalSegments);
    Serial.println("====================");
    lastStatus = millis();
  }
  
  delay(100);
}

void setupWebServer() {
  // Hauptseite
  server.on("/", HTTP_GET, []() {
    if (DEBUG_SERIAL) Serial.println("Hauptseite angefordert");
    server.send(200, "text/html", MAIN_page);
  });
  
  // Kalibrierung
  server.on("/calibrate", HTTP_POST, []() {
    if (DEBUG_SERIAL) Serial.println("=== KALIBRIERUNG EMPFANGEN ===");
    
    String data = server.arg("plain");
    if (DEBUG_SERIAL) Serial.printf("Empfangene Daten: %s\n", data.c_str());
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data);
    
    int pointIndex = doc["point"];
    int x = doc["x"];
    int y = doc["y"];
    
    if (DEBUG_SERIAL) Serial.printf("Punkt %d: (%d, %d)\n", pointIndex + 1, x, y);
    
    if (pointIndex >= 0 && pointIndex < 4) {
      tvCorners[pointIndex].x = x;
      tvCorners[pointIndex].y = y;
      tvCorners[pointIndex].set = true;
      
      if (DEBUG_SERIAL) Serial.printf("Punkt %d gesetzt: (%d, %d)\n", pointIndex, x, y);
      
      // Prüfen ob alle Punkte gesetzt sind
      bool allSet = true;
      for (int i = 0; i < 4; i++) {
        if (!tvCorners[i].set) {
          allSet = false;
          if (DEBUG_SERIAL) Serial.printf("Punkt %d noch nicht gesetzt\n", i);
        }
      }
      
      if (allSet) {
        calibrationMode = false;
        if (DEBUG_SERIAL) {
          Serial.println("✅ Kalibrierung abgeschlossen!");
          Serial.println("Alle Punkte gesetzt:");
          for (int i = 0; i < 4; i++) {
            Serial.printf("Punkt %d: (%d, %d)\n", i, tvCorners[i].x, tvCorners[i].y);
          }
        }
        calculateSegments();
      } else {
        if (DEBUG_SERIAL) Serial.printf("Noch %d Punkte fehlen\n", 4 - (pointIndex + 1));
      }
    } else {
      if (DEBUG_SERIAL) Serial.printf("Ungültiger Punkt-Index: %d\n", pointIndex);
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  
  // Parameter setzen
  server.on("/setParams", HTTP_POST, []() {
    if (DEBUG_SERIAL) Serial.println("Parameter empfangen");
    
    String data = server.arg("plain");
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data);
    
    horizontalDivisions = doc["horizontal"];
    verticalDivisions = doc["vertical"];
    
    if (DEBUG_SERIAL) Serial.printf("Horizontale Teilungen: %d\n", horizontalDivisions);
    if (DEBUG_SERIAL) Serial.printf("Vertikale Teilungen: %d\n", verticalDivisions);
    
    if (!calibrationMode) {
      calculateSegments();
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  
  // Status
  server.on("/status", HTTP_GET, []() {
    DynamicJsonDocument doc(1024);
    doc["calibrationMode"] = calibrationMode;
    doc["horizontalDivisions"] = horizontalDivisions;
    doc["verticalDivisions"] = verticalDivisions;
    doc["totalSegments"] = totalSegments;
    doc["fps"] = ANALYSIS_FPS;
    
    for (int i = 0; i < 4; i++) {
      doc["corners"][i]["x"] = tvCorners[i].x;
      doc["corners"][i]["y"] = tvCorners[i].y;
      doc["corners"][i]["set"] = tvCorners[i].set;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  // Reset-Kalibrierung
  server.on("/reset", HTTP_POST, []() {
    if (DEBUG_SERIAL) Serial.println("Reset-Kalibrierung angefordert");
    
    // Alle Punkte zurücksetzen
    for (int i = 0; i < 4; i++) {
      tvCorners[i].set = false;
    }
    calibrationMode = true;
    currentPoint = 0;
    
    if (DEBUG_SERIAL) Serial.println("✅ Kalibrierung zurückgesetzt");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  
  server.begin();
  if (DEBUG_SERIAL) Serial.println("✅ Web-Server gestartet");
}

void calculateSegments() {
  if (DEBUG_SERIAL) Serial.println("=== BERECHNE SEGMENTE ===");
  
  // Berechne Gesamtanzahl der Segmente
  totalSegments = (horizontalDivisions * 2) + (verticalDivisions * 2);
  
  if (DEBUG_SERIAL) {
    Serial.printf("Horizontale Teilungen: %d\n", horizontalDivisions);
    Serial.printf("Vertikale Teilungen: %d\n", verticalDivisions);
    Serial.printf("Gesamtsegmente: %d\n", totalSegments);
  }
  
  // Speicher für Farbdaten allozieren
  if (colorSegments != nullptr) {
    delete[] colorSegments;
    if (DEBUG_SERIAL) Serial.println("Alten colorSegments-Speicher freigegeben");
  }
  colorSegments = new ColorData[totalSegments];
  
  // Speicher für Visualisierung allozieren
  if (visualSegments != nullptr) {
    delete[] visualSegments;
    if (DEBUG_SERIAL) Serial.println("Alten visualSegments-Speicher freigegeben");
  }
  visualSegments = new Segment[totalSegments];
  
  if (DEBUG_SERIAL) {
    Serial.printf("✅ Segmente berechnet: %d\n", totalSegments);
    Serial.printf("Speicher alloziiert: %d Bytes\n", totalSegments * (sizeof(ColorData) + sizeof(Segment)));
  }
}

void analyzeColors() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;
  
  // Konvertiere zu RGB565 falls nötig
  uint8_t* rgb_buffer = nullptr;
  if (fb->format == PIXFORMAT_JPEG) {
    // JPEG zu RGB konvertieren
    rgb_buffer = (uint8_t*)malloc(fb->width * fb->height * 2);
    jpg2rgb565(fb->buf, fb->len, rgb_buffer, JPG_SCALE_NONE);
  } else {
    rgb_buffer = fb->buf;
  }
  
  // Berechne Steigungen für automatische Skalierung
  float topSlope = (tvCorners[1].y - tvCorners[0].y) / (float)horizontalDivisions;
  float bottomSlope = (tvCorners[2].y - tvCorners[3].y) / (float)horizontalDivisions;
  float leftSlope = (tvCorners[3].x - tvCorners[0].x) / (float)verticalDivisions;
  float rightSlope = (tvCorners[2].x - tvCorners[1].x) / (float)verticalDivisions;
  
  // Analysiere jedes Segment
  int segmentIndex = 0;
  
  // Horizontale Segmente (oben und unten)
  for (int i = 0; i < horizontalDivisions; i++) {
    // Obere Kante
    int x1 = tvCorners[0].x + (tvCorners[1].x - tvCorners[0].x) * i / horizontalDivisions;
    int y1 = tvCorners[0].y + (tvCorners[1].y - tvCorners[0].y) * i / horizontalDivisions;
    int x2 = tvCorners[0].x + (tvCorners[1].x - tvCorners[0].x) * (i + 1) / horizontalDivisions;
    int y2 = tvCorners[0].y + (tvCorners[1].y - tvCorners[0].y) * (i + 1) / horizontalDivisions;
    
    // Tiefe basiert auf vertikaler Teilung
    int depth = (tvCorners[3].y - tvCorners[0].y) / verticalDivisions;
    
    analyzeSegment(rgb_buffer, fb->width, fb->height, x1, y1, x2, y1 + depth, &colorSegments[segmentIndex]);
    
    // Speichere Segment für Visualisierung
    visualSegments[segmentIndex].x1 = x1;
    visualSegments[segmentIndex].y1 = y1;
    visualSegments[segmentIndex].x2 = x2;
    visualSegments[segmentIndex].y2 = y1 + depth;
    visualSegments[segmentIndex].color = colorSegments[segmentIndex];
    segmentIndex++;
    
    // Untere Kante
    x1 = tvCorners[3].x + (tvCorners[2].x - tvCorners[3].x) * i / horizontalDivisions;
    y1 = tvCorners[3].y + (tvCorners[2].y - tvCorners[3].y) * i / horizontalDivisions;
    x2 = tvCorners[3].x + (tvCorners[2].x - tvCorners[3].x) * (i + 1) / horizontalDivisions;
    y2 = tvCorners[3].y + (tvCorners[2].y - tvCorners[3].y) * (i + 1) / horizontalDivisions;
    
    analyzeSegment(rgb_buffer, fb->width, fb->height, x1, y1 - depth, x2, y1, &colorSegments[segmentIndex]);
    
    // Speichere Segment für Visualisierung
    visualSegments[segmentIndex].x1 = x1;
    visualSegments[segmentIndex].y1 = y1 - depth;
    visualSegments[segmentIndex].x2 = x2;
    visualSegments[segmentIndex].y2 = y1;
    visualSegments[segmentIndex].color = colorSegments[segmentIndex];
    segmentIndex++;
  }
  
  // Vertikale Segmente (links und rechts)
  for (int i = 0; i < verticalDivisions; i++) {
    // Linke Kante
    int x1 = tvCorners[0].x + (tvCorners[3].x - tvCorners[0].x) * i / verticalDivisions;
    int y1 = tvCorners[0].y + (tvCorners[3].y - tvCorners[0].y) * i / verticalDivisions;
    int x2 = tvCorners[0].x + (tvCorners[3].x - tvCorners[0].x) * (i + 1) / verticalDivisions;
    int y2 = tvCorners[0].y + (tvCorners[3].y - tvCorners[0].y) * (i + 1) / verticalDivisions;
    
    // Tiefe basiert auf horizontaler Teilung
    int depth = (tvCorners[1].x - tvCorners[0].x) / horizontalDivisions;
    
    analyzeSegment(rgb_buffer, fb->width, fb->height, x1, y1, x1 + depth, y2, &colorSegments[segmentIndex]);
    
    // Speichere Segment für Visualisierung
    visualSegments[segmentIndex].x1 = x1;
    visualSegments[segmentIndex].y1 = y1;
    visualSegments[segmentIndex].x2 = x1 + depth;
    visualSegments[segmentIndex].y2 = y2;
    visualSegments[segmentIndex].color = colorSegments[segmentIndex];
    segmentIndex++;
    
    // Rechte Kante
    x1 = tvCorners[1].x + (tvCorners[2].x - tvCorners[1].x) * i / verticalDivisions;
    y1 = tvCorners[1].y + (tvCorners[2].y - tvCorners[1].y) * i / verticalDivisions;
    x2 = tvCorners[1].x + (tvCorners[2].x - tvCorners[1].x) * (i + 1) / verticalDivisions;
    y2 = tvCorners[1].y + (tvCorners[2].y - tvCorners[1].y) * (i + 1) / verticalDivisions;
    
    analyzeSegment(rgb_buffer, fb->width, fb->height, x1 - depth, y1, x1, y2, &colorSegments[segmentIndex]);
    
    // Speichere Segment für Visualisierung
    visualSegments[segmentIndex].x1 = x1 - depth;
    visualSegments[segmentIndex].y1 = y1;
    visualSegments[segmentIndex].x2 = x1;
    visualSegments[segmentIndex].y2 = y2;
    visualSegments[segmentIndex].color = colorSegments[segmentIndex];
    segmentIndex++;
  }
  
  // Visualisierung wird jetzt im Stream-Handler gezeichnet
  
  if (fb->format == PIXFORMAT_JPEG && rgb_buffer != fb->buf) {
    free(rgb_buffer);
  }
  
  esp_camera_fb_return(fb);
}

void analyzeSegment(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, ColorData* colorData) {
  uint32_t totalR2 = 0, totalG2 = 0, totalB2 = 0;
  int pixelCount = 0;
  
  // Durchlaufe alle Pixel im Segment
  for (int y = min(y1, y2); y <= max(y1, y2); y++) {
    for (int x = min(x1, x2); x <= max(x1, x2); x++) {
      if (x >= 0 && x < width && y >= 0 && y < height) {
        int pixelIndex = (y * width + x) * 2;
        uint16_t pixel = (buffer[pixelIndex + 1] << 8) | buffer[pixelIndex];
        
        // RGB565 zu RGB888 konvertieren
        uint8_t r = ((pixel >> 11) & 0x1F) << 3;
        uint8_t g = ((pixel >> 5) & 0x3F) << 2;
        uint8_t b = (pixel & 0x1F) << 3;
        
        // RMS-Berechnung: Quadriere die Werte
        totalR2 += r * r;
        totalG2 += g * g;
        totalB2 += b * b;
        pixelCount++;
      }
    }
  }
  
  if (pixelCount > 0) {
    // RMS: Wurzel aus dem Durchschnitt der Quadrate
    colorData->r = sqrt(totalR2 / pixelCount);
    colorData->g = sqrt(totalG2 / pixelCount);
    colorData->b = sqrt(totalB2 / pixelCount);
    
    // Helligkeit berechnen (Luminance)
    colorData->brightness = (colorData->r * 299 + colorData->g * 587 + colorData->b * 114) / 1000;
  } else {
    colorData->r = colorData->g = colorData->b = colorData->brightness = 0;
  }
}

void drawQuadrilateral(uint8_t* buffer, int width, int height) {
  // Prüfe ob alle Punkte gesetzt sind
  bool allSet = true;
  for (int i = 0; i < 4; i++) {
    if (!tvCorners[i].set) {
      allSet = false;
      if (DEBUG_SERIAL) Serial.printf("Punkt %d nicht gesetzt\n", i);
      break;
    }
  }
  
  if (!allSet) {
    if (DEBUG_SERIAL) Serial.println("Nicht alle Punkte gesetzt, zeichne kein Viereck");
    return;
  }
  
  if (DEBUG_SERIAL) {
    Serial.println("Zeichne Viereck zwischen Punkten:");
    for (int i = 0; i < 4; i++) {
      Serial.printf("Punkt %d: (%d, %d)\n", i, tvCorners[i].x, tvCorners[i].y);
    }
  }
  
  // Zeichne Linien zwischen den 4 Punkten (Viereck)
  if (DEBUG_SERIAL) {
    Serial.println("Zeichne Linien:");
    Serial.printf("Linie 1: (%d,%d) -> (%d,%d)\n", tvCorners[0].x, tvCorners[0].y, tvCorners[1].x, tvCorners[1].y);
    Serial.printf("Linie 2: (%d,%d) -> (%d,%d)\n", tvCorners[1].x, tvCorners[1].y, tvCorners[2].x, tvCorners[2].y);
    Serial.printf("Linie 3: (%d,%d) -> (%d,%d)\n", tvCorners[2].x, tvCorners[2].y, tvCorners[3].x, tvCorners[3].y);
    Serial.printf("Linie 4: (%d,%d) -> (%d,%d)\n", tvCorners[3].x, tvCorners[3].y, tvCorners[0].x, tvCorners[0].y);
  }
  
  drawLine(buffer, width, height, tvCorners[0].x, tvCorners[0].y, tvCorners[1].x, tvCorners[1].y, 0, 255, 0); // Oben
  drawLine(buffer, width, height, tvCorners[1].x, tvCorners[1].y, tvCorners[2].x, tvCorners[2].y, 0, 255, 0); // Rechts
  drawLine(buffer, width, height, tvCorners[2].x, tvCorners[2].y, tvCorners[3].x, tvCorners[3].y, 0, 255, 0); // Unten
  drawLine(buffer, width, height, tvCorners[3].x, tvCorners[3].y, tvCorners[0].x, tvCorners[0].y, 0, 255, 0); // Links
  
  if (DEBUG_SERIAL) Serial.println("✅ Viereck gezeichnet");
}

void drawSegmentsOnImage(uint8_t* buffer, int width, int height) {
  for (int i = 0; i < totalSegments; i++) {
    // Zeichne Segment als gefülltes Rechteck in der analysierten Farbe
    drawRectangle(buffer, width, height, 
                 visualSegments[i].x1, visualSegments[i].y1, 
                 visualSegments[i].x2, visualSegments[i].y2,
                 visualSegments[i].color.r, 
                 visualSegments[i].color.g, 
                 visualSegments[i].color.b, 
                 true);
  }
}

void drawLine(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b) {
  // Bresenham-Linienalgorithmus mit dickerer Linie
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int err = dx - dy;
  
  int x = x1, y = y1;
  
  while (true) {
    // Zeichne 3x3 Pixel für dickere Linie
    for (int dy_offset = -1; dy_offset <= 1; dy_offset++) {
      for (int dx_offset = -1; dx_offset <= 1; dx_offset++) {
        int draw_x = x + dx_offset;
        int draw_y = y + dy_offset;
        
        if (draw_x >= 0 && draw_x < width && draw_y >= 0 && draw_y < height) {
          int pixelIndex = (draw_y * width + draw_x) * 2;
          // RGB888 zu RGB565 konvertieren
          uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
          buffer[pixelIndex] = pixel & 0xFF;
          buffer[pixelIndex + 1] = (pixel >> 8) & 0xFF;
        }
      }
    }
    
    if (x == x2 && y == y2) break;
    
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x += sx;
    }
    if (e2 < dx) {
      err += dx;
      y += sy;
    }
  }
}

void drawRectangle(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, bool filled) {
  int minX = min(x1, x2);
  int maxX = max(x1, x2);
  int minY = min(y1, y2);
  int maxY = max(y1, y2);
  
  // RGB888 zu RGB565 konvertieren
  uint16_t pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  uint8_t pixelLow = pixel & 0xFF;
  uint8_t pixelHigh = (pixel >> 8) & 0xFF;
  
  for (int y = minY; y <= maxY; y++) {
    for (int x = minX; x <= maxX; x++) {
      if (x >= 0 && x < width && y >= 0 && y < height) {
        int pixelIndex = (y * width + x) * 2;
        buffer[pixelIndex] = pixelLow;
        buffer[pixelIndex + 1] = pixelHigh;
      }
    }
  }
}

void sendColorData() {
  if (totalSegments == 0) return;
  
  DynamicJsonDocument doc(UDP_BUFFER_SIZE);
  doc["segments"] = totalSegments;
  
  JsonArray colors = doc.createNestedArray("colors");
  for (int i = 0; i < totalSegments; i++) {
    JsonObject color = colors.createNestedObject();
    color["r"] = colorSegments[i].r;
    color["g"] = colorSegments[i].g;
    color["b"] = colorSegments[i].b;
    color["brightness"] = colorSegments[i].brightness;
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  udp.beginPacket(LEUCHTER_IP, LEUCHTER_PORT);
  udp.write((uint8_t*)jsonString.c_str(), jsonString.length());
  udp.endPacket();
  
  if (DEBUG_FPS) {
    static unsigned long lastSend = 0;
    static int frameCount = 0;
    frameCount++;
    
    if (millis() - lastSend >= 1000) {
      if (DEBUG_SERIAL) Serial.printf("UDP-FPS: %d\n", frameCount);
      frameCount = 0;
      lastSend = millis();
    }
  }
}
