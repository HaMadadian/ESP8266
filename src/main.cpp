#include <Arduino.h>

// Pin definitions — production code always uses named constants
#define BUTTON_PIN  5   // GPIO5 = D1 on NodeMCU
#define LED_PIN     2   // GPIO2 = D4, onboard LED (active LOW)

// Shared variables between ISR and main — MUST be volatile
volatile uint32_t last_interrupt_time = 0;
volatile bool     button_pressed      = false;

// Simple software debounce threshold (ms)
const uint32_t DEBOUNCE_TIME_MS = 50;

// ISR — keep it SHORT & FAST (no delay, no Serial.print here in production!)
void IRAM_ATTR handleButtonInterrupt() {
    uint32_t now = millis();  // From Arduino timer — safe in ISR on ESP8266

    if ((now - last_interrupt_time) > DEBOUNCE_TIME_MS) {
        button_pressed = true;
        last_interrupt_time = now;
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);                // Give serial time to init
    Serial.println("\nESP8266 Button Interrupt Demo");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);  // LED off (active low)

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Attach interrupt — FALLING because button to GND
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

    Serial.println("Setup complete. Press button to toggle LED.");
}

void loop() {
    if (button_pressed) {
        // Toggle LED
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));

        // Reset flag
        noInterrupts();                // Critical section protect
        button_pressed = false;
        interrupts();

        Serial.println("Button press detected → LED toggled");
    }

    // In production: go to light sleep or deep sleep here if nothing to do
}