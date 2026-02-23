#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>

// ────────────────────────────────────────────────
// Configuration
// ────────────────────────────────────────────────
const char* WIFI_SSID     = "wifi-ssid";                    
const char* WIFI_PASSWORD = "wifi-pass";                

// ────────────────────────────────────────────────
// Pins
// ────────────────────────────────────────────────
#define TRIG_PIN            5
#define ECHO_PIN            4
#define DHT_PIN             14
#define DHT_TYPE            DHT11
#define GPS_RX_PIN          D7          // GPS TX → ESP RX
#define GPS_TX_PIN          D8          // GPS RX ← ESP TX
#define GPS_BAUD            9600

// Constants
#define SOUND_SPEED_BASE    331.3f
#define SOUND_TEMP_COEFF    0.606f
#define SOUND_HUM_COEFF     0.0124f
#define CM_PER_M            100.0f
#define US_PER_S            1000000.0f
#define ECHO_TIMEOUT_US     30000UL

// ────────────────────────────────────────────────
// Globals – declared first (critical for C++)
// ────────────────────────────────────────────────
volatile unsigned long echo_pulse_duration_us = 0;
volatile unsigned long echo_start_us = 0;

static float   last_temperature_c = 20.0f;
static float   last_humidity_pct  = 50.0f;
static float   last_distance_cm   = -1.0f;
static uint32_t last_sensor_update_ms = 0;

// GPS parser & UART
static TinyGPSPlus gps_parser;
static SoftwareSerial gps_uart(GPS_RX_PIN, GPS_TX_PIN);

// Raw GPS buffer – fixed size, circular, no heap
#define RAW_GPS_BUFFER_SIZE 2048
static char raw_gps_buffer[RAW_GPS_BUFFER_SIZE];
static uint16_t raw_gps_head = 0;   // write index
static uint16_t raw_gps_tail = 0;   // read index (for future use)

// Objects
DHT dht_sensor(DHT_PIN, DHT_TYPE);
ESP8266WebServer http_server(80);

// ────────────────────────────────────────────────
// ISR
// ────────────────────────────────────────────────
void IRAM_ATTR echo_isr() {
    if (digitalRead(ECHO_PIN) == HIGH) {
        echo_start_us = micros();
    } else if (echo_start_us != 0) {
        echo_pulse_duration_us = micros() - echo_start_us;
        echo_start_us = 0;
    }
}

// ────────────────────────────────────────────────
// Timestamped logging
// ────────────────────────────────────────────────
void log(const char* format, ...) {
    char msg[160];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    char ts[16];
    snprintf(ts, sizeof(ts), "[%07lu] ", millis());

    Serial.print(ts);
    Serial.println(msg);
}

// ────────────────────────────────────────────────
// Append byte to circular raw GPS buffer
// ────────────────────────────────────────────────
void append_raw_gps(char c) {
    raw_gps_buffer[raw_gps_head] = c;
    raw_gps_head = (raw_gps_head + 1) % RAW_GPS_BUFFER_SIZE;

    // If head catches tail, move tail forward (drop oldest)
    if (raw_gps_head == raw_gps_tail) {
        raw_gps_tail = (raw_gps_tail + 1) % RAW_GPS_BUFFER_SIZE;
    }
}

// ────────────────────────────────────────────────
// Main page – with link to raw GPS
// ────────────────────────────────────────────────
void handleRoot() {
    char buf[1400];

    char gps_line[120];
    if (gps_parser.location.isValid()) {
        snprintf(gps_line, sizeof(gps_line),
                 "%.6f, %.6f | Sats: %d | HDOP: %.1f | Age: %lu s",
                 gps_parser.location.lat(),
                 gps_parser.location.lng(),
                 gps_parser.satellites.value(),
                 gps_parser.hdop.hdop(),
                 gps_parser.location.age() / 1000UL);
    } else {
        strcpy(gps_line, "No valid fix (try outdoors)");
    }

    snprintf(buf, sizeof(buf),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta http-equiv='refresh' content='6'>"
        "<title>ESP8266 IoT Node</title>"
        "<style>"
        "body{font-family:Arial,sans-serif;text-align:center;background:#f8f9fa;}"
        "h1{color:#2c3e50;}.card{background:white;margin:20px auto;padding:25px;"
        "width:90%%;max-width:600px;border-radius:12px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}"
        ".value{font-size:2.2em;font-weight:bold;margin:12px 0;}"
        ".warn{color:#c0392b;}.ok{color:#27ae60;}"
        "a{display:block;margin:20px;font-size:1.2em;}"
        "</style></head>"
        "<body><h1>Live Sensor & GPS Data</h1>"
        "<div class='card'>"
        "<p>Temperature: <span class='value'>%.1f &deg;C</span></p>"
        "<p>Humidity: <span class='value'>%.1f %%</span></p>"
        "<p>Distance: <span class='value'>%.1f cm</span></p>"
        "<p><strong>GPS:</strong> <span class='value'>%s</span></p>"
        "<p><small>Last update: %lu ms ago</small></p>"
        "</div>"
        "<a href='/rawgps'>View Raw GPS NMEA Sentences (debug)</a>"
        "</body></html>",
        last_temperature_c, last_humidity_pct, last_distance_cm,
        gps_line,
        millis() - last_sensor_update_ms
    );

    if (last_distance_cm >= 0 && last_distance_cm < 30) {
        strcat(buf, "<p class='value warn'>CLOSE OBSTACLE DETECTED</p>");
    } else if (last_distance_cm >= 0) {
        strcat(buf, "<p class='value ok'>Path clear</p>");
    } else {
        strcat(buf, "<p class='value warn'>No valid distance reading</p>");
    }

    http_server.send(200, "text/html", buf);
}

// ────────────────────────────────────────────────
// Raw GPS debug page – shows last buffered NMEA
// ────────────────────────────────────────────────
void handleRawGPS() {
    char buf[2048];
    int len = 0;

    // Copy circular buffer to linear buffer (head to tail)
    uint16_t i = raw_gps_tail;
    while (i != raw_gps_head) {
        buf[len++] = raw_gps_buffer[i];
        i = (i + 1) % RAW_GPS_BUFFER_SIZE;
        if (len >= sizeof(buf) - 100) break;  // safety
    }
    buf[len] = '\0';

    char page[2500];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='3'>"
        "<title>Raw GPS Debug</title>"
        "<style>body{font-family:monospace;background:#000;color:#0f0;white-space:pre;}</style>"
        "</head><body>"
        "<h1>Raw NMEA Sentences (last %d bytes)</h1>"
        "<pre>%s</pre>"
        "<p><a href='/'>Back to main page</a></p>"
        "</body></html>",
        len, buf
    );

    http_server.send(200, "text/html", page);
}

// ────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    log("System boot – ESP8266 IoT node starting");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);
    attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echo_isr, CHANGE);
    log("Ultrasonic ISR attached on pin %d", ECHO_PIN);

    dht_sensor.begin();
    log("DHT11 sensor initialized");

    gps_uart.begin(GPS_BAUD);
    log("GPS UART started at %d baud on RX=%d TX=%d", GPS_BAUD, GPS_RX_PIN, GPS_TX_PIN);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    log("Connecting to WiFi: %s", WIFI_SSID);

    uint32_t wifi_timeout = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < wifi_timeout) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        log("WiFi connected – IP: %s", WiFi.localIP().toString().c_str());
    } else {
        log("WiFi timeout – rebooting in 10 s");
        delay(10000);
        ESP.restart();
    }

    http_server.on("/", handleRoot);
    http_server.on("/rawgps", handleRawGPS);
    http_server.begin();
    log("HTTP server started – /rawgps for GPS debug");
}

// ────────────────────────────────────────────────
void loop() {
    http_server.handleClient();
    yield();

    uint32_t now = millis();

    // ── GPS parsing & raw buffer ──────────────────────────
    while (gps_uart.available() > 0) {
        char c = gps_uart.read();
        append_raw_gps(c);  // store raw byte in circular buffer

        if (gps_parser.encode(c)) {
            if (gps_parser.location.isUpdated()) {
                log("GPS fix | Lat: %.6f | Lon: %.6f | Alt: %.1f m | Sats: %d | HDOP: %.1f",
                    gps_parser.location.lat(),
                    gps_parser.location.lng(),
                    gps_parser.altitude.meters(),
                    gps_parser.satellites.value(),
                    gps_parser.hdop.hdop());
            }
        }
    }

    // ── DHT11 – every 10 s ────────────────────────────────
    static uint32_t last_dht_ms = 0;
    if (now - last_dht_ms >= 10000) {
        last_dht_ms = now;

        float t = dht_sensor.readTemperature();
        float h = dht_sensor.readHumidity();

        if (!isnan(t) && !isnan(h)) {
            last_temperature_c = t;
            last_humidity_pct = h;
            log("DHT11 OK | T: %.1f °C | H: %.1f %%", t, h);
        } else {
            log("DHT11 read failure");
        }
    }

    // ── Ultrasonic ────────────────────────────────────────
    static bool pulse_active = false;
    static uint32_t last_trigger_ms = 0;

    if (!pulse_active && (now - last_trigger_ms >= 1500)) {
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(12);
        digitalWrite(TRIG_PIN, LOW);

        echo_pulse_duration_us = 0;
        pulse_active = true;
        last_trigger_ms = now;
    }

    if (pulse_active && echo_pulse_duration_us > 0) {
        float speed_m_s = SOUND_SPEED_BASE + SOUND_TEMP_COEFF * last_temperature_c + SOUND_HUM_COEFF * last_humidity_pct;
        float factor_cm_us = (speed_m_s * CM_PER_M / US_PER_S) / 2.0f;

        last_distance_cm = (echo_pulse_duration_us < ECHO_TIMEOUT_US) ? echo_pulse_duration_us * factor_cm_us : -1.0f;
        last_sensor_update_ms = now;

        if (last_distance_cm >= 0) {
            log("Distance OK | %.1f cm (pulse %lu µs)", last_distance_cm, echo_pulse_duration_us);
        } else {
            log("Ultrasonic timeout");
        }

        pulse_active = false;
    }

    if (pulse_active && (now - last_trigger_ms > 250)) {
        pulse_active = false;
        last_distance_cm = -1.0f;
        log("Ultrasonic watchdog timeout");
    }
}