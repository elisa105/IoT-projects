# IoT Projects on Zephyr RTOS

This repository contains my IoT projects developed during my internship (Jul–Aug 2025).  

## Projects Overview
- STM32 Nucleo applications using GPIO, UART, I²C, and sensors.
- LoRa point-to-point communication and private LoRaWAN network prototype (ChirpStack, SX1262 HAT).
- Sensor data collection and visualization with InfluxDB and Grafana.

## Project Visuals & Documentation
A visual overview of the system's architecture, network configuration, and data monitoring can be found in the **[images/](./images/)** folder. 

Specifically, the folder contains:
* **ChirpStack Dashboards:** Screenshots of the private LoRaWAN network management and device telemetry (`chirpstack.png`).
* **Data Visualization:** Real-time dashboards in Grafana and sensor data logs in InfluxDB (`grafana.png`, `influx.png`).

## Structure
- `abp_lorawan/` : LoRaWAN ABP examples
- `chirpstack-docker/` : ChirpStack private network setup
-  `images/` : Screenshots, architectural diagrams, and presentation visuals.
- `lora/` : LoRa point-to-point examples
- `lorawan_adt7410/`, `lorawan_scd41/` : Sensor projects
- `mylorawan/` : Additional LoRaWAN tests
- `sensors/`, `sensor_scd41/` : Sensor integrations

## Requirements
- Zephyr RTOS
- STM32 toolchain
- LoRa SX1262 hardware (for communication projects)
- InfluxDB & Grafana for visualization
