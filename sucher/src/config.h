/*
 * Hanawa Sucher - Konfigurationsdatei
 * 
 * Hier können alle wichtigen Parameter angepasst werden.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Debug-Einstellungen
#define DEBUG_LEVEL 1  // 0=kein Debug, 1=minimal, 2=normal, 3=verbose
#define DEBUG_SERIAL (DEBUG_LEVEL > 0)
#define DEBUG_FPS (DEBUG_LEVEL > 1)
#define DEBUG_STREAM (DEBUG_LEVEL > 2)

// WLAN-Konfiguration
#define WIFI_SSID "Zippen 24"
#define WIFI_PASSWORD "Boyzoneanker24"

// Netzwerk-Konfiguration
#define SUCHER_IP "192.168.0.80"
#define LEUCHTER_IP "192.168.0.81"
#define LEUCHTER_PORT 8888
#define SUCHER_PORT 8889

// Kamera-Konfiguration
#define CAMERA_FRAME_SIZE FRAMESIZE_QQVGA  // 160x120
#define CAMERA_FORMAT PIXFORMAT_RGB565
#define CAMERA_JPEG_QUALITY 80
#define CAMERA_FB_COUNT 1

// Standard-Teilungen
#define DEFAULT_HORIZONTAL_DIVISIONS 20
#define DEFAULT_VERTICAL_DIVISIONS 12

// Performance-Einstellungen
#define ANALYSIS_FPS 10  // Frames pro Sekunde für Farbanalyse
#define UDP_BUFFER_SIZE 2048

#endif
