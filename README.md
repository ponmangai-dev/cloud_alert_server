# Accident Detection & Cloud Alert System — Receiver-End Pipeline

Cloud-based alert pipeline for an IoT accident detection system designed for connectivity-deprived remote areas (e.g., pilgrimage routes like Sabarimala), where LoRa-transmitted accident alerts from a remote sensor network are received and converted into real-time SMS notifications with location data.

## System Architecture
- **ESP32-CAM (Detection Unit):** Runs an Edge Impulse FOMO object-detection model on live camera frames to detect vehicle accidents, overlays bounding boxes, serves a live MJPEG stream + HTML dashboard, and reports status ("ACCIDENT"/"NORMAL") via UART
- **ESP32 (Main Transmitter):** Fuses camera status (UART), GPS location (TinyGPSPlus), and ADXL345 accelerometer readings (tamper/position detection) into a single packet, broadcast via LoRa every ~800ms
- **ESP32 (Receiver):** Parses incoming LoRa packets, extracts GPS coordinates, and triggers this cloud pipeline specifically when an accident status is detected
- **Cloud Pipeline (this repo):** Flask backend on Render.com processes the alert and sends an SMS via Twilio, including GPS location

## My Role
Independently designed and built the receiver-end cloud alert pipeline:
- Flask backend (`app.py`) deployed on Render.com, exposing a live HTTPS endpoint (`/alert`)
- Twilio API integration to send real-time SMS alerts, including embedded GPS location as a Google Maps link
- Secured credentials using environment variables rather than hardcoding
- Deployed using Gunicorn as the production WSGI server
- Programmed the receiver-end ESP32 to call this endpoint upon receiving a LoRa alert signal

*(Camera detection and transmitter firmware were developed by teammates as part of this group project; included here for full system context.)*

## Tech Stack
- Python (Flask), Render.com, Twilio API, Gunicorn
- ESP32 (x3: camera, transmitter, receiver)
- LoRa (long-range communication)
- Edge Impulse / FOMO (on-device object detection)
- ADXL345 accelerometer, TinyGPSPlus (GPS)

## API Endpoints
- `POST /alert` — accepts JSON payload (`message`, `latitude`, `longitude`), sends an SMS with location link via Twilio
- `GET /` — health check, confirms server is running

## Repository Structure
```
app.py                                  - Flask backend, Twilio SMS integration with GPS location
requirements.txt                        - Python dependencies
firmware/
├── esp32_cam_detection.ino             - Camera unit: FOMO accident detection, live stream, UART status output
├── esp32_main_transmitter.ino          - Transmitter: fuses camera status, GPS, accelerometer data, sends via LoRa
└── esp32_lora_receiver.ino             - Receiver: parses LoRa packet, triggers cloud alert on accident detection
```