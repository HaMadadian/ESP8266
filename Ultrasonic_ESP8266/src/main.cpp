#include <Arduino.h>

#define TRIG_PIN 5   // D1 = GPIO5
#define ECHO_PIN 4   // D2 = GPIO4

volatile unsigned long startTime = 0;
volatile unsigned long endTime = 0;
volatile bool newEcho = false;

// ISR – must be fast, in IRAM
ICACHE_RAM_ATTR void echoISR() {
    if (digitalRead(ECHO_PIN)) {
        startTime = micros();
    } else {
        endTime = micros();
        newEcho = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);  // give serial time to settle
    Serial.println("\nHC-SR04 Production Demo");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT_PULLUP);  // internal pull-up helps stability

    // Interrupt on any edge – we discriminate in ISR
    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);

    digitalWrite(TRIG_PIN, LOW);  // idle low
}

void loop() {
    // Clean trigger pulse
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(12);  // >10 µs
    digitalWrite(TRIG_PIN, LOW);

    newEcho = false;

    unsigned long timeoutStart = micros();
    while (!newEcho && (micros() - timeoutStart < 300000UL)) {  // 30 m max
        yield();  // allow WiFi/cooperative tasks
    }

    if (newEcho && endTime > startTime) {
        unsigned long duration = endTime - startTime;
        float distance_cm = duration * 0.034f / 2.0f;  // speed of sound 340 m/s
        Serial.printf("Distance: %.1f cm\n", distance_cm);
    } else {
        Serial.println("No echo / timeout");
    }

    delay(1000);  // 1 Hz measurement – production-friendly rate
}