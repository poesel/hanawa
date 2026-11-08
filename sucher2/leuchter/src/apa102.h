#ifndef APA102_H
#define APA102_H

#include <Arduino.h>
#include <SPI.h>

// APA102 LED-Treiber für ESP32
// Hardware: DATA=GPIO23 (MOSI), CLOCK=GPIO18 (SCK)

class APA102 {
public:
    // Initialisiere APA102 mit Anzahl LEDs
    void begin(int num_leds);
    
    // Setze Farbe für eine LED (Index 0-basiert)
    void setPixel(int index, uint8_t r, uint8_t g, uint8_t b);
    
    // Setze globale Helligkeit (0-31)
    void setBrightness(uint8_t brightness);
    
    // Sende alle LED-Daten via SPI
    void show();
    
    // Schalte alle LEDs aus
    void clear();
    
    // Hole Anzahl LEDs
    int getNumLeds() { return _numLeds; }

private:
    int _numLeds;
    uint8_t _brightness;
    uint8_t* _pixels;  // RGB-Buffer (3 Bytes pro LED)
    
    void sendStartFrame();
    void sendEndFrame();
    void sendLEDFrame(uint8_t r, uint8_t g, uint8_t b);
};

#endif // APA102_H
