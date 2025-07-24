# Poultry Farm Management System

**Version:** v2.1  
**Author:** Boranno Golder
**Date:** 2025  

## Overview

This project implements a robust **Poultry Farm Environmental Monitoring System** using the ESP32 microcontroller. It continuously monitors **temperature**, **humidity**, and **ammonia (NH3)** levels to ensure optimal poultry health and welfare. It features a responsive LCD display and LED status indicators along with cloud connectivity via the **Blynk IoT platform** for remote monitoring.

## Features

- **Environmental Monitoring:**  
  - Temperature and humidity via DHT22 sensor  
  - Ammonia concentration via MQ-137 sensor  
- **FreeRTOS-Based Multitasking:** Dedicated sensor reading task for smooth, reliable data acquisition.  
- **Blynk Cloud Integration:** Remote data streaming and visualization through smartphone or web interfaces.  
- **User Interface:**  
  - 16x2 I2C LCD with a scrolling startup animation  
  - LED statuses signaling environmental condition (Optimal, Suboptimal, Poor/Dangerous)  
  - LEDs indicating Blynk connection/upload status  
- **Calibration:** Long press of calibration button to calibrate the ammonia sensor in clean air conditions.  
- **WiFi Connectivity:** Includes visual connection animation and handling of connection loss.

## Hardware Requirements

- ESP32 Development Board  
- DHT22 Temperature and Humidity Sensor  
- MQ-137 Ammonia Gas Sensor  
- 16x2 I2C LCD Display (address 0x27)  
- LEDs: Red, Yellow, Blue for environment status; additional LEDs for Blynk upload status  
- Push Button for sensor calibration (connected to GPIO 18)  
- Appropriate resistors and wiring  

## Pin Configuration

| Component                | GPIO Pin | Description                     |
|--------------------------|----------|--------------------------------|
| DHT22 Sensor             | 26       | Data pin                       |
| MQ-137 Sensor Analog     | 33       | Analog input for NH3 detection |
| Calibration Button       | 18       | Input with pull-up             |
| Red LED                 | 4        | Poor/Dangerous environment LED |
| Yellow LED              | 16       | Suboptimal environment LED     |
| Blue LED                | 17       | Optimal environment LED        |
| Blynk Upload Ongoing LED | 15       | Lights when uploading data     |
| Blynk Upload Stopped LED | 2        | Lights when no upload activity |

## Software Setup

### Prerequisites

- Arduino IDE (or compatible) with ESP32 board support installed  
- Install libraries:  
  - `Blynk` (BlynkSimpleEsp32)  
  - `DHT sensor library`  
  - `LiquidCrystal_I2C`  

### Configuration

1. Set your WiFi SSID and password in the source file (`ssid[]` and `pass[]`).  
2. Insert your Blynk authentication token in the `BLYNK_AUTH_TOKEN` variable.  
3. Confirm pin definitions match your hardware wiring.  
4. Upload the code to the ESP32 board.

## Usage

- Power on the device. The LCD performs a scrolling "POULTRY FARM MONITOR" startup animation.  
- The system attempts to connect to WiFi with an animation on the LCD.  
- Upon successful connection, Blynk service automatically starts for remote monitoring.  
- The sensor task runs continuously, updating readings every 2 seconds locally.  
- Data is pushed to Blynk every 10 seconds.  
- Environmental status is indicated by LEDs (blue = optimal, yellow = suboptimal, red = poor/dangerous).  
- Press and hold the calibration button for 5 seconds in clean air to calibrate the MQ-137 sensor baseline.  
- Connection status LEDs show Blynk upload activity.

## Environmental Condition Thresholds

| Parameter       | Optimal Range | Suboptimal Range         | Poor/Dangerous Range  |
|-----------------|---------------|-------------------------|----------------------|
| Temperature (°C)| 20 to 30      | 15 to 20 or 30 to 35    | < 15 or > 35         |
| Humidity (%)    | 50 to 70      | 30 to 50 or 70 to 80    | < 30 or > 80         |
| Ammonia (ppm)   | < 10          | 10 to 25                | > 25                 |

## Troubleshooting

- **WiFi Connection Failed:**  
  Check credentials, signal strength. If unavailable, system runs local-only mode without Blynk connection.  
- **Sensor Reading Failed:**  
  DHT sensor may need replacement or wiring check if NAN values appear.  
- **Calibration Issues:**  
  Ensure sensor is in a clean air environment for accurate calibration; hold the calibration button for full 5 seconds.  
- **Blynk Data Not Updating:**  
  Confirm Blynk token is correct and app is configured to receive virtual pins V0-V3.

## License

[Specify your license here, e.g., MIT License, GPLv3 etc.]

## Acknowledgments

- Blynk IoT Platform for seamless cloud integration  
- Arduino community projects for sensor libraries and development ideas

## Contact

For questions or support:  
- Email: borannogolder@gmail.com
- GitHub: [github.com/yourusername/yourrepo](https://github.com/yourusername/yourrepo)  

