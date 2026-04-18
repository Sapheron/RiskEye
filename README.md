Risk Eye-🏭 Industrial Safety Monitoring System (IoT)

  Overview

The Industrial Safety Monitoring System is an IoT-based project built using ESP32 to monitor industrial environments in real-time.

It collects data from multiple sensors and detects unsafe conditions like high temperature, abnormal sound, or intrusion, and triggers alerts to prevent accidents.

Core Idea:
 Monitor → Analyze Risk → Trigger Safety Action

## ✨ Features

- 🌡️ Temperature Monitoring (DHT11)
- 🔊 Sound Level Detection (I2S Microphone)
- 📏 Machine Movement Detection (VL53L0X Sensor)
- 👤 Human Detection System (IR Sensor)
- 🚨 Emergency Stop (Push Button)
- 📡 Real-time Data via Firebase
- 📊 Live Monitoring Dashboard



## 🧠 Smart Safety Logic

- High Temperature + High Sound → Machine Failure Risk
- Machine Movement Detected → Safety Alert
- Human Detection → Danger Warning
- Button Press → Immediate Shutdown

System Flow:
Monitor → Analyze → Detect Risk → Trigger Alert

## 🏗️ Architecture

ESP32 collects data from sensors:
- DHT11 (Temperature)
- I2S Microphone (Sound)
- VL53L0X (Distance)
- Touch Sensor (Emergency)

⬇

Data sent to Firebase Realtime Database

⬇

Web Dashboard displays live data and alerts

## 📸 Dashboard Preview

![Dashboard 1](riskeye-monitoring-dashboard/d1.png)

![Dashboard 2](riskeye-monitoring-dashboard/d2.png)


## 🧰 Tech Stack

- Microcontroller: ESP32
- Sensors:
  - DHT11
  - I2S Microphone
  - VL53L0X
  - Touch Sensor
- Backend: Firebase Realtime Database
- Frontend: HTML, CSS, JavaScript
- Communication: WiFi


## 🔌 Wiring Connections

| Component        | ESP32 Pin |
|-----------------|----------|
| DHT11           | GPIO 15  |
| I2S Mic (WS)    | GPIO 25  |
| I2S Mic (SEL)   | GPIO 26  |
| I2S Mic (BCLK)  | GPIO 27  |
| VL53L0X SDA     | GPIO 21  |
| VL53L0X SCL     | GPIO 22  |
| ButtonSwitch    | GPIO 4   |



## ⚙️ Setup Instructions

1. Install Arduino IDE
2. Install ESP32 Board Package
3. Install required libraries:
   - DHT Sensor Library
   - Adafruit Unified Sensor
   - VL53L0X Library
   - Firebase ESP Client

4. Upload code to ESP32
5. Connect sensors properly
6. Configure Firebase credentials
7. Run dashboard (index.html)



## 📡 Firebase Structure

/safety-system
  /temperature
  /sound
  /movement
  /people
  /emergency



  ## 🚨 Alerts

- Temperature Warning
- Machine Movement High
- Human Detected
- Emergency Stop Activated

## 🚀 Future Improvements

- AI-based predictive safety system
- Mobile app integration
- SMS / Call alerts
- Cloud analytics dashboard
- Automatic mechine cut-off