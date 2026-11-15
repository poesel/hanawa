#ifndef APA102_H
#define APA102_H

#include <Arduino.h>
#include <SPI.h>

// APA102 LED-Treiber (ESP8266 / Wemos D1 mini v2.1.0)
//
// The stripe is driven through the ESP8266's built-in HSPI unit:
// SCK/CLOCK = GPIO14, MOSI/DATA = GPIO13 (the core library's defaults).
// The stripe's two wires are connected so that the clock wire lands on GPIO14
// and the data wire on GPIO13 - measured with the GPIO probe (probe_pins/),
// which initially found them the other way round (clock=GPIO13, data=GPIO14);
// the stripe's wires were swapped to match the fixed HSPI pads.
//
// Referenz: https://cpldcpu.wordpress.com/2014/08/27/apa102/
// Driver taken from sucher2/leuchter (the one proven on this stripe).

class APA102 {
public:
    // Speicher for up to `capacity` LEDs; the length of the stripe itself
    // is irrelevant - show() only sends what setNumLeds() says.
    void begin(int capacity);

    // How many LEDs the next show() addresses (clamped to the capacity).
    // Set this to the number of RGB triplets of the received frame.
    void setNumLeds(int num_leds);

    // Setze Farbe für eine LED (Index 0-basiert)
    void setPixel(int index, uint8_t r, uint8_t g, uint8_t b);

    // Setze globale Helligkeit (0-31)
    void setBrightness(uint8_t brightness);

    // Sende alle LED-Daten via SPI
    void show();

    // Schalte alle LEDs aus
    void clear();

    // Hole Anzahl LEDs (of the last show())
    int getNumLeds() { return _numLeds; }

    // Hole Anzahl LEDs the buffer was allocated for
    int getCapacity() { return _capacity; }

private:
    int _capacity;    // pixels allocated in _pixels
    int _numLeds;     // LEDs sent by the next show()
    uint8_t _brightness;
    uint8_t* _pixels;  // RGB-Buffer (3 Bytes pro LED)

    void sendStartFrame();
    void sendEndFrame();
    void sendLEDFrame(uint8_t r, uint8_t g, uint8_t b);
};

#endif // APA102_H
