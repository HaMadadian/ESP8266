# ESP8266 HC-SR04 Ultrasonic Distance Sensor – Production-Ready Prototype
Author: Hamed Madadian(eng.madadian@gmail.com)


A professional-grade demonstration of interfacing an **HC-SR04** ultrasonic sensor with an **ESP8266** (NodeMCU / IdeaSpark board) using PlatformIO + Arduino framework. This project moves beyond basic hobbyist examples by emphasizing:

- Interrupt-driven timing (instead of blocking `pulseIn()`)
- Static memory usage only (no dynamic allocation)
- Voltage level shifting for safe 5V ↔ 3.3V operation
- Timeout handling and basic noise rejection
- Low-power considerations for future deep-sleep extensions


## Hardware Requirements

- ESP8266 board (NodeMCU v2 / ESP-12E module, e.g., IdeaSpark)
- HC-SR04 ultrasonic sensor
- Breadboard + jumper wires
- Resistors for voltage divider: 1 kΩ (series) + 2 kΩ (to GND) on ECHO pin
- USB cable for power/programming

### Wiring Diagram

| HC-SR04 Pin | ESP8266 Pin     | Notes                                                                 |
|-------------|-----------------|-----------------------------------------------------------------------|
| VCC         | VIN (5V from USB) | Provides 5V needed for reliable ultrasonic strength                   |
| GND         | GND             | Common ground                                                         |
| TRIG        | D1 (GPIO5)      | 3.3V trigger is sufficient (threshold ~2V)                            |
| ECHO        | D2 (GPIO4) via voltage divider | 5V → 3.3V divider required to protect ESP8266 GPIO (max ~3.6V)       |

**Voltage Divider for ECHO**:
- HC-SR04 ECHO → 1 kΩ resistor → junction point
- Junction point → ESP8266 D2 (GPIO4)
- Junction point → 2 kΩ resistor → GND

This gives: V_out ≈ 5V × (2k / (1k + 2k)) = ~3.33V — safe for ESP8266.

> **Schematic Tip**: Use a logic analyzer (Saleae, DSLogic, etc.) in production to verify pulse widths and edge timing on ECHO.

## Software Setup

- **IDE**: VS Code + PlatformIO
- **Framework**: Arduino (esp8266 core)
- **Board**: nodemcuv2 (or esp12e)
- **platformio.ini** example:

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200