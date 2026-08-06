# `sx127x`

Minimal SX1276/77/78/79 LoRa driver for ESP-IDF, used by the substation and the
gateway.

```cmake
idf_component_register(SRCS "sx127x.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_spi esp_driver_gpio
                       PRIV_REQUIRES esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow)
```

`REQUIRES` lists what the public header needs (`spi_master.h`, `gpio.h`);
`PRIV_REQUIRES` lists what only the implementation needs (`esp_timer`, for the
transmit deadline). Splitting them keeps the dependency that consumers inherit
as small as it actually is.

## What it replaces and why

It replaces the Arduino library `sandeepmistry/LoRa`, which has no ESP-IDF
equivalent. The modem settings written to the chip reproduce that library's
configuration bit for bit, so a device ported to ESP-IDF stays on the air with
the same spreading factor, bandwidth, coding rate, preamble and sync word as one
still running the old firmware.

## Scope

Deliberately narrow: half-duplex, explicit header, blocking send, polled
receive. That is the whole feature set the relay and the gateway use. No FSK
mode, no channel activity detection, no interrupt-driven receive.

## API

```c
esp_err_t sx127x_init(const sx127x_config_t *cfg, sx127x_handle_t *out_dev);
esp_err_t sx127x_send(sx127x_handle_t dev, const void *data, size_t len);
esp_err_t sx127x_start_receive(sx127x_handle_t dev);
esp_err_t sx127x_poll_receive(sx127x_handle_t dev, void *buf, size_t buf_size,
                              size_t *out_len, sx127x_rx_info_t *info);
esp_err_t sx127x_standby(sx127x_handle_t dev);
```

`sx127x_handle_t` is an opaque pointer. The struct behind it lives in the `.c`
file, so callers cannot reach into SPI handles or cached state, and the internals
can change without touching any firmware.

`sx127x_config_t` carries the SPI host, the five GPIOs, and the radio settings
(frequency, spreading factor, bandwidth, coding rate, preamble length, sync
word, TX power, CRC on/off). Configuration is passed as one struct rather than a
dozen setters, so a half-configured radio is not a reachable state.

A handle is **not** thread safe. Drive it from a single task.

## Behaviour worth knowing

### Init verifies the chip

`sx127x_init` resets the radio, then reads `REG_VERSION` and refuses to continue
unless it reads back `0x12`. That turns "SPI is miswired or the pin map is
wrong" into a clear boot-time error instead of a radio that appears to configure
fine and never receives anything. Init also unwinds cleanly on any failure: the
SPI device, the SPI bus and the allocation are all released.

### Continuous receive, not re-armed single receive

`sx127x_start_receive` puts the modem into `RX_CONTINUOUS` and leaves it there.
The Arduino firmware re-armed a single receive after each frame, which left a
window between reading a frame and re-arming where an inbound frame could be
lost. In continuous mode the modem never leaves RX, so polling frequency affects
latency but not whether a frame is caught.

### Polling instead of DIO0

Both transmit and receive poll the radio's `REG_IRQ_FLAGS` over SPI rather than
taking a GPIO interrupt on DIO0, which matches the previous firmware. The
transmit wait uses `vTaskDelay(1)` between polls, so the idle task runs and the
watchdog is fed while a frame is on the air. Transmit gives up after
`TX_TIMEOUT_MS` (4 s), far above the roughly 0.5 s airtime at SF12/BW125.

### 63-byte payload limit

The SPI bus is initialised with DMA disabled, which caps a single transfer at
the 64-byte hardware FIFO. One byte of each transfer is the register address,
leaving 63 bytes of LoRa payload. `SX127X_MAX_PAYLOAD` is 63 and `sx127x_send`
returns `ESP_ERR_INVALID_SIZE` rather than truncating. Both frames in this
project (26 and 8 bytes) sit well under the limit, and disabling DMA removes the
DMA-capable-buffer requirement entirely.

### Distinct error returns from `poll_receive`

| Return | Meaning |
|---|---|
| `ESP_OK` | A CRC-valid frame was read; `*out_len` is its length |
| `ESP_ERR_NOT_FOUND` | Nothing waiting |
| `ESP_ERR_INVALID_CRC` | A corrupt frame arrived and was discarded |
| `ESP_ERR_INVALID_SIZE` | A frame arrived that does not fit the caller's buffer |
| `ESP_ERR_INVALID_STATE` | Called without `sx127x_start_receive` first |

Callers can tell "quiet channel" from "interference" from "protocol mismatch"
without guessing, which is what the logs on both LilyGo devices report. The optional `sx127x_rx_info_t` gives RSSI in dBm and SNR in dB
for the last frame; RSSI uses the low-frequency or high-frequency port offset
depending on whether the carrier is below or above 525 MHz.

### Derived settings

`LowDataRateOptimize` is enabled automatically when the symbol duration exceeds
16 ms, which the datasheet requires to stop crystal drift smearing a symbol
across FFT bins. Spreading factor, coding rate and TX power are clamped to their
valid ranges instead of being written through as invalid register values, and
SF6's special implicit-header detection settings are handled.

TX power uses the PA_BOOST pin only (2 to 17 dBm), matching the previous
configuration; the +20 dBm high-power mode is not used. Overcurrent protection
is set to 100 mA.
