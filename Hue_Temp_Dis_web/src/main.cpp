#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <DHT.h>
#include <WiFiServer.h>   // or use WiFiServer for custom

// ────────────────────────────────────────────────
// Configuration – CHANGE THESE
// ────────────────────────────────────────────────
const char* ssid     = "YourWiFiSSID";
const char* password = "YourWiFiPassword";
const char* ota_password = "mysecretpassword123";   // ← same in platformio.ini --auth=

// ────────────────────────────────────────────────
// Pins
// ────────────────────────────────────────────────
#define TRIG_PIN    5           // D1 – GPIO5
#define ECHO_PIN    4           // D2 – GPIO4
#define DHT_PIN     14          // D5 – GPIO14
#define DHT_TYPE    DHT11

// ────────────────────────────────────────────────
// Constants
// ────────────────────────────────────────────────
#define SOUND_SPEED_BASE    331.3f
#define SOUND_TEMP_COEFF    0.606f
#define SOUND_HUM_COEFF     0.0124f
#define CM_PER_M            100.0f
#define US_PER_S            1000000.0f
#define TIMEOUT_US          30000UL

// ────────────────────────────────────────────────
// Globals – static only (production: no heap)
volatile unsigned long duration = 0;
volatile unsigned long start_time = 0;
static float last_temp = 20.0f;
static float last_hum  = 50.0f;
static float last_dist_cm = -1.0f;
static unsigned long last_update_ms = 0;
static unsigned long last_trigger_ms = 0;
WiFiServer telnetServer(23);  // Telnet port 23

// Objects
DHT dht(DHT_PIN, DHT_TYPE);
ESP8266WebServer server(80);

// ────────────────────────────────────────────────
// ISR – placed in IRAM (critical!)
void IRAM_ATTR echo_isr() {
    if (digitalRead(ECHO_PIN) == HIGH) {
        start_time = micros();
    } else {
        duration = micros() - start_time;
    }
}

// ────────────────────────────────────────────────
// Web page handler
void handleRoot() {
    char buf[1024];
    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='6'>"
        "<title>ESP8266 Monitor</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;text-align:center;background:#f8f9fa;}"
        "h1{color:#2c3e50;}.card{background:white;margin:20px auto;padding:25px;"
        "width:90%%;max-width:550px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}"
        ".value{font-size:2.4em;font-weight:bold;margin:12px 0;}"
        ".warn{color:#c0392b;}.ok{color:#27ae60;}"
        "</style></head>"
        "<body><h1>Live Environmental Data - ota_V1.0.1</h1>"
        "<div class='card'>"
        "<p>Temperature: <span class='value'>%.1f &deg;C</span></p>"
        "<p>Humidity: <span class='value'>%.1f %%</span></p>"
        "<p>Distance: <span class='value'>%.1f cm</span></p>"
        "<p><small>Last update: %lu ms ago</small></p>",
        last_temp, last_hum, last_dist_cm,
        "<p>Device IP: %s</p>", WiFi.localIP().toString().c_str(),
        millis() - last_update_ms
    );

    if (last_dist_cm >= 0 && last_dist_cm < 30.0f) {
        strcat(buf, "<p class='value warn'>OBSTACLE DETECTED (close)</p>");
    } else if (last_dist_cm >= 0) {
        strcat(buf, "<p class='value ok'>Path clear</p>");
    } else {
        strcat(buf, "<p class='value warn'>No valid distance reading</p>");
    }

    strcat(buf, "</div></body></html>");

    server.send(200, "text/html", buf);
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n=== ESP8266 IoT Sensor Node – Production Style ===\n");

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

    // mDNS – so you can use esp-sensor.local
    if (MDNS.begin("esp-sensor")) {
        Serial.println("mDNS started → http://esp-sensor.local");
    } else {
        Serial.println("mDNS failed");
    }

    // Telnet server for remote monitoring over OTA
    telnetServer.begin();
    Serial.println("Telnet server started on port 23 – connect with telnet 192.168.1.132 23");

    // OTA setup
    ArduinoOTA.setHostname("esp-sensor");
    ArduinoOTA.setPassword(ota_password);

    ArduinoOTA.onStart([]()    { Serial.println("OTA: Update started"); });
    ArduinoOTA.onEnd([]()      { Serial.println("\nOTA: Update finished"); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
        Serial.printf("OTA progress: %u%%\r", (p / (t / 100)));
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("OTA Error[%u]: ", e);
        if (e == OTA_AUTH_ERROR) Serial.println("Auth failed (wrong password?)");
        else if (e == OTA_BEGIN_ERROR) Serial.println("Begin failed");
        else if (e == OTA_CONNECT_ERROR) Serial.println("Connect failed");
        else if (e == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
        else if (e == OTA_END_ERROR) Serial.println("End failed");
    });

    ArduinoOTA.begin();
    Serial.println("OTA ready – password protected");

    // Web server
    server.on("/", handleRoot);
    server.begin();
    Serial.println("HTTP server started");
}

// ────────────────────────────────────────────────
void loop() {
    ArduinoOTA.handle();      // Required for OTA to work
    server.handleClient();    // Required for web server
    yield();                  // Give Wi-Fi stack time

    unsigned long now = millis();

    // DHT11 – read every 10 seconds
    static unsigned long last_dht = 0;
    if (now - last_dht > 10000) {
        last_dht = now;
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t) && !isnan(h)) {
            last_temp = t;
            last_hum  = h;
        }
    }

    // Ultrasonic – non-blocking
    static bool waiting = false;
    if (!waiting && (now - last_trigger_ms > 1500)) {
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(12);
        digitalWrite(TRIG_PIN, LOW);

        duration = 0;
        waiting = true;
        last_trigger_ms = now;
    }

    if (waiting && duration > 0) {
        float speed = SOUND_SPEED_BASE + SOUND_TEMP_COEFF * last_temp + SOUND_HUM_COEFF * last_hum;
        float factor = (speed * CM_PER_M / US_PER_S) / 2.0f;
        last_dist_cm = (duration < TIMEOUT_US) ? duration * factor : -1.0f;
        last_update_ms = now;
        waiting = false;

        Serial.printf("T: %.1f°C  H: %.1f%%  Dist: %.1f cm\n", last_temp, last_hum, last_dist_cm);
    }

    if (waiting && (now - last_trigger_ms > 200)) {
        waiting = false;
        last_dist_cm = -1.0f;
    }

    WiFiClient telnetClient = telnetServer.available();
    if (telnetClient) {
        while (telnetClient.connected()) {
            if (telnetClient.available()) {
                // Optional: read from telnet to ESP if needed
                while (telnetClient.available()) Serial.write(telnetClient.read());
            }
            if (Serial.available()) {
                telnetClient.write(Serial.read());
            }
        }
        telnetClient.stop();
    }
}