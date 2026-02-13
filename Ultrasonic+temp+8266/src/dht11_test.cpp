// dht11_reader.cpp - Production-grade DHT11 reader using proven library
// Focus: static config, error retry, no heap, clear failure modes

#include <Arduino.h>
#include <DHT.h>                // Adafruit DHT library

// === Configuration - change only here ===
#define DHT_PIN     D5          // GPIO14 – good choice, low interference
#define DHT_TYPE    DHT11
#define MAX_RETRIES 4           // More retries = better reliability in noisy env
#define READ_INTERVAL_MS 2500   // DHT11 min 2 s

// Static instance – no dynamic alloc
static DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
    Serial.begin(115200);
    delay(200);                 // Stabilize serial
    Serial.println("\n=== DHT11 Production Reader (Library-based) ===\n");

    dht.begin();                // Initializes GPIO + sensor check
    pinMode(DHT_PIN, INPUT);    // Redundant but explicit
}

void loop() {
    bool success = false;
    float h = NAN;
    float t = NAN;

    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        // Library handles timing, checksum, bit-bang internally
        h = dht.readHumidity();
        t = dht.readTemperature();  // °C

        // Library returns NAN on failure (timeout/checksum/range)
        if (!isnan(h) && !isnan(t)) {
            // Optional extra sanity (production habit)
            if (h >= 20 && h <= 90 && t >= 0 && t <= 50) {
                success = true;
                break;
            }
        }

        Serial.printf("Read attempt %d/%d failed (h=%.1f t=%.1f)\n", retry + 1, MAX_RETRIES, h, t);
        delay(500 + retry * 200);   // Backoff slightly
    }

    if (success) {
        Serial.printf("Humidity:    %.1f %%\n", h);
        Serial.printf("Temperature: %.1f °C\n", t);
        Serial.println("---------------------");
    } else {
        Serial.println("All retries failed → check wiring, power, sensor health");
        // In production: could trigger fallback / alert / deep-sleep skip
    }

    delay(READ_INTERVAL_MS);
}