# CV node: getting started {#cv_start}

The CV node reads a meter's display with a camera. Two boards do the work:

```text
meter display ──► ESP32-CAM ──Wi-Fi (HTTP /json)──► C3 (CV node) ──ESP-NOW──► substation ──► …
                  AI-on-the-edge-device             end_node image
```

The camera runs [AI-on-the-edge-device](https://github.com/jomjol/AI-on-the-edge-device),
which takes the picture and recognises the digits and dials itself. The C3 hosts
its own Wi-Fi network for the camera to join, asks it for the reading over HTTP,
and sends it on like any other end node. This page takes you from a blank camera
to readings arriving at the substation. The general tools and the rest of the
network are in [Getting started](GETTING_STARTED.md).

## What you need {#cv_need}

| Part | Notes |
|---|---|
| ESP32-C3 Supermini | The CV node, flashed with the `end_node` image |
| ESP32-CAM (AI-Thinker) | The camera |
| ESP32-CAM-MB programmer board | For flashing the camera and powering it over USB |
| microSD card, FAT32 | Holds the camera's config, models and web UI |
| Power for the camera | The EE team's CV Head v1.0 powered from the C3, or the programmer board on USB. See [Power the camera](#cv_power) |
| The camera firmware repo | [foucault-dynamics/AI-on-the-edge-FD](https://github.com/foucault-dynamics/AI-on-the-edge-FD), the team's fork |

## 1. Prepare the camera's SD card {#cv_sd}

1. Copy everything inside the fork's `sd-card/` folder to the root of the card.
2. Edit `wlan.ini` at the root of the card, so the camera joins the C3's network
   at a fixed address:

   ```ini
   ssid = "AMR_Base"
   password = "AMR12345"
   ip = "192.168.4.2"
   gateway = "192.168.4.1"
   netmask = "255.255.255.0"
   ```

   These match the C3's defaults: `ap_ssid`, `ap_pass` and `cam_host`. Change
   them on both sides together, or the camera never joins or is never found.
   The C3 hands out DHCP addresses from `192.168.4.100` up, so the camera's `.2`
   never clashes with a phone or laptop that joins too.

3. For a demo without a meter, set up demo mode. The camera then replays 12
   photos of a water meter instead of taking pictures, and each photo's file name
   is its true reading (530.07077 to 531.82235).
   - Copy `ref0.jpg`, `ref1.jpg`, `reference.jpg` and `prevalue.ini` from the
     card's `demo/` folder into its `config/` folder.
   - Copy `docs/hw-tests/2026-10-13-cv-node/camera/config.ini.tuned` from this
     repo to the card as `config/config.ini`. Use it instead of the demo's own
     `config.ini`: the demo's reads one digit and one dial, so it reports about
     1.7 instead of 530, and uses a dial model that misreads most needle
     positions. The tuned one reads all 5 digits and 4 dials, uses the
     `ana-cont_1300_s2` dial model, and sets `AnalogToDigitTransitionStart` to 7.5
     for this meter's early rolling last digit. With it, all 12 demo photos read
     within ±0.005.

For a real meter, keep the stock `config/config.ini` (`Demo = false`) and set
the digit and dial windows in the camera's web UI once it is running. See
[Tuning for a real meter](#cv_tuning).

## 2. Flash the camera {#cv_flash_cam}

Put the card in the camera, the camera on its programmer board, and the board on
USB. Then, from the fork's `code/` folder:

```sh
pio run -e esp32cam -t upload
```

If it stops at `Failed to connect to ESP32`, the programmer board is not putting
the camera into its bootloader by itself, as with the board this was written
on. Do it by hand: hold **IO0** on the programmer board, tap **RST** on the
camera, release IO0, and run the upload again. Afterwards tap **RST** once more,
with no serial monitor open, to boot the new firmware.

Building on a new machine rewrites `code/dependencies.lock` and downloads into
`code/managed_components/`. Do not commit those along with your own changes.

## 3. Flash and start the C3 {#cv_c3}

```sh
pio run -e end_node -t upload
pio device monitor -p /dev/ttyACM0 -b 115200 --dtr 0 --rts 0
```

- `-p` matters when the camera's programmer board is plugged in too. Without
  it, the monitor may open the camera's port instead of the C3's.
- `--dtr 0 --rts 0` keeps the C3 from landing in download mode when it resets.
  If it ever does (the monitor shows `waiting for download`), press its RST
  button, or unplug and replug it without holding BOOT.

The C3 waits for the monitor, then for a role key. Press `3`. The first time,
set the channel to the substation's and restart:

```text
set espnow_chan 6
reboot
```

then press `3` again. The C3 does this every boot: it does not start the CV node
on a charger or with no monitor open, and closing the monitor resets it back to
the menu.

The CV node's settings, all optional:

| Key | Default | Meaning |
|---|---|---|
| `espnow_chan` | `1` | Channel for its Wi-Fi network and ESP-NOW. Must match the substation, which defaults to `6` |
| `ap_ssid`, `ap_pass` | `AMR_Base`, `AMR12345` | The network the camera joins. Must match the camera's `wlan.ini` |
| `cam_host` | `192.168.4.2` | The camera's address. Must match the camera's `wlan.ini` |
| `cam_flow` | `main` | The number name in the camera's `config.ini` to read as import |
| `cam_flow_exp` | empty | A second number name to read as export. Empty sends export as 0 |
| `poll_ms` | `30000` | Time between readings, in ms. `60000` matches the camera's one round a minute |

All keys are in [NVS config keys](../lib/config/nvs_config/NVS_KEYS.md).

## 4. Power the camera {#cv_power}

Readings always travel over Wi-Fi, so the camera only needs power.

- **From the C3, through the EE team's CV Head v1.0.** The carrier takes 5V and
  GND from the C3. It has no data lines. The camera draws current spikes of
  300 to 500 mA when its Wi-Fi transmits, which is near the limit for this
  supply: with a loose contact the camera browns out and reboots in a loop, and
  its white flash LED fires at every boot. Keep both wires firmly seated. A
  470 to 1000 µF capacitor across the camera's 5V and GND makes it more robust.
- **On the programmer board, on a USB port or charger.** The steadiest option,
  and it keeps the camera's serial log available.

The camera restarts whenever its power does, so expect about a minute before its
first value after plugging in.

## 5. What you should see {#cv_see}

On the C3:

```text
[BOOT] module type 3
[WifiRadio] AP started
[WifiRadio] SSID: AMR_Base
[WifiRadio] AP IP: 192.168.4.1
[WifiRadio] Channel: 6
[Link] Beacon from F0:24:F9:92:FB:C0, joining      only with the substation up
[Link] Joined substation F0:24:F9:92:FB:C0
[HttpBus] No station joined the AP yet, skipping request.
[CV] Meter check failed, retrying in 5 s           until the camera answers
[CV] Ready
[CV] import=530.074 export=0.000 seq=1683
```

`[CV] Ready` prints once the camera answers its first request, and the first
reading follows straight away, then one per `poll_ms`. Right after the camera
boots it may answer before its first round has finished, which shows as
`Flow "main" has no value yet` for about a minute. In demo mode the value steps
through the 12 photos once a minute, from 530.07 up to 531.82, then starts again
at 530.07.

## 6. Watch the camera {#cv_watch}

- **Its web UI, the most useful view.** Join a phone or laptop to `AMR_Base`
  (password `AMR12345`) and open `http://192.168.4.2`: the current picture with
  its digit and dial windows, and the value it read. `http://192.168.4.2/json`
  is exactly what the C3 reads, and System, Log Viewer shows the camera's log
  without a cable. The device has no internet while it is joined.
- **Its serial log**, with the camera on its programmer board:

  ```sh
  pio device monitor -p /dev/ttyUSB0 -b 115200 --dtr 0 --rts 0
  ```

  `--dtr 0` stops a reset from landing in download mode, since the programmer
  board ties DTR to the camera's IO0. For the first few seconds the board's USB
  chip may replay a stale scrap of output over and over. Ignore it, the real log
  follows. While no C3 network is up, the camera prints
  `WIFI: Disconnected (201, No AP)` every few seconds, which is normal.

## 7. End to end {#cv_e2e}

Bring up the gateway and substation as in [Getting started](GETTING_STARTED.md),
sections 5.1 and 5.2, with every board flashed from the same commit: the packet
format changes between versions, and a board on another version drops the
readings as the wrong size. With the CV node on the substation's channel:

```text
[Substation] Joined UID: ... | MAC: ...             substation
[Substation] Buffered UID: ... | SEQ: ... | Total: 1
[Substation] Relay SUCCESS | UID: ... | SEQ: ...
DATA FWD, UID: ..., PUB: SUCCESS                    gateway
```

The CV node waits for the substation to acknowledge each reading. If it does not,
the node logs `[Link] No delivery ACK, rejoining` and tries again with the next
reading. Watch the readings arrive as in [Listen to MQTT](GETTING_STARTED.md).

## 8. Tuning for a real meter {#cv_tuning}

The demo config only fits the demo photos. For a real meter, in the camera's web
UI:

- **Reference marks and alignment:** pick two high contrast marks that never
  move, such as screws or printed corners.
- **Digit windows:** one per wheel, each just enclosing the digit.
- **Dial windows:** square, centred on the needle's hub, with the scale ring
  inside.
- **Dial model:** use `ana-cont_1300_s2` or `ana-cont_1500_s2_q`. Avoid
  `ana-cont_1400_s2_q`: its output is clipped, so it only reads needles between
  5 and 7.5 correctly.
- **`AnalogToDigitTransitionStart`:** the ×0.1 dial's position when the last
  digit wheel starts to roll. The default 9.2 suits most meters. Lower it if
  readings jump a whole number too early around a rollover.

`docs/hw-tests/2026-10-13-cv-node/README.md` shows how the demo was tuned, with
before and after images and readings.

## 9. Troubleshooting {#cv_trouble}

| Symptom | Likely cause |
|---|---|
| The camera's log repeats `WIFI: Disconnected (201, No AP)` | The C3 is not running the CV node yet (waiting at the menu, or no monitor), or `wlan.ini` does not match `ap_ssid` and `ap_pass` |
| The C3 repeats `[HttpBus] No station joined the AP yet` | The camera has not joined: see the line above, or the camera is unpowered |
| `[HttpBus] GET http://192.168.4.2/json failed, HTTP status -1` for minutes | The camera is still booting (allow a minute), is boot looping (see below), or `wlan.ini`'s `ip` does not match `cam_host` |
| The camera's flash LED fires over and over | Brown out reboot loop. Reseat its power wires, or power it from USB on the programmer board |
| `Flow "main" not present in response.` | `cam_flow` does not match the number name in the camera's `config.ini` |
| `Flow "main" has no value yet.` | The camera has not finished its first round since booting. Wait a minute |
| `[Link] No substation beacon heard` | `espnow_chan` differs from the substation's, or the substation is off |
| `[CV] ESP-NOW send failed.` or `[Link] No delivery ACK, rejoining` | The substation is off, out of range, or on another firmware version |
| The C3 prints nothing, or `waiting for download` | It is in download mode. Press its RST, or replug it without holding BOOT, and open the monitor with `--dtr 0 --rts 0` |
| `pio device monitor` shows camera lines instead of the C3's | It opened the camera's port. Pass `-p /dev/ttyACM0` |
| The camera upload stops at `Failed to connect to ESP32` | Put it in its bootloader by hand: hold IO0, tap RST, release IO0 |
| A reading jumps a whole number near a rollover | `AnalogToDigitTransitionStart` does not suit this meter. See [Tuning for a real meter](#cv_tuning) |
