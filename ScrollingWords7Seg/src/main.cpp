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
int gScrollDirection = 1; // 1: right-to-left, -1: left-to-right
enum class PingPongState : uint8_t { None, AwaitingBounce };

PingPongState gPingPongState = PingPongState::None;
constexpr char kPingCommand[] = "PING";
constexpr char kPongResponse[] = "PONG";
constexpr size_t kPingCommandLength = sizeof(kPingCommand) - 1;
constexpr size_t kPongResponseLength = sizeof(kPongResponse) - 1;

void setMessage(const char *message, size_t length);

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

bool windowHasVisibleChars(size_t index) {
  if (index >= gPaddedLength) {
    return false;
  }

  for (size_t digit = 0; digit < kDisplayDigits; ++digit) {
    const size_t charIndex = index + digit;
    if (charIndex >= gPaddedLength) {
      break;
    }
    if (gPaddedMessage[charIndex] != ' ') {
      return true;
    }
  }

  return false;
}

bool handlePingPongBounce() {
  if (gPingPongState != PingPongState::AwaitingBounce) {
    return false;
  }

  if (gScrollLimit <= 1) {
    gPingPongState = PingPongState::None;
    return false;
  }

  if (!windowHasVisibleChars(gScrollIndex)) {
    return false;
  }

  bool nextWindowHasChars = false;
  if (gScrollDirection >= 0) {
    if ((gScrollIndex + 1) < gScrollLimit) {
      nextWindowHasChars = windowHasVisibleChars(gScrollIndex + 1);
    }
  } else {
    if (gScrollIndex > 0) {
      nextWindowHasChars = windowHasVisibleChars(gScrollIndex - 1);
    }
  }

  if (nextWindowHasChars) {
    return false;
  }

  gScrollDirection = (gScrollDirection >= 0) ? -1 : 1;
  gPingPongState = PingPongState::None;

  setMessage(kPongResponse, kPongResponseLength);

  if (gScrollLimit > 0) {
    if (gScrollDirection >= 0) {
      const size_t maxIndex = gScrollLimit - 1;
      gScrollIndex = (maxIndex >= 1) ? 1 : maxIndex;
    } else {
      gScrollIndex = (gScrollLimit > 2) ? (gScrollLimit - 2) : 0;
    }
    updateScrollBuffer();
  }

  return true;
}

void advanceScroll() {
  if (gScrollLimit <= 1) {
    return;
  }

  if (handlePingPongBounce()) {
    return;
  }

  if (gScrollDirection >= 0) {
    gScrollIndex = (gScrollIndex + 1) % gScrollLimit;
  } else {
    if (gScrollIndex == 0) {
      gScrollIndex = gScrollLimit - 1;
    } else {
      --gScrollIndex;
    }
  }
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
  if (gScrollDirection < 0 && gScrollLimit > 0) {
    gScrollIndex = gScrollLimit - 1;
  }
  updateScrollBuffer();
  gLastScrollMillis = millis();
}

void updateScrollDirectionFromMessage(const char *message, size_t length) {
  while (length > 0) {
    const char c = message[length - 1];
    if (c == '9') {
      gScrollDirection = 1;
      break;
    }
    if (c == '0') {
      gScrollDirection = -1;
      break;
    }
    if (!isspace(static_cast<unsigned char>(c))) {
      break;
    }
    --length;
  }
}

bool isPingCommand(const char *message, size_t length) {
  size_t start = 0;
  while (start < length &&
         isspace(static_cast<unsigned char>(message[start]))) {
    ++start;
  }

  if (start >= length) {
    return false;
  }

  size_t end = length;
  while (end > start &&
         isspace(static_cast<unsigned char>(message[end - 1]))) {
    --end;
  }

  size_t trimmedEnd = end;
  if (trimmedEnd > start) {
    const char lastChar = message[trimmedEnd - 1];
    if (lastChar == '0' || lastChar == '9') {
      --trimmedEnd;
      while (trimmedEnd > start &&
             isspace(static_cast<unsigned char>(message[trimmedEnd - 1]))) {
        --trimmedEnd;
      }
    }
  }

  const size_t trimmedLength = (trimmedEnd > start) ? (trimmedEnd - start) : 0;
  if (trimmedLength != kPingCommandLength) {
    return false;
  }

  for (size_t i = 0; i < kPingCommandLength; ++i) {
    const char c = message[start + i];
    if (toupper(static_cast<unsigned char>(c)) != kPingCommand[i]) {
      return false;
    }
  }

  return true;
}

void commitSerialMessage() {
  gSerialInputBuffer[gSerialInputLength] = '\0';
  const bool isPing = isPingCommand(gSerialInputBuffer, gSerialInputLength);
  updateScrollDirectionFromMessage(gSerialInputBuffer, gSerialInputLength);
  setMessage(gSerialInputBuffer, gSerialInputLength);
  gPingPongState =
      isPing ? PingPongState::AwaitingBounce : PingPongState::None;
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
