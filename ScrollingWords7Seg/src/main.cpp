#include <Arduino.h>
#include <ctype.h>
#include <string.h>

constexpr uint8_t kSegmentPins[7] = {2, 3, 4, 5, 6, 7, 8}; // a-g
constexpr uint8_t kDigitPins[8] = {9, 10, 11, 12, 22, 24, 26, 28};

// Adjust these to match your hardware (common anode: segments active low,
// digits active high).
constexpr bool kSegmentsActiveHigh = false;
constexpr bool kDigitsActiveHigh = true;

constexpr unsigned long kDigitRefreshIntervalMicros =
    1000; // ~1 ms per digit (~125 Hz overall)
constexpr unsigned long kScrollIntervalMillis = 250; // Scroll step every 250 ms

constexpr size_t kDisplayDigits = sizeof(kDigitPins) / sizeof(kDigitPins[0]);
constexpr size_t kPaddingSpaces =
    kDisplayDigits;                          // Leading and trailing blanks
constexpr size_t kMaxMessageLength = 64;     // Adjust if you need longer text
constexpr size_t kMaxPaddedLength =
    kMaxMessageLength + 2 * kPaddingSpaces;

constexpr char kDefaultMessage[] = "HELLO 7SEG";

uint8_t gDisplayBuffer[kDisplayDigits] = {};
char gMessage[kMaxMessageLength + 1] = {};
size_t gMessageLength = 0;
char gPaddedMessage[kMaxPaddedLength + 1] = {};
size_t gPaddedLength = 0;
size_t gScrollIndex = 0;
size_t gScrollLimit = 1;
size_t gCurrentDigit = kDisplayDigits - 1;
unsigned long gLastRefreshMicros = 0;
unsigned long gLastScrollMillis = 0;

char gSerialInputBuffer[kMaxMessageLength + 1] = {};
size_t gSerialInputLength = 0;
bool gIgnoreNextLinefeed = false;

constexpr uint8_t SEG_A = 1 << 0;
constexpr uint8_t SEG_B = 1 << 1;
constexpr uint8_t SEG_C = 1 << 2;
constexpr uint8_t SEG_D = 1 << 3;
constexpr uint8_t SEG_E = 1 << 4;
constexpr uint8_t SEG_F = 1 << 5;
constexpr uint8_t SEG_G = 1 << 6;

constexpr uint8_t kDigitPatterns[10] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,         // 0
    SEG_B | SEG_C,                                         // 1
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                 // 2
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                 // 3
    SEG_B | SEG_C | SEG_F | SEG_G,                         // 4
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                 // 5
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,         // 6
    SEG_A | SEG_B | SEG_C,                                 // 7
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G, // 8
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G          // 9
};

uint8_t encodeChar(char c) {
  if (c >= '0' && c <= '9') {
    return kDigitPatterns[c - '0'];
  }

  c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

  switch (c) {
  case 'A':
    return SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
  case 'B':
    return SEG_C | SEG_D | SEG_E | SEG_F | SEG_G;
  case 'C':
    return SEG_A | SEG_D | SEG_E | SEG_F;
  case 'D':
    return SEG_B | SEG_C | SEG_D | SEG_E | SEG_G;
  case 'E':
    return SEG_A | SEG_D | SEG_E | SEG_F | SEG_G;
  case 'F':
    return SEG_A | SEG_E | SEG_F | SEG_G;
  case 'G':
    return SEG_A | SEG_C | SEG_D | SEG_E | SEG_F;
  case 'H':
    return SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;
  case 'I':
    return SEG_B | SEG_C;
  case 'J':
    return SEG_B | SEG_C | SEG_D;
  case 'L':
    return SEG_D | SEG_E | SEG_F;
  case 'N':
    return SEG_C | SEG_E | SEG_G;
  case 'O':
    return SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
  case 'P':
    return SEG_A | SEG_B | SEG_E | SEG_F | SEG_G;
  case 'R':
    return SEG_A | SEG_B | SEG_E | SEG_F | SEG_G | SEG_C;
  case 'S':
    return SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;
  case 'T':
    return SEG_D | SEG_E | SEG_F | SEG_G;
  case 'U':
    return SEG_B | SEG_C | SEG_D | SEG_E | SEG_F;
  case 'Y':
    return SEG_B | SEG_C | SEG_D | SEG_F | SEG_G;
  case '-':
    return SEG_G;
  case '_':
    return SEG_D;
  case ' ':
  default:
    return 0;
  }
}

uint8_t segmentOnState(bool segmentEnabled) {
  return segmentEnabled ? (kSegmentsActiveHigh ? HIGH : LOW)
                        : (kSegmentsActiveHigh ? LOW : HIGH);
}

uint8_t digitState(bool enabled) {
  return enabled ? (kDigitsActiveHigh ? HIGH : LOW)
                 : (kDigitsActiveHigh ? LOW : HIGH);
}

void applySegments(uint8_t pattern) {
  for (size_t i = 0; i < 7; ++i) {
    const bool segmentEnabled = (pattern & (1 << i)) != 0;
    digitalWrite(kSegmentPins[i], segmentOnState(segmentEnabled));
  }
}

void disableAllDigits() {
  for (size_t i = 0; i < kDisplayDigits; ++i) {
    digitalWrite(kDigitPins[i], digitState(false));
  }
}

void refreshDisplay() {
  digitalWrite(kDigitPins[gCurrentDigit], digitState(false));

  gCurrentDigit = (gCurrentDigit + 1) % kDisplayDigits;
  applySegments(gDisplayBuffer[gCurrentDigit]);
  digitalWrite(kDigitPins[gCurrentDigit], digitState(true));
}

void updateScrollBuffer() {
  for (size_t digit = 0; digit < kDisplayDigits; ++digit) {
    const size_t charIndex = gScrollIndex + digit;
    const char c =
        (charIndex < gPaddedLength) ? gPaddedMessage[charIndex] : ' ';
    gDisplayBuffer[digit] = encodeChar(c);
  }
}

void buildPaddedMessage() {
  const size_t totalLength = gMessageLength + 2 * kPaddingSpaces;
  gPaddedLength =
      (totalLength <= kMaxPaddedLength) ? totalLength : kMaxPaddedLength;

  size_t writeIndex = 0;
  const size_t leadingSpaces =
      (kPaddingSpaces <= gPaddedLength) ? kPaddingSpaces : gPaddedLength;
  for (size_t i = 0; i < leadingSpaces; ++i) {
    gPaddedMessage[writeIndex++] = ' ';
  }

  const size_t remainingCapacity =
      (gPaddedLength > writeIndex) ? (gPaddedLength - writeIndex) : 0;
  size_t copyLength =
      (gMessageLength < remainingCapacity) ? gMessageLength : remainingCapacity;
  if (copyLength > 0) {
    memcpy(&gPaddedMessage[writeIndex], gMessage, copyLength);
    writeIndex += copyLength;
  }

  while (writeIndex < gPaddedLength) {
    gPaddedMessage[writeIndex++] = ' ';
  }

  gPaddedMessage[writeIndex] = '\0';

  gScrollLimit =
      (gPaddedLength >= kDisplayDigits) ? (gPaddedLength - kDisplayDigits) + 1
                                        : 1;
  if (gScrollLimit == 0) {
    gScrollLimit = 1;
  }
  if (gScrollIndex >= gScrollLimit) {
    gScrollIndex = 0;
  }
}

void advanceScroll() {
  if (gScrollLimit <= 1) {
    return;
  }

  gScrollIndex = (gScrollIndex + 1) % gScrollLimit;
  updateScrollBuffer();
}

void setMessage(const char *message, size_t length) {
  if (message == nullptr) {
    length = 0;
  }

  if (length > kMaxMessageLength) {
    length = kMaxMessageLength;
  }

  if (length > 0) {
    memcpy(gMessage, message, length);
  }
  gMessageLength = length;
  gMessage[gMessageLength] = '\0';

  gScrollIndex = 0;
  buildPaddedMessage();
  updateScrollBuffer();
  gLastScrollMillis = millis();
}

void commitSerialMessage() {
  gSerialInputBuffer[gSerialInputLength] = '\0';
  setMessage(gSerialInputBuffer, gSerialInputLength);
  gSerialInputLength = 0;

  Serial.print(F("Scrolling: "));
  Serial.println(gMessage);
}

void processSerialInput() {
  while (Serial.available() > 0) {
    const char incoming = Serial.read();

    if (incoming == '\r') {
      commitSerialMessage();
      gIgnoreNextLinefeed = true;
      continue;
    }

    if (incoming == '\n') {
      if (gIgnoreNextLinefeed) {
        gIgnoreNextLinefeed = false;
        continue;
      }
      commitSerialMessage();
      continue;
    }

    gIgnoreNextLinefeed = false;

    if (incoming == '\b' || incoming == 127) {
      if (gSerialInputLength > 0) {
        --gSerialInputLength;
      }
      continue;
    }

    if (isprint(static_cast<unsigned char>(incoming))) {
      if (gSerialInputLength < kMaxMessageLength) {
        gSerialInputBuffer[gSerialInputLength++] = incoming;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("Send text followed by ENTER to update the scroll."));

  for (uint8_t pin : kSegmentPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, segmentOnState(false));
  }

  for (uint8_t pin : kDigitPins) {
    pinMode(pin, OUTPUT);
  }
  disableAllDigits();

  setMessage(kDefaultMessage, strlen(kDefaultMessage));

  gCurrentDigit = kDisplayDigits - 1;
  gLastRefreshMicros = micros();
}

void loop() {
  processSerialInput();

  const unsigned long nowMicros = micros();
  if (nowMicros - gLastRefreshMicros >= kDigitRefreshIntervalMicros) {
    gLastRefreshMicros = nowMicros;
    refreshDisplay();
  }

  const unsigned long nowMillis = millis();
  if (nowMillis - gLastScrollMillis >= kScrollIntervalMillis) {
    gLastScrollMillis = nowMillis;
    advanceScroll();
  }
}
