/*
 * Hanawa Sucher - Konfigurationsdatei
 * 
 * Hier können alle wichtigen Parameter angepasst werden.
 */

#ifndef CONFIG_H
#define CONFIG_H

// WLAN-Konfiguration
#define WIFI_SSID "Zippen 24"
#define WIFI_PASSWORD "boyzoneanker24"

// Netzwerk-Konfiguration
#define SUCHER_IP "192.168.0.80"
#define LEUCHTER_IP "192.168.0.81"
#define LEUCHTER_PORT 8888
#define SUCHER_PORT 8889

// Kamera-Konfiguration
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA  // 640x480
#define CAMERA_FORMAT PIXFORMAT_RGB565
#define CAMERA_JPEG_QUALITY 12
#define CAMERA_FB_COUNT 1

// Standard-Teilungen
#define DEFAULT_HORIZONTAL_DIVISIONS 20
#define DEFAULT_VERTICAL_DIVISIONS 12

// Performance-Einstellungen
#define ANALYSIS_FPS 10  // Frames pro Sekunde für Farbanalyse
#define UDP_BUFFER_SIZE 2048

// Debug-Einstellungen
#define DEBUG_SERIAL true
#define DEBUG_FPS false

#endif
