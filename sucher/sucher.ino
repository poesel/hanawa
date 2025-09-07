/*
 * Hanawa Sucher - ESP32CAM Farbanalyse für Ambient Lighting
 * 
 * Dieses Programm analysiert die Farben eines Fernsehers über die ESP32CAM
 * und sendet die Farbdaten an den Leuchter.
 */

#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include "WiFi.h"
#include "WebServer.h"
#include "ArduinoJson.h"
#include "WiFiUdp.h"
#include "config.h"
#include "webpage.h"

// WLAN-Konfiguration aus config.h
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* sucherIP = SUCHER_IP;
const char* leuchterIP = LEUCHTER_IP;
const int leuchterPort = LEUCHTER_PORT;

// ESP32CAM Pin-Konfiguration
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    22
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     4

// Globale Variablen
WiFiUDP udp;
WebServer server(80);
camera_fb_t * fb = NULL;

// Fernseher-Kalibrierung
struct Point {
  int x, y;
  bool set = false;
};

Point tvCorners[4]; // Punkt 1-4 im Uhrzeigersinn
int horizontalDivisions = DEFAULT_HORIZONTAL_DIVISIONS;
int verticalDivisions = DEFAULT_VERTICAL_DIVISIONS;
bool calibrationMode = true;

// Farbdaten für Leuchter
struct ColorData {
  uint8_t r, g, b;
  uint8_t brightness;
};

ColorData* colorSegments = nullptr;
int totalSegments = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Hanawa Sucher startet...");
  
  // ESP32 Stabilisierung
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Kamera-Konfiguration
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = CAMERA_FORMAT;
  config.frame_size = CAMERA_FRAME_SIZE;
  config.jpeg_quality = CAMERA_JPEG_QUALITY;
  config.fb_count = CAMERA_FB_COUNT;
  
  // Kamera initialisieren
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Kamera-Initialisierung fehlgeschlagen: 0x%x", err);
    return;
  }
  
  // WLAN verbinden
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WLAN verbunden");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
  
  // Statische IP setzen
  IPAddress localIP;
  localIP.fromString(sucherIP);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(localIP, gateway, subnet);
  
  // Web-Server Routen
  setupWebServer();
  
  // UDP initialisieren
  udp.begin(SUCHER_PORT);
  
  Serial.println("Sucher bereit!");
}

void loop() {
  server.handleClient();
  
  if (!calibrationMode && totalSegments > 0) {
    analyzeColors();
    sendColorData();
    delay(1000 / ANALYSIS_FPS); // Konfigurierbare FPS
  }
}

void setupWebServer() {
  // Hauptseite
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", MAIN_page);
  });
  
  // Livebild
  server.on("/stream", HTTP_GET, []() {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      server.send(500, "text/plain", "Kamera-Fehler");
      return;
    }
    
    server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
  });
  
  // Kalibrierung
  server.on("/calibrate", HTTP_POST, []() {
    String data = server.arg("plain");
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data);
    
    int pointIndex = doc["point"];
    int x = doc["x"];
    int y = doc["y"];
    
    if (pointIndex >= 0 && pointIndex < 4) {
      tvCorners[pointIndex].x = x;
      tvCorners[pointIndex].y = y;
      tvCorners[pointIndex].set = true;
      
      // Prüfen ob alle Punkte gesetzt sind
      bool allSet = true;
      for (int i = 0; i < 4; i++) {
        if (!tvCorners[i].set) allSet = false;
      }
      
      if (allSet) {
        calibrationMode = false;
        calculateSegments();
      }
    }
    
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });
  
  // Parameter setzen
  server.on("/setParams", HTTP_POST, []() {
    String data = server.arg("plain");
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data);
    
    horizontalDivisions = doc["horizontal"];
    verticalDivisions = doc["vertical"];
    
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
    
    for (int i = 0; i < 4; i++) {
      doc["corners"][i]["x"] = tvCorners[i].x;
      doc["corners"][i]["y"] = tvCorners[i].y;
      doc["corners"][i]["set"] = tvCorners[i].set;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });
  
  server.begin();
}



void calculateSegments() {
  // Berechne Gesamtanzahl der Segmente
  totalSegments = (horizontalDivisions * 2) + (verticalDivisions * 2);
  
  // Speicher für Farbdaten allozieren
  if (colorSegments != nullptr) {
    delete[] colorSegments;
  }
  colorSegments = new ColorData[totalSegments];
  
  Serial.printf("Segmente berechnet: %d\n", totalSegments);
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
  
  // Analysiere jedes Segment
  int segmentIndex = 0;
  
  // Horizontale Segmente (oben und unten)
  for (int i = 0; i < horizontalDivisions; i++) {
    // Obere Kante
    analyzeSegment(rgb_buffer, fb->width, fb->height, 
                  tvCorners[0].x + (tvCorners[1].x - tvCorners[0].x) * i / horizontalDivisions,
                  tvCorners[0].y + (tvCorners[1].y - tvCorners[0].y) * i / horizontalDivisions,
                  tvCorners[0].x + (tvCorners[1].x - tvCorners[0].x) * (i + 1) / horizontalDivisions,
                  tvCorners[0].y + (tvCorners[1].y - tvCorners[0].y) * (i + 1) / horizontalDivisions,
                  &colorSegments[segmentIndex++]);
    
    // Untere Kante
    analyzeSegment(rgb_buffer, fb->width, fb->height,
                  tvCorners[3].x + (tvCorners[2].x - tvCorners[3].x) * i / horizontalDivisions,
                  tvCorners[3].y + (tvCorners[2].y - tvCorners[3].y) * i / horizontalDivisions,
                  tvCorners[3].x + (tvCorners[2].x - tvCorners[3].x) * (i + 1) / horizontalDivisions,
                  tvCorners[3].y + (tvCorners[2].y - tvCorners[3].y) * (i + 1) / horizontalDivisions,
                  &colorSegments[segmentIndex++]);
  }
  
  // Vertikale Segmente (links und rechts)
  for (int i = 0; i < verticalDivisions; i++) {
    // Linke Kante
    analyzeSegment(rgb_buffer, fb->width, fb->height,
                  tvCorners[0].x + (tvCorners[3].x - tvCorners[0].x) * i / verticalDivisions,
                  tvCorners[0].y + (tvCorners[3].y - tvCorners[0].y) * i / verticalDivisions,
                  tvCorners[0].x + (tvCorners[3].x - tvCorners[0].x) * (i + 1) / verticalDivisions,
                  tvCorners[0].y + (tvCorners[3].y - tvCorners[0].y) * (i + 1) / verticalDivisions,
                  &colorSegments[segmentIndex++]);
    
    // Rechte Kante
    analyzeSegment(rgb_buffer, fb->width, fb->height,
                  tvCorners[1].x + (tvCorners[2].x - tvCorners[1].x) * i / verticalDivisions,
                  tvCorners[1].y + (tvCorners[2].y - tvCorners[1].y) * i / verticalDivisions,
                  tvCorners[1].x + (tvCorners[2].x - tvCorners[1].x) * (i + 1) / verticalDivisions,
                  tvCorners[1].y + (tvCorners[2].y - tvCorners[1].y) * (i + 1) / verticalDivisions,
                  &colorSegments[segmentIndex++]);
  }
  
  if (fb->format == PIXFORMAT_JPEG && rgb_buffer != fb->buf) {
    free(rgb_buffer);
  }
  
  esp_camera_fb_return(fb);
}

void analyzeSegment(uint8_t* buffer, int width, int height, int x1, int y1, int x2, int y2, ColorData* colorData) {
  uint32_t totalR = 0, totalG = 0, totalB = 0;
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
        
        totalR += r;
        totalG += g;
        totalB += b;
        pixelCount++;
      }
    }
  }
  
  if (pixelCount > 0) {
    colorData->r = totalR / pixelCount;
    colorData->g = totalG / pixelCount;
    colorData->b = totalB / pixelCount;
    
    // Helligkeit berechnen (Luminance)
    colorData->brightness = (colorData->r * 299 + colorData->g * 587 + colorData->b * 114) / 1000;
  } else {
    colorData->r = colorData->g = colorData->b = colorData->brightness = 0;
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
  
  udp.beginPacket(leuchterIP, leuchterPort);
  udp.write((uint8_t*)jsonString.c_str(), jsonString.length());
  udp.endPacket();
}
