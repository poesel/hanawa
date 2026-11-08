#ifndef ESPNOW_SENDER_H
#define ESPNOW_SENDER_H

#include <Arduino.h>
#include "windows.h"

// ESP-NOW Sender für Ambilight-Daten
// Implementiert das Ambilight-Protokoll gemäß doc/AMBILIGHT_PROTOCOL.md

// Initialisiert ESP-NOW
void espnow_sender_init();

// Sendet Ambilight-Daten via ESP-NOW
// Packt die Daten gemäß Protokoll in Pakete und sendet sie
void espnow_send_ambilight(const AmbilightResult& result, int h_seg, int v_seg);

// Gibt Statistiken zurück
uint32_t espnow_get_sent_frames();
uint32_t espnow_get_failed_frames();

#endif // ESPNOW_SENDER_H
