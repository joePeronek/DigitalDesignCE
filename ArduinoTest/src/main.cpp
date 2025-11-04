#include <Arduino.h>

namespace {
constexpr uint8_t SEGMENT_COUNT = 7;
constexpr uint8_t DIGIT_COUNT = 4;

constexpr uint8_t segmentPins[SEGMENT_COUNT] = {2, 3, 4, 5,
                                                6, 7, 8};   // a-g shared
constexpr uint8_t digitPins[DIGIT_COUNT] = {9, 10, 11, 12}; // digit 1-4 anodes

constexpr uint8_t SEG_A = 1 << 0;
constexpr uint8_t SEG_B = 1 << 1;
constexpr uint8_t SEG_C = 1 << 2;
constexpr uint8_t SEG_D = 1 << 3;
constexpr uint8_t SEG_E = 1 << 4;
constexpr uint8_t SEG_F = 1 << 5;
constexpr uint8_t SEG_G = 1 << 6;

constexpr unsigned long COUNT_INTERVAL_MS = 20;
constexpr unsigned long FLIP_FRAME_DURATION_MS[] = {150, 110, 150};
constexpr unsigned int MULTIPLEX_ON_TIME_US = 1000;

constexpr uint8_t normalDigitPatterns[10] = {
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

constexpr uint8_t rotateSegments(uint8_t pattern) {
  return ((pattern & SEG_A) ? SEG_D : 0) | ((pattern & SEG_B) ? SEG_E : 0) |
         ((pattern & SEG_C) ? SEG_F : 0) | ((pattern & SEG_D) ? SEG_A : 0) |
         ((pattern & SEG_E) ? SEG_B : 0) | ((pattern & SEG_F) ? SEG_C : 0) |
         ((pattern & SEG_G) ? SEG_G : 0);
}

constexpr uint8_t invertedDigitPatterns[10] = {
    rotateSegments(normalDigitPatterns[0]),
    rotateSegments(normalDigitPatterns[1]),
    rotateSegments(normalDigitPatterns[2]),
    rotateSegments(normalDigitPatterns[3]),
    rotateSegments(normalDigitPatterns[4]),
    rotateSegments(normalDigitPatterns[5]),
    rotateSegments(normalDigitPatterns[6]),
    rotateSegments(normalDigitPatterns[7]),
    rotateSegments(normalDigitPatterns[8]),
    rotateSegments(normalDigitPatterns[9])};

enum class Mode { CountUp, FlipAnimation, CountDown };

Mode mode = Mode::CountUp;
bool invertedDisplay = false;
bool animationTargetInverted = false;
int currentValue = 9000;
unsigned long lastUpdateMs = 0;

uint8_t activePatterns[DIGIT_COUNT] = {0, 0, 0, 0};

constexpr uint8_t NORMAL_DIGIT_ORDER[DIGIT_COUNT] = {0, 1, 2, 3};
constexpr uint8_t INVERTED_DIGIT_ORDER[DIGIT_COUNT] = {3, 2, 1, 0};

uint8_t animationFrame = 0;
unsigned long animationFrameStartMs = 0;
constexpr uint8_t FLIP_FRAME_COUNT = 4;

uint8_t patternForDigit(uint8_t digit, bool inverted) {
  if (digit > 9) {
    return 0;
  }
  return inverted ? invertedDigitPatterns[digit] : normalDigitPatterns[digit];
}

void setUniformPattern(uint8_t pattern) {
  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    activePatterns[i] = pattern;
  }
}

void setPatternsForValue(int value, bool inverted) {
  value = constrain(value, 0, 9999);

  const uint8_t digits[DIGIT_COUNT] = {
      static_cast<uint8_t>((value / 1000) % 10),
      static_cast<uint8_t>((value / 100) % 10),
      static_cast<uint8_t>((value / 10) % 10),
      static_cast<uint8_t>(value % 10)};

  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    activePatterns[i] = 0;
  }

  const uint8_t *digitOrder =
      inverted ? INVERTED_DIGIT_ORDER : NORMAL_DIGIT_ORDER;

  bool leadingZero = true;
  for (uint8_t position = 0; position < DIGIT_COUNT; ++position) {
    const uint8_t digit = digits[position];
    if (leadingZero && digit == 0 && position < DIGIT_COUNT - 1) {
      continue;
    }
    leadingZero = false;
    activePatterns[digitOrder[position]] = patternForDigit(digit, inverted);
  }
}

void applyAnimationFrame() {
  switch (animationFrame) {
  case 0:
    setPatternsForValue(currentValue, invertedDisplay);
    break;
  case 1:
    setUniformPattern(0);
    break;
  case 2:
    setUniformPattern(SEG_G);
    break;
  default:
    setPatternsForValue(currentValue, animationTargetInverted);
    break;
  }
}

void startFlipAnimation(bool targetInverted) {
  mode = Mode::FlipAnimation;
  animationTargetInverted = targetInverted;
  animationFrame = 0;
  animationFrameStartMs = millis();
  applyAnimationFrame();
}

void updateFlipAnimation(unsigned long now) {
  if (animationFrame >= FLIP_FRAME_COUNT - 1) {
    return;
  }

  const unsigned long elapsed = now - animationFrameStartMs;
  if (elapsed < FLIP_FRAME_DURATION_MS[animationFrame]) {
    return;
  }

  ++animationFrame;
  animationFrameStartMs = now;
  applyAnimationFrame();

  if (animationFrame == FLIP_FRAME_COUNT - 1) {
    invertedDisplay = animationTargetInverted;
    setPatternsForValue(currentValue, invertedDisplay);
    mode = animationTargetInverted ? Mode::CountDown : Mode::CountUp;
    lastUpdateMs = now;
  }
}

void refreshDisplay() {
  static uint8_t digitIndex = 0;

  for (uint8_t i = 0; i < DIGIT_COUNT; ++i) {
    digitalWrite(digitPins[i], LOW);
  }

  digitIndex = (digitIndex + 1) % DIGIT_COUNT;

  const uint8_t pattern = activePatterns[digitIndex];
  for (uint8_t seg = 0; seg < SEGMENT_COUNT; ++seg) {
    const bool segmentOn = pattern & (1 << seg);
    digitalWrite(segmentPins[seg], segmentOn ? LOW : HIGH);
  }

  digitalWrite(digitPins[digitIndex], HIGH);
  delayMicroseconds(MULTIPLEX_ON_TIME_US);
  digitalWrite(digitPins[digitIndex], LOW);
}

} // namespace

void setup() {
  for (uint8_t pin : segmentPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  for (uint8_t pin : digitPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  setPatternsForValue(currentValue, invertedDisplay);
  lastUpdateMs = millis();
}

void loop() {
  refreshDisplay();

  const unsigned long now = millis();

  switch (mode) {
  case Mode::CountUp:
    if (now - lastUpdateMs >= COUNT_INTERVAL_MS) {
      lastUpdateMs = now;
      if (currentValue < 9999) {
        ++currentValue;
        setPatternsForValue(currentValue, invertedDisplay);
      } else {
        startFlipAnimation(true);
      }
    }
    break;
  case Mode::CountDown:
    if (now - lastUpdateMs >= COUNT_INTERVAL_MS) {
      lastUpdateMs = now;
      if (currentValue > 0) {
        --currentValue;
        setPatternsForValue(currentValue, invertedDisplay);
      } else {
        startFlipAnimation(false);
      }
    }
    break;
  case Mode::FlipAnimation:
    updateFlipAnimation(now);
    break;
  }
}
