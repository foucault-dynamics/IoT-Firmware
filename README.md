# Focault Dynamics IoT Firmware

Focault Dynamics IoT Firmware is an ESP32/PlatformIO prototype for a fault-tolerant intelligent energy metering system. It simulates energy meter readings on an ESP32-C3 SuperMini, sends the readings to a LilyGo substation using ESP-NOW, relays the payload over LoRa, and forwards the received data to an MQTT broker from the gateway.

## System Overview

```text
ESP32-C3 SuperMini meter node
        |
        | ESP-NOW
        v
LilyGo substation / relay
        |
        | LoRa
        v
LilyGo gateway
        |
        | MQTT
        v
MQTT broker
```

## Features

- Simulated meter readings for import kWh, export kWh, voltage and battery voltage.
- ESP-NOW communication from the SuperMini node to the LilyGo substation.
- RTC memory buffering on the SuperMini for unsent records.
- LoRa relay with acknowledgement handling and retry logic.
- MQTT publishing from the gateway.
- OLED status display on LilyGo devices.

## Hardware

- ESP32-C3 SuperMini
- LilyGo / ESP32 LoRa OLED board as the substation relay
- LilyGo / ESP32 LoRa OLED board as the gateway
- LoRa antennas
- MQTT broker

## Project Structure

```text
NEXTGEN_IEMS/
├── include/
│   └── SharedPayload.h
├── src/
│   ├── main-esp-now-supermini.cpp
│   ├── main-esp-now-lilyGo.cpp
│   ├── main-esp-now-gateway.cpp
│   └── secrets.h
├── platformio.ini
└── README.md
```

## PlatformIO Environments

| Environment | Device | Purpose |
|---|---|---|
| `supermini` | ESP32-C3 SuperMini | Simulates meter readings and sends them by ESP-NOW. |
| `lilygo` | LilyGo / ESP32 LoRa OLED board | Receives ESP-NOW packets and relays them by LoRa. |
| `gateway` | LilyGo / ESP32 LoRa OLED board | Receives LoRa packets, sends ACKs and publishes data to MQTT. |

## `secrets.h` Configuration

The project requires a local configuration file in the `src/` folder:

```text
src/secrets.h
```

Use the following format and update the values for your own setup:

```cpp
#pragma once

#define SECRET_MQTT_SERVER "broker.hivemq.com"
#define SECRET_MQTT_PORT 1883
#define SECRET_MQTT_TOPIC "your/mqtt/topic"

#define SECRET_MAC {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
#define SECRET_LORA_BAND 433E6
```

### Configuration Fields

- `SECRET_MQTT_SERVER`: MQTT broker address.
- `SECRET_MQTT_PORT`: MQTT broker port. Standard non-TLS MQTT usually uses `1883`.
- `SECRET_MQTT_TOPIC`: MQTT topic used by the gateway when publishing meter data.
- `SECRET_MAC`: MAC address of the LilyGo substation relay. Upload the `lilygo` firmware first, open the serial monitor, copy the printed MAC address, then paste it here.
- `SECRET_LORA_BAND`: LoRa frequency used by both LilyGo devices. Both devices must use the same frequency.

## Build and Upload

### Upload the LilyGo Substation Relay

```bash
pio run -e lilygo -t upload
pio device monitor -e lilygo
```

Copy the MAC address shown in the serial monitor and update `SECRET_MAC` in `src/secrets.h`.

### Upload the LilyGo Gateway

```bash
pio run -e gateway -t upload
pio device monitor -e gateway
```

Confirm that the gateway connects to the MQTT broker.

### Upload the ESP32-C3 SuperMini Meter Node

```bash
pio run -e supermini -t upload
pio device monitor -e supermini
```

The SuperMini will generate simulated meter data, send it by ESP-NOW and then enter deep sleep.

## Payload Format

The binary payload is defined in `include/SharedPayload.h`:

```cpp
struct Payload {
    uint32_t uid;
    uint32_t seq;
    float kwh_import;
    float kwh_export;
    float voltage;
    float battery_v;
    uint8_t community_id;
    uint8_t unit_id;
};

struct AckPayload {
    uint32_t uid;
    uint32_t seq;
};
```

All devices must use the same `SharedPayload.h` structure. If the structure is changed, rebuild and upload all environments again.

## Troubleshooting

### ESP-NOW Send Fails

- Check that `SECRET_MAC` is the LilyGo substation MAC address.
- Confirm both devices are powered on.
- Use serial monitor baud rate `115200`.

### LoRa Receive Size Mismatch

- Confirm all devices use the same `SharedPayload.h` file.
- Rebuild and upload all firmware after changing the payload structure.
- Confirm both LoRa devices use the same `SECRET_LORA_BAND`.

### MQTT Does Not Connect

- Check MQTT server and port.
- Confirm the broker accepts the connection.
- Avoid using MQTT wildcard characters such as `+` when publishing to a topic.
