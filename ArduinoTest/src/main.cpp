#include <Arduino.h>

constexpr uint8_t kLedPin = LED_BUILTIN; // Use built-in LED pin; adjust if needed
constexpr unsigned long kBlinkIntervalMs = 500; // Half-second on/off cadence

void setup() {
    pinMode(kLedPin, OUTPUT);
}

void loop() {
    digitalWrite(kLedPin, HIGH);
    delay(kBlinkIntervalMs);

    digitalWrite(kLedPin, LOW);
    delay(kBlinkIntervalMs);
}
