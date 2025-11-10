/*
 * 4-digit 7-segment display test script for Arduino Mega 2560.
 * Segment pins: a-g = pins 2-8, decimal point = pin 13
 * Digit select pins: digits 1-4 = pins 9-12
 */

#include <Arduino.h>

constexpr uint8_t kSegmentPins[] = {2, 3, 4, 5,
                                    6, 7, 8, 9}; // a, b, c, d, e, f, g, dp
constexpr size_t kSegmentCount = sizeof(kSegmentPins) / sizeof(kSegmentPins[0]);
constexpr uint8_t kDigitPins[] = {10, 11, 12, 13}; // digit 1..4
constexpr bool kCommonAnode = false; // flip to true if display is common anode
constexpr uint16_t kFrameDelayMicros = 1200; // refresh time per digit
constexpr uint16_t kHoldMillis = 1500;       // time to hold each test pattern

enum Segment : uint8_t {
  SEG_A = 0b0000001,
  SEG_B = 0b0000010,
  SEG_C = 0b0000100,
  SEG_D = 0b0001000,
  SEG_E = 0b0010000,
  SEG_F = 0b0100000,
  SEG_G = 0b1000000,
  SEG_DP = 0b10000000,
};

struct Glyph {
  char symbol;
  uint8_t mask;
};

constexpr Glyph kGlyphs[] = {
    {'0', SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F},
    {'1', SEG_B | SEG_C},
    {'2', SEG_A | SEG_B | SEG_D | SEG_E | SEG_G},
    {'3', SEG_A | SEG_B | SEG_C | SEG_D | SEG_G},
    {'4', SEG_B | SEG_C | SEG_F | SEG_G},
    {'5', SEG_A | SEG_C | SEG_D | SEG_F | SEG_G},
    {'6', SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'7', SEG_A | SEG_B | SEG_C},
    {'8', SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'9', SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G},
    {'A', SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G},
    {'b', SEG_C | SEG_D | SEG_E | SEG_F | SEG_G},
    {'C', SEG_A | SEG_D | SEG_E | SEG_F},
    {'d', SEG_B | SEG_C | SEG_D | SEG_E | SEG_G},
    {'E', SEG_A | SEG_D | SEG_E | SEG_F | SEG_G},
    {'F', SEG_A | SEG_E | SEG_F | SEG_G},
    {'-', SEG_G},
    {'.', SEG_DP},
    {' ', 0},
};

const char *kTestPatterns[] = {
    "0123", "4567", "89Ab", "CdEF", "----", "....", "    ",
};

uint8_t glyphFor(char symbol) {
  for (const auto &glyph : kGlyphs) {
    if (glyph.symbol == symbol) {
      return glyph.mask;
    }
  }
  return 0;
}

constexpr uint8_t segmentOnLevel() { return kCommonAnode ? LOW : HIGH; }

constexpr uint8_t segmentOffLevel() { return kCommonAnode ? HIGH : LOW; }

constexpr uint8_t digitEnableLevel() { return kCommonAnode ? HIGH : LOW; }

constexpr uint8_t digitDisableLevel() { return kCommonAnode ? LOW : HIGH; }

void writeSegments(uint8_t mask) {
  for (size_t i = 0; i < kSegmentCount; ++i) {
    const bool segmentOn = mask & (1 << i);
    digitalWrite(kSegmentPins[i],
                 segmentOn ? segmentOnLevel() : segmentOffLevel());
  }
}

void enableDigit(size_t index, bool enable) {
  digitalWrite(kDigitPins[index],
               enable ? digitEnableLevel() : digitDisableLevel());
}

void displayFrame(const char *text, uint32_t durationMillis) {
  const uint32_t start = millis();
  do {
    for (size_t digit = 0; digit < 4; ++digit) {
      enableDigit(digit, false);
      writeSegments(glyphFor(text[digit]));
      enableDigit(digit, true);
      delayMicroseconds(kFrameDelayMicros);
      enableDigit(digit, false);
    }
  } while (millis() - start < durationMillis);
}

void sweepSegments() {
  for (size_t seg = 0; seg < kSegmentCount; ++seg) {
    const uint8_t mask = 1 << seg;
    const uint32_t start = millis();
    do {
      for (size_t digit = 0; digit < 4; ++digit) {
        enableDigit(digit, false);
        writeSegments(mask);
        enableDigit(digit, true);
        delayMicroseconds(kFrameDelayMicros);
        enableDigit(digit, false);
      }
    } while (millis() - start < 200);
  }
}

void setup() {
  for (const auto pin : kSegmentPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, segmentOffLevel());
  }
  for (const auto pin : kDigitPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, digitDisableLevel());
  }
}

void loop() {
  sweepSegments(); // confirm every segment lights

  for (const auto *pattern : kTestPatterns) {
    displayFrame(pattern, kHoldMillis);
  }
}
