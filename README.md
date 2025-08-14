# Poultry Farm Management System (PFMS)

**Version:** v2.2  
**Author:** Boranno Golder  
**Board:** ESP32  
**Date:** 2025  

---

## Overview
The **Poultry Farm Management System (PFMS)** is an ESP32-based IoT monitoring and control solution designed to ensure optimal poultry house conditions.  
It monitors **temperature**, **humidity**, and **ammonia (NH₃)** levels and automatically controls actuators (fan, heater, ventilation, siren) to maintain safe conditions.

PFMS can operate in **online mode** with Blynk IoT cloud connectivity or **offline mode** with full local display and control.

---

## Key Features
- **Multi-parameter monitoring**:
  - Temperature & humidity via **DHT22**
  - Ammonia via **MQ-135** gas sensor
- **Automatic control** of:
  - Fan
  - Heater
  - Ventilation fan
  - Siren alarm
- **WiFi + Blynk IoT**:
  - Remote monitoring via Blynk app/dashboard
  - `Live_Update_Review` button toggles IoT updates
  - Automatic reconnection system
- **Offline mode**:
  - Works without WiFi/Blynk
  - Displays readings locally on LCD
- **LCD user interface** with:
  - Startup scrolling animation
  - Live environmental readings
  - State abbreviation (OK / !! / XX)
- **Environmental classification**:
  - OPTIMAL, SUBOPTIMAL, POOR/DANGEROUS
- **Automatic actuator logic** based on sensor readings
- **Non-blocking connection handling** for stable performance

---

## Hardware Requirements

| Component                  | GPIO Pin |
|----------------------------|----------|
| **DHT22 Sensor Data**      | 47       |
| **MQ-135 Sensor Analog**   | 4        |
| **Live Update Button**     | 12       |
| **Red LED**                | 38       |
| **Yellow LED**             | 37       |
| **Blue LED**               | 39       |
| **Blynk Upload Ongoing LED**| 10      |
| **Blynk Upload Stopped LED**| 11      |
| **Fan Relay**              | 16       |
| **Heater Relay**           | 20       |
| **Ventilation Relay**      | 35       |
| **Siren**                  | 6        |

---

## Environmental Thresholds

| Parameter       | Optimal Range      | Suboptimal Range             | Poor/Dangerous Range  |
|-----------------|--------------------|------------------------------|-----------------------|
| Temperature (°C)| 20–30              | 15–20 or 30–35               | <15 or >35            |
| Humidity (%)    | 50–70              | 30–50 or 70–80               | <30 or >80            |
| Ammonia (ppm)   | <10                 | 10–25                        | >25                   |

---

## Blynk Virtual Pin Mapping

| Virtual Pin | Data Sent                  |
|-------------|---------------------------|
| V0          | Temperature (°C)          |
| V1          | Humidity (%)               |
| V2          | Ammonia (ppm)              |
| V3          | State code (1, 2, 3)       |

---

## Operating Modes
- **Online Mode**:  
  `Live_Update_Review` button is **ON**, system attempts to connect to WiFi & Blynk. Sends live data to cloud and updates IoT dashboard.
- **Offline Mode**:  
  `Live_Update_Review` button **OFF** or no network—system runs locally, controlling actuators and showing readings on LCD.

---

## Actuator Logic
- **Optimal**: Blue LED ON, all actuators OFF.
- **Suboptimal**: Yellow LED ON, selective fan/heater/ventilation control based on temperature, humidity & NH₃.
- **Poor/Dangerous**: Red LED ON, siren ON, emergency cooling/heating/ventilation activated.

---

## Software Setup

### 1. Requirements
- Arduino IDE or PlatformIO
- ESP32 board support
- Libraries:
  - `BlynkSimpleEsp32`
  - `DHT sensor library`
  - `LiquidCrystal_I2C`

### 2. Configuration
In the code:
