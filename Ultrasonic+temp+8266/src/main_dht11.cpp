// esp8266_ultrasonic_dht11_reliable.ino
// Uses old working ISR style + tight polling to guarantee capture on ESP8266

#include <Arduino.h>
#include <DHT.h>

// Pins & constants (unchanged from your working setup)
#define TRIG_PIN    D1          // GPIO5
#define ECHO_PIN    D2          // GPIO4
#define DHT_PIN     D5          // GPIO14    
#define DHT_TYPE    DHT11

#define SOUND_SPEED_BASE    331.3f
#define SOUND_TEMP_COEFF    0.606f
#define SOUND_HUM_COEFF     0.0124f
#define CM_PER_M            100.0f
#define US_PER_S            1000000.0f
#define TIMEOUT_US          30000UL
#define CM_TO_INCH          0.393701f

volatile unsigned long duration = 0;
static unsigned long start_time = 0;  // static inside ISR scope in old code

void ICACHE_RAM_ATTR echo_isr() {
    if (digitalRead(ECHO_PIN) == HIGH) {
        start_time = micros();
    } else {
        duration = micros() - start_time;
    }
}

static DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
    Serial.begin(115200);
    delay(2000);  // Stabilize serial + sensor power
    Serial.println("\n=== Reliable HC-SR04 + DHT11 Compensation (Old ISR Pattern) ===\n");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echo_isr, CHANGE);  // ← restored: CHANGE mode

    dht.begin();
    Serial.println("Sensors ready");
}

void loop() {
    // DHT11 read with retry (static last-good for fallback)
    static float last_temp = 20.0f;
    static float last_hum  = 50.0f;
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    bool valid = !isnan(t) && !isnan(h);
    if (valid) {
        last_temp = t;
        last_hum  = h;
    } else {
        Serial.println("DHT11 failed → using last good values");
    }

    delay(100);  // Stagger: let DHT11 settle before ultrasonic burst
    
    float speed_m_s = SOUND_SPEED_BASE + SOUND_TEMP_COEFF * last_temp + SOUND_HUM_COEFF * last_hum;
    float factor_cm_us = (speed_m_s * CM_PER_M / US_PER_S) / 2.0f;

    Serial.printf("T: %.1f °C  H: %.1f %%  Speed: %.1f m/s  Factor: %.6f\n",
                  last_temp, last_hum, speed_m_s, factor_cm_us);

    // Trigger (old reliable sequence)
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(12);
    digitalWrite(TRIG_PIN, LOW);

    delayMicroseconds(300);  // small settle

    duration = 0;
    unsigned long wait_start = micros();

    while (duration == 0 && (micros() - wait_start < TIMEOUT_US + 5000UL)) {
        delayMicroseconds(100);  // tight loop – no yield, preserves ISR priority
        if ((micros() - wait_start) % 5000 == 0) {
            Serial.printf("Echo state during wait: %d\n", digitalRead(ECHO_PIN));
}
    }

    if (duration > 0 && duration < TIMEOUT_US) {
        float dist_cm = duration * factor_cm_us;
        Serial.printf("Distance: %.1f cm  |  %.1f inch  (duration %lu µs)\n",
                      dist_cm, dist_cm * CM_TO_INCH, duration);
    } else {
        Serial.printf("No echo / timeout (duration = %lu µs)\n", duration);
    }

    Serial.println("---------------------");
    delay(950);  // old interval – keeps sensor happy
}