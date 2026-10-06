# IoT Current Measure Station

An energy-efficient, autonomous IoT current monitoring station designed for long-term AC current measurement using an ESP8266 and an SCT-013 non-invasive sensor, built on the **ESP8266 RTOS SDK (ESP-IDF style)**.

---

## 🛠️ Hardware Components

- **MCU:** ESP8266 Pro D1 Mini module
- **Sensor:** Non-invasive AC Current Sensor (SCT-013 series)
- **Burden Resistor:** $56\,\Omega$
- **DC Offset Bias Dividers:** $2\times 10\,\text{k}\Omega$ Resistors (with decoupling capacitor)
- **Power Source:** 3.7V 1000mAh LiPo / 18650 Battery

---

## 🏗️ Firmware Architecture

The firmware is modularized into custom components built upon the **ESP8266 RTOS SDK** framework:

- **Network Manager (`network_manager`):** Handles TCP/IP stack initialization, Wi-Fi STA connection, and blocking SNTP clock synchronization.
- **mDNS Keep-Alive Task:** Dedicated FreeRTOS background task ensuring hostname resolution (`esp8266_000.local`) remains active during Light Sleep cycles.
- **Local Data Store (`data_store`):** SPIFFS-backed circular buffer storing timestamped measurements ($I_{\text{RMS}}$) during network outages or long sleep intervals.
- **Current Measurement (`current_read`):** Performs ADC sampling, DC offset removal, and RMS current calculation for the SCT-013 sensor.
- **Power Management & Deadband Logic:** Operates with `WIFI_PS_LIGHT_SLEEP` combined with a **Deadband ($\Delta I \ge 0.5\,\text{A}$)** and **Heartbeat (5 min timeout)** trigger strategy to minimize Flash writes and radio duty cycles.
- **OTA Manager (`ota_update`):** Supports background firmware updates over HTTP/Web.

---

## 🐳 Backend Stack (Docker Containerized)

Orchestrated on an Ubuntu server via `docker-compose`:

- **MQTT Broker (`eclipse-mosquitto:2.0`):** Ingests batch JSON payloads from the ESP8266.
- **Data Ingestion (`telegraf:1.28`):** Parses incoming JSON batch arrays (`json_v2` plugin) and writes time-series metrics directly to InfluxDB.
- **Time-Series Database (`influxdb:2.7`):** Stores historical current measurements and battery voltage metrics.
- **Visualization Dashboard (`grafana:latest`):** Displays real-time current consumption curves, battery status, and aggregated historical power metrics via Flux queries.

---

## 📌 Version History

### **Version 0.2 (Current)**
- **Event-Driven Logging:** Implemented Deadband ($\Delta I$) and Heartbeat trigger strategy to save energy and protect Flash memory.
- **Local Persistence & SNTP:** Added SPIFFS-based `data_store` and sync-blocking SNTP clock initialization (`BRT3` timezone).
- **Power Optimization:** Enabled `WIFI_PS_LIGHT_SLEEP` with mDNS background keep-alive task (~1.5 mA average current, achieving ~23-28 days autonomy on a 1000 mAh cell).
- **Containerized Backend:** Deployed full Docker Compose pipeline (Mosquitto $\to$ Telegraf $\to$ InfluxDB v2 $\to$ Grafana).

### **Version 0.1**
- Core system baseline and module separation.
- Wi-Fi provisioning and continuous RMS current sampling.
- Basic telemetry transport pipeline over MQTT/HTTP.

---

## 🚀 Roadmap & Next Steps

1. **Firmware Batch MQTT Draining:**
   - Implement the C-based driver on the ESP8266 to read pending records from SPIFFS `data_store`, serialize them into JSON batch arrays, and publish to the Mosquitto broker at 1-hour intervals or upon buffer threshold.

2. **Ultra-Low Power Optimization:**
   - Explore Deep-Sleep duty cycling for long-term deployments.
   - Hardware adjustments: Replace LDO with ultra-low $I_q$ regulator (e.g., MCP1700), scale bias resistors to $100\,\text{k}\Omega$, aiming for $>2$ months of autonomy.

3. **3D Enclosure Fabrication:**
   - Design a custom 3D enclosure (CAD) tailored for the D1 Mini, current sensor jack, and battery holder.