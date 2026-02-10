#include <Arduino.h>

#define TRIG_PIN    5          // GPIO5 = D1
#define ECHO_PIN    4          // GPIO4 = D2 — interrupt capable

// Ultrasonic constants
#define SOUND_SPEED_BASE    331.3f     // m/s at 0°C
#define SOUND_TEMP_COEFF    0.606f     // m/s per °C
#define CM_PER_M            100.0f
#define US_PER_S            1000000.0f
#define TIMEOUT_US          30000UL    // ~5 m max
#define CM_TO_INCH          0.393701f

// KY-028 Thermistor constants (NTC 10k @25°C, B=3950)
#define SERIES_R            10000.0f   // Fixed resistor
#define NTC_R25             10000.0f   // NTC at 25°C
#define BETA                3950.0f    // Beta constant
#define T0_KELVIN           298.15f    // 25°C in Kelvin
#define VCC                 3.3f       // Supply voltage
#define ADC_MAX             1024.0f    // ESP8266 ADC resolution (scaled to 3.3V)
#define TEMP_AVG_SAMPLES    10         // For noise reduction
#define TEMP_UPDATE_INTERVAL 10        // Update speed every X measurements

volatile unsigned long duration = 0;
float sound_speed_factor = (0.0343f / 2.0f);  // Default @20°C
int loop_count = 0;

void ICACHE_RAM_ATTR echo_isr() {
    static unsigned long start_time = 0;
    if (digitalRead(ECHO_PIN) == HIGH) {
        start_time = micros();
    } else {
        duration = micros() - start_time;
    }
}

float read_temperature() {
    float adc_sum = 0.0f;
    for (int i = 0; i < TEMP_AVG_SAMPLES; i++) {
        adc_sum += analogRead(A0);
        delayMicroseconds(100);  // Short settle time
    }
    float adc_avg = adc_sum / TEMP_AVG_SAMPLES;
    
    if (adc_avg < 1.0f || adc_avg > (ADC_MAX - 1.0f)) {
        return 20.0f;  // Fallback to default if invalid
    }
    
    float voltage = (adc_avg / ADC_MAX) * VCC;
    float resistance = (voltage * SERIES_R) / (VCC - voltage);
    
    float temp_k = 1.0f / ((1.0f / T0_KELVIN) + (1.0f / BETA) * log(resistance / NTC_R25));
    float temp_c = temp_k - 273.15f;
    
    // Bounds check (realistic range)
    if (temp_c < -10.0f || temp_c > 60.0f) {
        return 20.0f;  // Invalid — fallback
    }
    
    return temp_c;
}

void update_sound_speed(float temp_c) {
    float speed_m_s = SOUND_SPEED_BASE + SOUND_TEMP_COEFF * temp_c;// temp_c;  // Base speed at 20°C
    float speed_cm_us = (speed_m_s * CM_PER_M) / US_PER_S;
    sound_speed_factor = speed_cm_us / 2.0f;
}

void setup() {
    Serial.begin(115200);
    delay(10000);
    Serial.println("\nESP8266 HC-SR04 + KY-028 Temp Compensation");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echo_isr, CHANGE);
    
    // Initial temp read
    float initial_temp = read_temperature();
    update_sound_speed(initial_temp);
    Serial.printf("Initial Temp: %.1f °C\n", initial_temp);
}

void loop() {
    // Trigger pulse
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(12);
    digitalWrite(TRIG_PIN, LOW);

    delayMicroseconds(300);

    unsigned long wait_start = micros();
    duration = 0;

    while (duration == 0 && (micros() - wait_start < TIMEOUT_US + 5000UL)) {
        delayMicroseconds(100);
    }

    if (duration > 0 && duration < TIMEOUT_US) {
        float distance_cm = duration * sound_speed_factor;
        float distance_inch = distance_cm * CM_TO_INCH;

        Serial.printf("Distance: %6.1f cm  |  %5.1f inch  (duration %lu µs)\n",
                      distance_cm, distance_inch, duration);
    } else {
        Serial.println("No echo / timeout / out of range");
    }

    // Periodic temp update
    loop_count++;
    if (loop_count >= TEMP_UPDATE_INTERVAL) {
        float temp_c = read_temperature();
        update_sound_speed(temp_c);
        Serial.printf("Updated Temp: %.1f °C | Speed Factor: %.6f cm/µs\n", temp_c, sound_speed_factor * 2.0f);
        loop_count = 0;
    }

    delay(950);
}