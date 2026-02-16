#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>

// ────────────────────────────────────────────────
// Wi-Fi credentials – CHANGE THESE TO YOUR NETWORK
// ────────────────────────────────────────────────
const char* ssid     = "YOUR_SSID";
const char* password = "WIFI_PASSWORD";

// ────────────────────────────────────────────────
// Pins & constants
// ────────────────────────────────────────────────
#define TRIG_PIN    5           // D1 – GPIO5
#define ECHO_PIN    4           // D2 – GPIO4
#define DHT_PIN     14          // D5 – GPIO14
#define DHT_TYPE    DHT11

#define SOUND_SPEED_BASE    331.3f
#define SOUND_TEMP_COEFF    0.606f
#define SOUND_HUM_COEFF     0.0124f
#define CM_PER_M            100.0f
#define US_PER_S            1000000.0f
#define TIMEOUT_US          30000UL

// ────────────────────────────────────────────────
// Globals – static allocation only, no heap
// ────────────────────────────────────────────────
volatile unsigned long duration = 0;
volatile unsigned long start_time = 0;
static float last_temp = 20.0f;
static float last_hum  = 50.0f;
static float last_dist_cm = -1.0f;
static unsigned long last_update_ms = 0;
static unsigned long last_trigger_ms = 0;

// Objects
DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);

// ────────────────────────────────────────────────
// ISR – placed in IRAM (critical on ESP8266)
// ────────────────────────────────────────────────
void IRAM_ATTR echo_isr() {
    if (digitalRead(ECHO_PIN) == HIGH) {
        start_time = micros();
    } else {
        duration = micros() - start_time;
    }
}

// ────────────────────────────────────────────────
// Web page handler (forward declaration + definition)
// ────────────────────────────────────────────────
void handleRoot();

void handleRoot() {
    char buffer[1024];  // fixed-size static buffer – safe & predictable memory usage

    snprintf(buffer, sizeof(buffer),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='6'>"   // auto-refresh every 6 seconds
        "<title>ESP8266 Sensor Monitor</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;text-align:center;background:#f8f9fa;}"
        "h1{color:#2c3e50;}"
        ".card{background:white;margin:20px auto;padding:25px;width:90%%;max-width:550px;"
        "border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}"
        ".value{font-size:2.4em;font-weight:bold;margin:12px 0;}"
        ".warn{color:#c0392b;font-weight:bold;}"
        ".ok{color:#27ae60;}"
        "</style>"
        "</head><body>"
        "<h1>ESP8266 Live Sensors</h1>"
        "<div class='card'>"
        "<p>Temperature: <span class='value'>%.1f &deg;C</span></p>"
        "<p>Humidity: <span class='value'>%.1f %%</span></p>"
        "<p>Distance: <span class='value'>%.1f cm</span></p>"
        "<p><small>Last update: %lu ms ago</small></p>",
        last_temp, last_hum, last_dist_cm,
        millis() - last_update_ms
    );

    if (last_dist_cm >= 0 && last_dist_cm < 30.0f) {
        strcat(buffer, "<p class='value warn'>OBSTACLE DETECTED (close)</p>");
    } else if (last_dist_cm >= 0) {
        strcat(buffer, "<p class='value ok'>Path clear</p>");
    } else {
        strcat(buffer, "<p class='value warn'>No valid distance reading</p>");
    }

    strcat(buffer, "</div></body></html>");

    server.send(200, "text/html", buffer);
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);  // Give serial time to initialize

    Serial.println("\n=== ESP8266 Web Server + DHT11 + HC-SR04 ===");
    Serial.println("Production-style: non-blocking, static alloc, IRAM ISR\n");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echo_isr, CHANGE);

    dht.begin();

    // Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to ");
    Serial.print(ssid);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");
    Serial.print("IP address: http://");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.begin();
    Serial.println("HTTP server started");
}

// ────────────────────────────────────────────────
// Main loop – must exist and run continuously
// ────────────────────────────────────────────────
void loop() {
    server.handleClient();   // Process incoming HTTP requests
    yield();                 // Give Wi-Fi / TCP stack time slices (critical!)

    unsigned long now = millis();

    // DHT11 – slow sensor, read every ~10 seconds
    static unsigned long last_dht_read = 0;
    if (now - last_dht_read > 10000) {
        last_dht_read = now;

        float t = dht.readTemperature();
        float h = dht.readHumidity();

        if (!isnan(t) && !isnan(h)) {
            last_temp = t;
            last_hum  = h;
        } else {
            Serial.println("DHT11 read failed → keeping last good values");
        }
    }

    // Ultrasonic – non-blocking trigger & check
    static bool waiting_for_echo = false;

    if (!waiting_for_echo && (now - last_trigger_ms > 1500)) {
        // Start new measurement
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(12);
        digitalWrite(TRIG_PIN, LOW);

        duration = 0;               // reset
        waiting_for_echo = true;
        last_trigger_ms = now;
    }

    // Check if echo arrived
    if (waiting_for_echo && duration > 0) {
        float speed_m_s = SOUND_SPEED_BASE + SOUND_TEMP_COEFF * last_temp + SOUND_HUM_COEFF * last_hum;
        float factor_cm_us = (speed_m_s * CM_PER_M / US_PER_S) / 2.0f;

        if (duration < TIMEOUT_US) {
            last_dist_cm = duration * factor_cm_us;
        } else {
            last_dist_cm = -1.0f;
        }

        last_update_ms = now;
        waiting_for_echo = false;

        Serial.printf("T: %.1f °C   H: %.1f %%   Dist: %.1f cm   (duration %lu µs)\n",
                      last_temp, last_hum, last_dist_cm, duration);
    }

    // Safety timeout – prevent hanging if echo never comes
    if (waiting_for_echo && (now - last_trigger_ms > 200)) {
        waiting_for_echo = false;
        last_dist_cm = -1.0f;
        Serial.println("Ultrasonic timeout – no echo received");
    }
}