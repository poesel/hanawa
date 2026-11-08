/*
 * Hanawa Leuchter - GPIO-Probe for the LED stripe (Wemos D1 mini v2.1.0)
 *
 * The core SPI library always uses the ESP8266's built-in HSPI unit (fixed
 * physical pads GPIO14=SCK, GPIO13=MOSI). If the stripe is plugged into other
 * pads (the v2-generation boards may label/mount them differently), those
 * writes never reach the stripe. This probe bit-bangs a native APA102 frame
 * to EVERY candidate GPIO pair, so the stripe itself tells us where it sits.
 *
 * Candidate k is shown as: the first <(k % 40) + 1> LEDs in colour
 * <COLOURS[k % 6]>. Because 6 and 40 are coprime, (colour, lit-count)
 * identifies k uniquely among the 110 candidates tried here.
 *
 * No black frames are written, so after one full sweep the stripe shows the
 * pattern of the LAST candidate that actually reached it = the correct pair.
 * Read that off (colour + number of lit LEDs) and report it.
 */

#include <Arduino.h>

/* the stripe's own GPIO candidates (the pads the D1 mini exposes) */
static const uint8_t CAND[] = {13, 14, 12, 16, 15, 5, 4, 2, 0, 1, 3};
static const int NCAND = sizeof(CAND) / sizeof(CAND[0]);

// Physical stripe length. The probe only ever lights 1..40 LEDs
// (lit = (k % 40) + 1), so a longer stripe still shows the same pattern.
#define LEDS 40
#define HOLD_MS 1100
#define FINAL_HOLD_MS 45000

static const uint8_t COLS[6][3] = {
    {255, 0, 0},     // 0 red
    {0, 255, 0},     // 1 green
    {0, 0, 255},     // 2 blue
    {255, 255, 0},   // 3 yellow
    {255, 0, 255},   // 4 magenta
    {0, 255, 255},   // 5 cyan
};

/* ---------------- bit-level SPI (manual, works on any GPIO pair) ---------- */
static void setPin(uint8_t pin, uint8_t val) { digitalWrite(pin, val); }

/* one bit: MOSI holds the bit, SCK does one clock cycle (low -> high -> low)
 * SPI_MODE0-style: change bit while SCK low, clock it with a high pulse */
static void bitOf(uint8_t mosi, uint8_t sck, uint8_t bit) {
  setPin(sck, LOW);
  setPin(mosi, bit);
  delayMicroseconds(3);
  setPin(sck, HIGH);
  delayMicroseconds(3);
  setPin(sck, LOW);
  delayMicroseconds(3);
}

static void sendBits(uint8_t mosi, uint8_t sck, uint8_t value, int count) {
  for (int i = count - 1; i >= 0; i--) {
    bitOf(mosi, sck, (value >> i) & 1);
  }
}

static void showFrame(uint8_t mosi, uint8_t sck, int lit, uint8_t r, uint8_t g,
                      uint8_t b) {
  for (int i = 0; i < 32; i++) bitOf(mosi, sck, 0);  // 32-bit start frame
  for (int i = 0; i < LEDS; i++) {
    if (i < lit) {
      sendBits(mosi, sck, 0xE0 | 31, 8);  // 111 + 5-bit brightness
      sendBits(mosi, sck, b, 8);
      sendBits(mosi, sck, g, 8);
      sendBits(mosi, sck, r, 8);
    } else {
      sendBits(mosi, sck, 0xE0 | 31, 8);
      sendBits(mosi, sck, 0, 8);
      sendBits(mosi, sck, 0, 8);
      sendBits(mosi, sck, 0, 8);
    }
  }
  for (int i = 0; i < 4; i++) sendBits(mosi, sck, 0xFF, 8);  // end frame
}

/* ------------------------------- sweep ---------------------------------- */
static int tryPair(uint8_t mosi, uint8_t sck, int k) {
  pinMode(mosi, OUTPUT);
  pinMode(sck, OUTPUT);
  int lit = (k % LEDS) + 1;
  showFrame(mosi, sck, lit, COLS[k % 6][0], COLS[k % 6][1], COLS[k % 6][2]);
  delay(HOLD_MS);
  pinMode(mosi, INPUT);
  pinMode(sck, INPUT);
  return lit;
}

void setup() {
  for (int i = 0; i < NCAND; i++) pinMode(CAND[i], INPUT);
  delay(300);
}

void loop() {
  int lastLit = 0;
  int k = 0;
  for (int a = 0; a < NCAND; a++) {
    for (int b = 0; b < NCAND; b++) {
      if (a == b) continue;
      lastLit = tryPair(CAND[a], CAND[b], k++);
    }
  }
  /* hold the surviving pattern long enough to count it comfortably */
  delay(FINAL_HOLD_MS);
  (void)lastLit;
}
