# IoT Current Measure Station

An IoT-based current monitoring station designed for real-time AC current measurement using an ESP8266 and a non-invasive current sensor, built on the **ESP8266 RTOS SDK (ESP-IDF style)**.

---

## 🛠️ Hardware Components

- **MCU:** ESP8266 Pro D1 Mini module
- **Sensor:** Non-invasive AC Current Sensor (SCT-013 series)
- **Burden Resistor:** $56\,\Omega$
- **DC Offset Bias Dividers:** $2\times 10\,\text{k}\Omega$ Resistors (with decoupling capacitor)

---

## 🏗️ Firmware Architecture

The firmware is modularized into custom components built upon the **ESP8266 RTOS SDK** framework:

- **Network Manager (`network_manager`):** Handles TCP/IP stack initialization, Wi-Fi STA connection, and mDNS responder.
- **MQTT Client (`mqtt_client`):** Manages telemetry publication and command subscription over MQTT.
- **Current Measurement (`current_read`):** Performs ADC sampling, DC offset removal, and RMS current calculation for the SCT-013 sensor.
- **OTA Manager (`ota_update`):** Supports background firmware updates over HTTP.

---

## 📌 Version History

### **Version 0.1 (Current)**
- Core system baseline and module separation.
- Wi-Fi provisioning and continuous RMS current sampling.
- Basic telemetry transport pipeline over MQTT/HTTP.

---

## 🚀 Roadmap & Next Steps

1. **Local Data Persistence (NVS/Flash Logging):**
   - Implement SPIFFS/NVS storage buffering to retain measurement data during Wi-Fi outages and ensure data consistency.

2. **Backend Server & Analytics Platform:**
   - Build a central backend service (Node.js / Rust / Python) for ingestion, persistent storage (Time-Series DB), and dashboard visualization (e.g., Grafana / Web Dashboard).

3. **Low-Power & Battery Optimization:**
   - Transition from continuous active mode to **Deep-Sleep duty cycling**.
   - Hardware adjustments: Replace LDO with ultra-low $I_q$ regulator (e.g., MCP1700), scale bias resistors to $100\,\text{k}\Omega$, and target **$\ge 2$ months of autonomy** on a single 18650 Li-Ion cell.

4. **3D Enclosure & Enclosure Fabrication:**
   - Design a custom 3D enclosure (CAD) tailored for the D1 Mini, current sensor jack, and 18650 battery holder, followed by FDM 3D printing.