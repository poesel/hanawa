#include "apa102.h"

// APA102 LED-Treiber Implementierung
// Referenz: https://cpldcpu.wordpress.com/2014/08/27/apa102/
// (taken from sucher2/leuchter, proven on this stripe)

void APA102::begin(int num_leds) {
    _numLeds = num_leds;
    _brightness = 31;  // Maximum (0-31)

    // RGB-Buffer allokieren
    _pixels = (uint8_t*)malloc(num_leds * 3);
    if (!_pixels) {
        Serial.println("[APA102] ERROR: Memory allocation failed");
        return;
    }

    // Buffer initialisieren (schwarz)
    memset(_pixels, 0, num_leds * 3);

    // SPI initialisieren
    // D1 mini: HSPI-Pins des Boards (SCK=GPIO14, MOSI=GPIO13) sind
    // SPI.begin()'s Defaults of the ESP8266 core
    SPI.begin();
    SPI.setFrequency(10000000); // 10 MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);

    Serial.print("[APA102] Initialised with ");
    Serial.print(num_leds);
    Serial.println(" LEDs");

    // Alle LEDs out
    clear();
    show();
}

void APA102::setPixel(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < 0 || index >= _numLeds) {
        return;  // Index outside
    }

    _pixels[index * 3 + 0] = r;
    _pixels[index * 3 + 1] = g;
    _pixels[index * 3 + 2] = b;
}

void APA102::setBrightness(uint8_t brightness) {
    // APA102 supports 0-31
    _brightness = constrain(brightness, 0, 31);
}

void APA102::show() {
    sendStartFrame();

    for (int i = 0; i < _numLeds; i++) {
        uint8_t r = _pixels[i * 3 + 0];
        uint8_t g = _pixels[i * 3 + 1];
        uint8_t b = _pixels[i * 3 + 2];
        sendLEDFrame(r, g, b);
    }

    sendEndFrame();
}

void APA102::clear() {
    memset(_pixels, 0, _numLeds * 3);
}

void APA102::sendStartFrame() {
    // Start Frame: 32 Bits (0x00)
    // ESP8266 hat kein SPI.write32(), herer 4x write(0x00)
    SPI.write(0x00);
    SPI.write(0x00);
    SPI.write(0x00);
    SPI.write(0x00);
}

void APA102::sendEndFrame() {
    // End Frame: (num_leds + 15) / 16 Bytes (0xFF)
    // Simplified: 4 Bytes (0xFF) suffice for up to 64 LEDs
    int endBytes = (_numLeds + 15) / 16;
    if (endBytes < 4) endBytes = 4;

    for (int i = 0; i < endBytes; i++) {
        SPI.write(0xFF);
    }
}

void APA102::sendLEDFrame(uint8_t r, uint8_t g, uint8_t b) {
    // LED Frame: 4 Bytes
    // Byte 0: 111BBBBB (3 Ones + 5 Bit Brightness)
    // Byte 1: Blue
    // Byte 2: Green
    // Byte 3: Red
    uint8_t brightness_bits = 0xE0 | _brightness;  // 0b111BBBBB

    SPI.write(brightness_bits);
    SPI.write(b);
    SPI.write(g);
    SPI.write(r);
}
