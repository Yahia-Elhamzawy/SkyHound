# 🚁 SkyHound (FALCON) - Unified Drone Command System

![SkyHound Cover](https://via.placeholder.com/1000x300.png?text=SkyHound+Drone+System)

**SkyHound** (also referred to as FALCON) is an advanced, fully integrated drone ecosystem. It combines custom flight control logic running on an ESP32, high-definition wireless video streaming via an ESP32-CAM, and a smart Command Center running locally on a Python Flask server powered by an integrated local AI (Ollama). 

This repository contains all the code required to build, configure, and fly the drone, as well as the server to control it and use AI vision on the camera feed.

---

## 📑 Table of Contents
1. [System Architecture](#1-system-architecture)
2. [Hardware Requirements](#2-hardware-requirements)
3. [Wiring & Pinout](#3-wiring--pinout)
4. [Component 1: Drone Flight Controller](#4-component-1-drone-flight-controller)
5. [Component 2: ESP32-CAM Module](#5-component-2-esp32-cam-module)
6. [Component 3: Command Center (Flask Server)](#6-component-3-command-center-flask-server)
7. [Installation & Setup](#7-installation--setup)
8. [API Endpoints](#8-api-endpoints)
9. [Safety & Security](#9-safety--security)

---

## 1. System Architecture
SkyHound relies on a local WiFi network created by the flight controller. 

* **The Drone Controller (ESP32)** acts as a **WiFi Access Point (AP)** with the SSID `SkyHund` (IP: `192.168.4.1`).
* **The Camera Module (ESP32-CAM)** connects to the AP as a **Station** (typically IP: `192.168.4.2`).
* **The Command Center (PC)** connects to the AP and runs a Flask Server, allowing the user to view telemetry, video feed, and control the drone simultaneously via a Web Interface. It also connects to a local Ollama AI instance to analyze the drone's vision.

---

## 2. Hardware Requirements
* **Main Controller:** ESP32 NodeMCU (or similar ESP32 Dev Board).
* **Vision Module:** ESP32-CAM (AI-Thinker module recommended).
* **IMU Sensor:** MPU9250 (Accelerometer, Gyroscope, Magnetometer) connected via I2C.
* **Motors & ESCs:** 4x Brushless Motors and Electronic Speed Controllers (ESCs).
* **Power:** LiPo Battery (3S or 4S depending on motors) with a step-down converter (BEC) to provide stable 5V for the ESP32 boards.
* **Host PC:** A computer to run the Command Center Server and Ollama for AI features.

---

## 3. Wiring & Pinout

### ESP32 Drone Controller
| Component | ESP32 Pin | Note |
| :--- | :--- | :--- |
| **MPU9250 SDA** | `GPIO 21` | Standard I2C Data |
| **MPU9250 SCL** | `GPIO 22` | Standard I2C Clock |
| **ESC 1 (Front-Left)** | `GPIO 13` | PWM Signal |
| **ESC 2 (Front-Right)**| `GPIO 32` | PWM Signal |
| **ESC 3 (Back-Left)**  | `GPIO 14` | PWM Signal |
| **ESC 4 (Back-Right)** | `GPIO 27` | PWM Signal |

> **Warning:** Ensure that the ESC ground wires are securely connected to the ESP32 ground to prevent signal noise.

---

## 4. Component 1: Drone Flight Controller
**Location:** `/drone_controller`

This is the brain of the drone. It reads orientation data from the MPU9250 and adjusts motor speeds using a custom PID controller to keep the drone stable. 
* **PID Loop:** The code features independent PID variables for Pitch, Roll, and Yaw (Rate). 
* **Fail-Safe:** Implements a disconnect safety feature. If no command is received within 5 seconds (`DISCONNECT_TIMEOUT`), the motors automatically cut off to prevent flyaways.
* **Double-Tap Speed System:** Offers speed tiers for better control (e.g., 80% throttle limit vs 100% full speed).

---

## 5. Component 2: ESP32-CAM Module
**Location:** `/ESP32_Drone_Cam`

This module is solely responsible for vision. It offloads the heavy processing of video streaming from the main flight controller.
* **Streaming:** Serves an MJPEG stream over HTTP (Port 81).
* **Snapshot:** Captures high-res JPEG images on demand for AI analysis.
* **Flash LED:** Can control the onboard flash LED for night vision or signaling.

---

## 6. Component 3: Command Center (Flask Server)
**Location:** `/server`

A unified dashboard written in Python (Flask) that provides:
1. **Live HUD (Head-Up Display):** Shows Pitch, Roll, Yaw, and Throttle in real-time.
2. **Video Feed:** Embeds the ESP32-CAM stream.
3. **GEO (Tactical AI Vision):** Connects to **Ollama** (e.g., `gemma3` or `gemma2:2b` models) on `localhost:11434`. The user can ask questions like *"What do you see?"* and the AI will analyze the live snapshot from the drone and reply, acting as a tactical spotter.

---

## 7. Installation & Setup

### Flashing the Microcontrollers
1. Open Arduino IDE.
2. Install the necessary libraries (`WiFi`, `WebServer`, `ESP32Servo`, `MPU9250_WE`).
3. Set your secure WiFi password in both `.ino` files by modifying the `password` / `DRONE_PASS` variable.
4. Flash `drone_controller.ino` to the ESP32.
5. Flash `ESP32_Drone_Cam.ino` to the ESP32-CAM.

### Running the Command Center
Ensure Python 3.9+ is installed.

```bash
# Navigate to the server directory
cd server

# Install dependencies
pip install -r requirements.txt

# Make sure your PC is connected to the 'SkyHund' WiFi Network.

# Start the server
python server.py
# (Alternatively, you can just run run_server.bat from the root)
```

Access the dashboard at: `http://localhost:5000`

---

## 8. API Endpoints

### Drone Controller (`192.168.4.1`)
* `GET /` : Serves a basic HTML fallback controller interface.
* `GET /data` : Returns JSON telemetry (Pitch, Roll, Yaw, Throttle, Testing Mode status).
* `POST /control` : Receives flight commands (Target Pitch, Roll, Yaw, Throttle) from the Command Center.

### ESP32-CAM (`192.168.4.2`)
* `GET :81/stream` : Live MJPEG video stream.
* `GET /capture` : Returns a single JPEG frame.
* `GET /status` : Returns camera configuration and status.
* `GET /control?var=X&val=Y` : Adjusts camera settings (brightness, contrast, flash).

---

## 9. Safety & Security
* **No Hardcoded Keys:** The code has been audited. Passwords should be injected by the user before flashing.
* **Local AI:** The AI Vision system relies entirely on Ollama running on your local machine. No images or flight data are sent to the cloud. This guarantees complete privacy and zero latency reliant on external internet access.
* **Propeller Safety:** ALWAYS remove propellers when testing the code on your desk. Only attach propellers when in an open, safe outdoor area.

---
*Created by Antigravity for the SkyHound Ecosystem.*
