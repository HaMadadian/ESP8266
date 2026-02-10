#include <Arduino.h>           // ← This is mandatory!

#define TRIG_PIN    5          // GPIO5 = D1 on NodeMCU
#define ECHO_PIN    4          // GPIO4 = D2 on NodeMCU — interrupt capable

#define SOUND_SPEED          0.0343f   // cm/µs at ~20°C
#define SOUND_SPEED_FACTOR   (SOUND_SPEED / 2.0f)
#define TIMEOUT_US           30000UL   // ~5 m max

#define CM_TO_INCH           0.393701f

volatile unsigned long duration = 0;

void ICACHE_RAM_ATTR echo_isr() {     // ← Correct macro for ESP8266
    static unsigned long start_time = 0;
    if (digitalRead(ECHO_PIN) == HIGH) {
        start_time = micros();
    } else {
        duration = micros() - start_time;
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);                       // Let serial stabilize
    Serial.println("\nESP8266 HC-SR04 – Production Example");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);         // No pull-up needed for HC-SR04 echo

    digitalWrite(TRIG_PIN, LOW);      // Idle low

    // Attach interrupt – CHANGE catches both edges
    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echo_isr, CHANGE);
}

void loop() {
    // Clean trigger pulse (≥10 µs high)
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(12);
    digitalWrite(TRIG_PIN, LOW);

    delayMicroseconds(300);           // Skip ringing/crosstalk

    unsigned long wait_start = micros();
    duration = 0;

    // Wait for ISR to fill duration (with timeout)
    while (duration == 0 && (micros() - wait_start < TIMEOUT_US + 5000UL)) {
        delayMicroseconds(100);
    }

    if (duration > 0 && duration < TIMEOUT_US) {
        float distance_cm   = duration * SOUND_SPEED_FACTOR;
        float distance_inch = distance_cm * CM_TO_INCH;

        Serial.printf("Distance: %6.1f cm  |  %5.1f inch  (duration %lu µs)\n",
                      distance_cm, distance_inch, duration);
    } else {
        Serial.println("No echo / timeout / out of range");
    }

    delay(950);   // ~1 Hz – conservative for sensor life & power
}