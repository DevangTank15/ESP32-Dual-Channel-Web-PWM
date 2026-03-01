# ESP32 Web Controlled Dual Channel PWM

A real-time dual-channel PWM controller built on ESP32 using the LEDC hardware peripheral.

This project provides precise frequency and duty cycle control through both a web-based interface and physical buttons. It includes smart acceleration logic and automatic LEDC timer reconfiguration to ensure stable operation across frequency boundaries.

---

## 🚀 Features

* Dual independent PWM output channels
* Web-based real-time control
* Adjustable frequency (configurable MIN–MAX range)
* Adjustable duty cycle
* Smart physical button acceleration
* Automatic LEDC timer reconfiguration when crossing ~976 Hz boundary
* Centralized frequency control engine
* Clean layered firmware architecture

---

## 🧠 System Architecture

The firmware is structured into three logical layers:

### 1️⃣ Input Layer

* Physical button handling
* Web request handling

### 2️⃣ Control Layer

* Centralized frequency processing engine
* Smart acceleration logic (button only)
* Frequency boundary detection

### 3️⃣ Hardware Layer

* LEDC timer configuration
* Frequency and duty updates

This separation ensures:

* No duplicated logic
* Stable high-frequency switching
* Safe timer reconfiguration
* Clean scalability for future features

---

## 🌐 Web Interface

The web UI allows:

* Frequency increase / decrease
* Manual frequency input
* Duty cycle adjustment
* Channel selection
* Immediate application of changes

---

## ⚙ Hardware Requirements

* ESP32 (LEDC supported variant)
* External MOSFET driver (if switching higher voltages)
* Stable power supply
* 3.3V logic interface
