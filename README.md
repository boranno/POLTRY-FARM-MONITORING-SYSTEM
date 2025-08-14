# Poultry Farm Management System (PFMS)

**Version:** v2.2 

**Author:** Boranno Golder 

**Board:** ESP32 S3 WROOM 

**Date:** 14 AUG 2025  


---

## 📌 Overview

The **Poultry Farm Management System (PFMS)** is an IoT-enabled environmental monitoring and control system for poultry houses, built on the ESP32 microcontroller.  
It monitors **temperature**, **humidity**, and **ammonia (NH₃)** levels, and automatically controls actuators (fan, heater, ventilation, siren) to maintain healthy conditions for poultry.

PFMS features both **online mode** (with cloud dashboard & email alerts) and **offline mode** (local display & control).

---

## ✨ Key Features

- **Environmental Monitoring**
  - Temperature & Humidity via **DHT22**
  - Ammonia concentration via **MQ-135**
- **Actuator Control (Automatic)**
  - Fan
  - Heater
  - Ventilation
  - Siren
- **Web & Mobile Dashboard**  
  - Real-time monitoring via **Blynk IoT** (web + Android/iOS)
- **Automated Alerts**
  - **Email notifications** for **Suboptimal** and **Dangerous** environmental conditions
- **Modes of Operation**
  - **Online mode**: Live data to cloud + dashboard + alerts
  - **Offline mode**: Full local control with live LCD updates
- **Non-blocking, staged reconnection system** to handle unstable networks
- **LCD User Interface**
  - Startup scrolling animation
  - Live sensor readings
  - State abbreviations: `OK` (Optimal), `!!` (Suboptimal), `XX` (Dangerous)

---

## 🛠 Hardware Requirements

| Component                  | GPIO Pin |
|----------------------------|----------|
| **DHT22 Sensor**           | 47       |
| **MQ-135 Analog Output**   | 4        |
| **Live Update Button**     | 12       |
| **Red LED**                | 38       |
| **Yellow LED**             | 37       |
| **Blue LED**               | 39       |
| **Blynk Upload Active LED**| 10       |
| **Blynk Upload Stopped LED**| 11      |
| **Fan Relay**              | 16       |
| **Heater Relay**           | 20       |
| **Ventilation Relay**      | 35       |
| **Siren Relay**            | 6        |

---

## 🌡 Environmental Thresholds

| Parameter       | Optimal Range      | Suboptimal Range        | Poor/Dangerous Range  |
|-----------------|--------------------|-------------------------|-----------------------|
| Temperature (°C)| 20–30              | 15–20 or 30–35          | <15 or >35            |
| Humidity (%)    | 50–70              | 30–50 or 70–80          | <30 or >80            |
| Ammonia (ppm)   | <10                 | 10–25                   | >25                   |

---

## 🌐 Blynk Cloud Integration

**Virtual Pin Mapping**:

| Virtual Pin | Data Sent                  |
|-------------|---------------------------|
| `V0`        | Temperature (°C)          |
| `V1`        | Humidity (%)               |
| `V2`        | Ammonia (ppm)              |
| `V3`        | State code (1, 2, 3)       |

---

## 📩 Email Notification Logic

- **Trigger:**  
  - Suboptimal conditions (State 2) — moderate deviations from thresholds  
  - Dangerous conditions (State 3) — extreme values risking poultry health  
- **Alert Content:**  
  - Current readings  
  - Farm state name & abbreviation  
  - Recommended corrective action  

> **NOTE:** Email notifications are configured via Blynk’s Eventor / Automations or external server integration. The device sends the data and triggers the event; Blynk handles email delivery.

---

## 🔄 Modes of Operation

### **Online Mode**
- Live cloud updates every 10 seconds
- Email alerts for suboptimal & dangerous conditions
- Viewable via Blynk mobile app and web dashboard

### **Offline Mode**
- No internet/cloud required
- Full actuator control + local display

---

## 🚦 Actuator Response

- **State 1 (OPTIMAL)**:  
  Blue LED ON, fan/heater/vent/siren OFF
- **State 2 (SUBOPTIMAL)**:  
  Yellow LED ON, targeted cooling/heating/venting, siren OFF
- **State 3 (POOR/DANGEROUS)**:  
  Red LED ON, siren ON, maximum ventilation/control engaged

---

## 💻 Software Setup

### Requirements
- Arduino IDE / PlatformIO
- ESP32 board support
- Libraries:
  - `BlynkSimpleEsp32`
  - `DHT sensor library`
  - `LiquidCrystal_I2C`

### Configuration
Edit in the code:

#define BLYNK_TEMPLATE_ID "TMPL6NkYtXFy4"

#define BLYNK_TEMPLATE_NAME "POULTRY FARM MANAGEMENT SYSTEM"

#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"


char ssid[] = "YOUR_WIFI_SSID";

char pass[] = "YOUR_WIFI_PASSWORD";



### Upload
1. Connect ESP32 via USB
2. Select board: **ESP32 S3 WROOM**
3. Upload code via Arduino IDE 


## 🚀 Future Enhancements
- Data logging to SD card
- SMS & push notification integration
- Manual actuator override from dashboard

---

## 📜 License
[Your chosen license, e.g., MIT]

---





