#include <Arduino.h>
#include <SoftwareSerial.h>

// ────────────────────────────────────────────────
// GPS wiring for IdeaSpark NodeMCU
// ────────────────────────────────────────────────
#define GPS_RX_PIN  D7          // GPS TX → ESP D7 (GPIO13) – ESP receives
#define GPS_TX_PIN  D8          // GPS RX ← ESP D8 (GPIO15) – ESP transmits

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=== GPS RAW NMEA DUMP – Production Debug Test ===\n");
    Serial.println("Wiring confirmation:");
    Serial.println("  GPS TX → NodeMCU D7 (GPIO13)");
    Serial.println("  GPS RX → NodeMCU D8 (GPIO15)");
    Serial.println("  GPS VCC → 3V3     GND → GND\n");

    gpsSerial.begin(9600);
    Serial.println("GPS UART started at 9600 baud on D7 (RX) / D8 (TX)");
    Serial.println("Take board outdoors or near large window.");
    Serial.println("First fix can take 30–180 s (cold start).");
    Serial.println("----------------------------------------\n");
}

void loop() {
    static unsigned long last_beat = 0;

    // Forward every byte from GPS to USB serial monitor
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        Serial.print(c);
    }

    // Heartbeat so we know code is alive
    if (millis() - last_beat > 5000) {
        last_beat = millis();
        Serial.printf("\n[%lu s] Still running – waiting for GPS data...\n", millis() / 1000);
    }
}