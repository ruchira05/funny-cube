# Key Cube – Implementation Plan

A desk cube built on an ESP32-S3. It has a 1.3" OLED on one face, a touch strip and a tilt sensor. The face the cube stands on selects what it does. One mode is "key buddy": touch, speak, and the cube answers with an emotion face and a short reply from the key-ai server.

```
[Mic I2S] → ESP32-S3 → HTTPS POST /audio (raw PCM16, 16 kHz) → nginx → FastAPI
                                                                          │ whisper → ollama
[OLED]  ← {text, reply, emotion} JSON ←───────────────────────────────────┘
```

The server already exists (`main.py`). This plan covers the firmware, wiring and cube.

## Stages at a glance

| Stage | Goal | New hardware | Done when |
|---|---|---|---|
| **1. WiFi setup by QR** | Cube joins any WiFi from a phone, and stores server settings | Board, OLED, touch strip | Scan QR on the OLED, pick a network, cube connects and reaches `/health` |
| **2. Cube orientation** | 5 resting positions select 5 modes, screen rotates | MMA8452 accelerometer | Each face shows its mode name upright; off face sleeps |
| **3. Key buddy mode** | Touch to talk, reply and face on screen | INMP441 mic | Spoken question gets a face and reply text |
| **4. Clock, alarm, reminders, focus timer** | The other 3 modes work offline after time sync | MAX98357A amp and 40 mm speaker | Alarm and reminders fire on time, touch to dismiss |
| **5. Hardening and enclosure** | Reliable enough to live on a desk | Cube enclosure | 24 h soak test passes |

Each stage builds on the previous one and ends with a working device.

---

## Project-wide decisions

### Hardware (all stages)

| Part | Detail | Stage |
|---|---|---|
| ESP32-S3 DevKitC-1 **N16R8** | 16 MB flash, 8 MB octal PSRAM (tronic.lk MD0656) | 1 |
| 1.3" 128×64 white OLED, I2C | SH1106 or SSD1306 driver (tronic.lk DM0056) | 1 |
| Copper tape or foil strip | About 1 × 5 cm, one jumper wire. Fallback: TTP223 module | 1 |
| MMA8452 (GY-45) accelerometer | I2C (tronic.lk MD0693) | 2 |
| INMP441 I2S microphone | | 3 |
| MAX98357A I2S amplifier module | Mono class D, 3.2 W into 4 Ω at 5 V (tronic.lk MD0860, Rs. 480) | 4 |
| 4 Ω 5 W speaker, 1.5" (40 mm) | Full range (tronic.lk SP0043, Rs. 980). Replaces the buzzer | 4 |
| Cube enclosure | About 7–8 cm per side to fit the DevKitC-1 (about 70 × 26 mm) | 5 |
| Breadboard and jumper wires | | 1 |

### Pin map (ESP32-S3 N16R8)

| Function | GPIO | Stage |
|---|---|---|
| I2C SDA (OLED, MMA8452) | 8 | 1 |
| I2C SCL (OLED, MMA8452) | 9 | 1 |
| Touch strip | 4 (Touch4) | 1 |
| MMA8452 INT1 (motion wake) | 10 | 2 |
| Mic SCK / WS / SD | 5 / 6 / 7 | 3 |
| Speaker amp BCLK / LRC / DIN | 15 / 16 / 17 | 4 |
| Speaker amp SD (shutdown, high = on) | 18 | 4 |

Pin rules (pinout: mischianti.org ESP32-S3 DevKitC-1):
- Don't use GPIO 0, 3, 45 or 46 (strapping pins).
- Don't use GPIO 35, 36 or 37. They're on the header, but the octal PSRAM uses them. GPIO 26–32 are flash and PSRAM.
- Avoid GPIO 19 and 20 (USB D− and D+) and 43 and 44 (UART0, serial log).
- There is no GPIO 22–25.
- Touch pins are GPIO 1–14. Deep-sleep wake (RTC) pins are GPIO 0–21.

I2C addresses: OLED `0x3C`, MMA8452 `0x1C` (or `0x1D`). No clash.

### Software

- PlatformIO in VS Code, Arduino framework. The project is in `esp32/`.
- `platformio.ini` uses `board = esp32-s3-devkitc-1` (PlatformIO's N8 profile) with N16R8 overrides: 16 MB flash, `default_16MB.csv`, `memory_type = qio_opi`, `-DBOARD_HAS_PSRAM`. Keep these overrides, or PSRAM reads as 0.
- Libraries are added per stage (listed in each stage).

### Settings and secrets

WiFi networks, the server URL, the API key, the device ID and the timezone are entered once on the phone setup page (Stage 1) and stored in flash (NVS, via `Preferences`). Nothing secret is compiled into the firmware. `config.h` holds only pins and constants.

### Firmware layout (grows by stage)

```
esp32/
├─ platformio.ini
└─ src/
   ├─ main.cpp           # boot, top-level loop
   ├─ config.h           # pins and constants only (no secrets)
   ├─ settings.cpp/.h    # NVS: networks, server URL, API key, device ID, timezone   (1)
   ├─ display.cpp/.h     # U8g2 init, status screens, QR drawing, rotation          (1)
   ├─ touch.cpp/.h       # calibration, debounced touch, long-press                 (1)
   ├─ wifi_setup.cpp/.h  # connect to saved networks, setup hotspot and portal      (1)
   ├─ server.cpp/.h      # HTTPS: /health, /audio, /reset                           (1, 3)
   ├─ orientation.cpp/.h # MMA8452, face detection, motion wake                     (2)
   ├─ modes.cpp/.h       # mode state machine: enter/loop/exit                      (2)
   ├─ net.cpp/.h         # background WiFi reconnect and /health task               (2)
   ├─ power.cpp/.h       # deep sleep, wake on motion or touch                      (2)
   ├─ faces.cpp/.h       # emotion faces                                            (3)
   ├─ keybuddy.cpp/.h    # mode A: hold to talk, thinking, reply pages              (3)
   ├─ audio.cpp/.h       # I2S mic, record to PSRAM                                 (3)
   ├─ speaker.cpp/.h     # I2S output to the MAX98357A: tones and beeps             (4)
   ├─ alarms.cpp/.h      # NTP time, alarm and reminder schedule, alert screens     (4)
   ├─ clockmodes.cpp/.h  # mode B (alarm) and mode C (health reminders) screens     (4)
   ├─ focus.cpp/.h       # mode D: focus timer and large clock                      (4)
   └─ webconfig.cpp/.h   # settings page at http://keycube.local                    (4)
```

---

## Stage 1: WiFi setup by QR code

**Goal:** a new cube, with no WiFi saved, shows a QR code. You scan it with a phone, choose a WiFi network, enter the server settings, and the cube connects. It remembers several networks and picks whichever is in range.

### 1.1 Wiring

| Part | Pin | ESP32-S3 |
|---|---|---|
| OLED | GND / VCC | GND / 3V3 |
| OLED | SDA / SCL | GPIO 8 / GPIO 9 |
| Touch strip | wire | GPIO 4 |

- OLED pin order differs between modules (GND-VCC-SCL-SDA vs VCC-GND-SDA-SCL). Read the silkscreen first, because reversed power can damage it.
- Touch strip: stick the copper on card or plastic and cover it with tape. Keep the wire short and away from the I2C lines and the antenna.

### 1.2 Libraries

```ini
lib_deps =
    olikraus/U8g2
    bblanchon/ArduinoJson
```

Implemented without extra libraries. The ESP32 core (arduino-esp32 3.x) already bundles everything else:
- `WebServer` and `DNSServer` for the captive portal (own code, non-blocking, supports the saved-network list and extra fields)
- `esp_qrcode_generate` (ESP-IDF QR encoder) for the QR codes
- `Preferences` for NVS, `NetworkClientSecure` and `HTTPClient` for HTTPS

The server check pins ISRG Root X1 and X2 (Let's Encrypt), stored in `src/certs.h`.

### 1.3 Boot flow

```
BOOT ─► board check ─► display + touch init
          │
          ├─ touch held during boot (3 s)? ──────────────► SETUP MODE
          ├─ touch held during boot (10 s)? ─► wipe NVS ─► SETUP MODE   (factory reset)
          │
          ├─ no saved networks? ─────────────────────────► SETUP MODE
          │
          └─ try saved networks (strongest first, 20 s)
                ├─ connected ─► check server /health ─► READY
                └─ all failed ─► "No WiFi" screen ─► SETUP MODE
```

Holding the touch strip while powering on is the setup gesture. It can't clash with touch-to-talk in later stages.

### 1.4 Setup mode (hotspot and portal)

1. The cube starts a WiFi hotspot named `KeyCube-XXXX` (last 4 hex digits of the MAC) with a WPA2 password. Generate the password once, store it in NVS, and show it in small text under the QR code.
2. **QR screen 1 (join hotspot):** a WiFi QR code, `WIFI:T:WPA;S:KeyCube-XXXX;P:<password>;;`. Phone cameras on iOS and Android join from this directly.
3. Once the phone joins, the captive portal page usually opens automatically. If it doesn't, the cube switches to **QR screen 2 (open page)** with `http://192.168.4.1`. A short touch toggles between QR 1 and QR 2.
4. The portal page (served from the main loop, so the display keeps updating):
   - List of nearby networks from a scan, and a password field
   - Extra fields: **server URL**, **API key**, **device ID**, **timezone** (default Sri Lanka, POSIX `<+0530>-5:30`)
5. On save: append the network to the saved list, store the extra fields in NVS, stop the hotspot, and connect.
6. Setup mode times out after 5 minutes with no phone connected, then retries saved networks.

**Fitting the QR code on 128×64:** a 38-character WiFi string fits a version 3 QR code (29 × 29 modules). At 2 px per module it's 58 × 58 px. Draw it on the left with a quiet zone, and put the text ("Scan to set up", the hotspot name, the password) in the 64 px on the right.

### 1.5 Multiple networks

- Store up to **5 networks** in NVS as a list: SSID, password and last-used time.
- At boot, scan, then connect to the strongest saved network in range (`WiFiMulti`).
- Re-running setup **adds** a network. When the list is full, drop the least recently used one.
- A network is added to the list only after the cube connects to it successfully.
- While connected, if WiFi drops: retry for 30 s, show a "WiFi lost" icon, and keep running offline features.

### 1.6 Server check

After connecting, call `GET <server URL>/health` over HTTPS. Show "Server OK" or the error.
- Pin the root CA with `client.setCACert()`. The server's nginx uses Let's Encrypt, so the root is ISRG Root X1. Don't use `setInsecure()`: the API key travels in a header from Stage 3.
- Wrong URL, DNS failure or timeout shows a "Server unreachable" screen, with a hint to re-run setup.
- `/health` doesn't need the API key. A 401 check comes in Stage 3 with `/audio`.

### 1.7 Screens

| Screen | Content |
|---|---|
| Booting | Logo or "Key Cube", version |
| Connecting | "Connecting to <SSID>…" with a spinner |
| Setup: QR 1 | WiFi QR code, "Scan to set up", hotspot name and password |
| Setup: QR 2 | URL QR code, "Open 192.168.4.1" |
| Phone connected | "Phone connected, finish on your phone" |
| Connected | Happy face, SSID, IP address |
| Server OK / error | "Server OK" or the error text |
| No WiFi | Sad face, "No WiFi. Hold touch at power-on to set up" |

Never show saved WiFi passwords or the API key on the display.

### 1.8 Steps

1. **Board check:** flash the existing `src/main.cpp` sketch. Expect flash 16 MB, PSRAM about 8 MB, and `ps_malloc(320 KB): OK`.
2. **Display bring-up:** run an I2C scan (expect `0x3C`). Start with `U8G2_SH1106_128X64_NONAME_F_HW_I2C`. If the image is shifted 2 px or garbled, switch to `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`.
3. **Touch:** log raw `touchRead(4)` values over serial. On the S3 the value **rises** when touched. Calibrate at boot (average 20 readings), set the threshold to `baseline * 1.1` to start, and tune from the logs. Use 3-sample hysteresis and detect "held for N seconds".
4. **Settings store:** `settings.cpp` with `Preferences`: network list, server URL, API key, device ID, timezone, hotspot password, and a factory-reset wipe.
5. **QR rendering:** draw a fixed test string as a QR code and scan it with a phone from about 20 cm.
6. **Setup mode:** hotspot, portal with the extra fields, save to NVS, both QR screens, and the 5-minute timeout.
7. **Connect flow:** saved-network list, strongest first, boot gestures, and reconnect handling.
8. **Server check:** HTTPS `/health` with the pinned CA.

### 1.9 Stage 1 test checklist

- [ ] Fresh board (factory reset) shows QR 1. The phone camera joins the hotspot from it.
- [ ] Portal opens automatically, or via QR 2 if it doesn't.
- [ ] Saving a network connects within about 10 s, shows SSID and IP, then "Server OK".
- [ ] Reboot reconnects without setup.
- [ ] Add a second network (phone hotspot). Turn off the first network and reboot: the cube joins the second.
- [ ] Wrong WiFi password shows an error and returns to setup.
- [ ] Wrong server URL shows "Server unreachable".
- [ ] At power-on the boot screen says "Hold strip: setup". Touching it within 2 s and holding 3 s enters setup, 10 s does a factory reset. A hand already on the cube at power-on (pressing RESET) is ignored.
- [ ] Setup mode with no phone times out after 5 minutes.
- [ ] The API key and WiFi passwords never appear on the screen or in the serial log.

### 1.10 Stage 1 risks

| Risk | Mitigation |
|---|---|
| Captive portal doesn't pop up on some phones | QR 2 with the `192.168.4.1` URL |
| QR too small or blurry to scan on the OLED | 2 px per module, quiet zone, keep the string short |
| QR library clashes with the core | Avoided: uses the core's built-in `esp_qrcode_generate` |
| 5 GHz-only or captive-login networks (hotels, campuses) | Not supported by the ESP32-S3. Show a clear error |
| Portal blocks the display | Web server and DNS are polled from the main loop (non-blocking) |
| API key readable from flash | Acceptable for a prototype. Enable flash encryption before shipping (Stage 5) |

---

## Stage 2: Cube orientation and modes

**Goal:** the face the cube stands on selects one of 5 states, and the screen rotates so the text is upright.

### 2.1 Layout

- The OLED is on the **front** face. The **off face is the back** (opposite the screen).
- Resting on the off face puts the screen facing up and blank. This is the default idle state.
- Standing the cube on any of the other 4 faces selects a mode. The screen then faces sideways, toward you.
- An accelerometer measures tilt, not spin around the vertical axis. This layout works because each of the 5 positions puts gravity on a different axis.

Each side face is named by the display rotation that reads upright on it (R0–R3), so a face also sets the screen rotation. The back face is the 5th position.

| Face down | Face name | Mode (to confirm) | Screen |
|---|---|---|---|
| Back (off face) | `Back` | **Off:** screen blank, deep sleep | off |
| Side, portrait | `R1` | **A: Key buddy** (Stage 3) | 64 × 128 |
| Side, portrait, flipped | `R3` | **B: Alarm** (Stage 4) | 64 × 128 |
| Bottom edge | `R0` | **C: Health reminders** (Stage 4) | 128 × 64 |
| Top edge | `R2` | **D: Focus timer or clock** (Stage 4) | 128 × 64 |

Change A–D by editing `MODE_FOR_FACE` in `modes.cpp` only.

### 2.2 Wiring

| MMA8452 (GY-45) | ESP32-S3 |
|---|---|
| VCC / GND | 3V3 / GND |
| SDA / SCL | GPIO 8 / GPIO 9 (shared with OLED) |
| INT1 | GPIO 10 (motion wake) |

The listing gives no pin names, so read the silkscreen before wiring. If INT1 isn't wired, touch still wakes the cube (GPIO 10 is pulled down during sleep).

### 2.3 Orientation sensing (`orientation.cpp`)

- Own MMA8452 driver over `Wire` (no library): ±2 g, 50 Hz, read at 20 Hz, low-pass filtered.
- Each face has a stored gravity direction (unit vector). A reading belongs to the nearest one if it is within about 30° (`FACE_MIN_DOT` 0.85) and the total is 0.8–1.2 g. Otherwise the cube is moving and the reading is ignored.
- Switch only after the same face holds for **500 ms**. Raw and filtered readings must agree, so shaking can't trigger a switch.
- Directions are set per face in `config.h` (`FACE_DIR_BACK`, `FACE_DIR_SCREEN_DOWN`, `FACE_DIR_R0`..`R3`); no calibration step. Lying on the screen also counts as off. The defaults assume the sensor is mounted with X pointing to the back (into the screen), Y to the right and Z down. If it is mounted differently, read the `filt` values from the serial log on each face (`ACCEL_DEBUG 1`) and enter them there. A warning is logged at boot if two directions are less than 60° apart.

### 2.4 Mode state machine (`modes.cpp`), WiFi (`net.cpp`), sleep (`power.cpp`)

- `face = orientation.face()` maps to a mode. Each mode has `enter()`, `loop()` and `exit()`. In this stage, modes A–D show their name, an icon and a status line (Online, No WiFi, ...). A tap shows WiFi and server status for 6 s and re-checks `/health`.
- `/health` runs in a background FreeRTOS task, and WiFi reconnects without blocking, so the screen never freezes waiting for the network.
- **Off mode:** blank the screen; after 3 s turn WiFi off and enter deep sleep. Wake on MMA8452 motion (INT1 → GPIO 10, tilt about 0.25 g past rest) or touch (GPIO 4). On wake: read the face. If it's still the back face, sleep again without WiFi. Otherwise show the mode at once and reconnect WiFi in the background.
- The Stage 1 setup gesture works at power-on (fresh touch within 2 s of the boot screen, held 3 s / 10 s) and after a touch wake (keep holding the strip). A tilt wake skips it.
- DEV-ONLY `DEV_NO_SLEEP_ON_USB`: while a computer is connected over USB, the off face blanks the screen but stays awake, so upload and the serial monitor keep working.

### 2.5 Steps

1. Wire the MMA8452. I2C scan should show `0x3C` and `0x1C`. Print raw x, y and z.
2. Hold the cube on each face and note the axis and sign.
3. Debounced face detector. Set the `FACE_DIR_*` values from step 2 if the sensor is not mounted flat behind the screen.
4. Mode switching with rotation and placeholder screens.
5. Off mode with deep sleep and wake on motion or touch.

### 2.6 Stage 2 test checklist

- [ ] All 5 positions recognized correctly, 20 times each.
- [ ] Carrying or shaking the cube doesn't change modes.
- [ ] Text is upright on all 4 side faces.
- [ ] Off face blanks the screen and sleeps. Picking the cube up wakes it.
- [ ] WiFi reconnects after wake without setup.

---

## Stage 3: Key buddy mode (Mode A)

**Goal:** touch and hold, speak, release. The cube shows a "thinking" face, then the emotion face and the reply.

### 3.1 Wiring

| INMP441 | ESP32-S3 |
|---|---|
| VDD / GND | 3V3 / GND |
| L/R | GND (left channel) |
| SCK / WS / SD | GPIO 5 / 6 / 7 |

### 3.2 Flow

```
IDLE ──(touch)──► RECORDING ──(release or 10 s)──► UPLOADING ──► SHOW_REPLY (~8 s) ──► IDLE
                                                       └── error ──► SHOW_ERROR ──► IDLE
```

| State | Screen |
|---|---|
| IDLE | Neutral face, "Touch to talk" |
| RECORDING | Listening face, level bar |
| UPLOADING | Thinking face |
| SHOW_REPLY | Emotion face for about 2 s, then the reply text |
| SHOW_ERROR | Short message (WiFi down, 401, 502, timeout) |

### 3.3 Audio (`audio.cpp`)

- I2S RX at 16 kHz, 32-bit samples, left channel. The INMP441 outputs 24 bits left-aligned in 32: convert with `sample >> 14` (try `>> 11` if quiet), then clip to int16.
- The mic runs (in its own FreeRTOS task) only while mode A is active, so recording starts instantly on touch. The first 200 ms after it starts are dropped (mic settle). A DC blocker removes the INMP441 offset. `MIC_SHIFT` in `config.h` sets the gain.
- Buffer in PSRAM with `ps_malloc()`: 32 KB per second, **10 s cap (320 KB)**. Internal RAM stays free for WiFi and TLS.
- Uses `ESP_I2S` (arduino-esp32 3.x): standard mode, 32-bit, mono, left slot.

### 3.4 Upload (`server.cpp`)

- `POST <server URL>/audio?sample_rate=16000`, body is raw PCM16 mono.
- Headers: `Content-Type: application/octet-stream`, `X-API-Key: <key from NVS>`, `X-Device-Id: <device ID from NVS>`.
- Timeouts: connect 5 s, response 30 s (matches nginx `proxy_read_timeout`).
- Parse `{text, reply, emotion}` with ArduinoJson.
- Errors: 400 too short, 401 bad key (hint: re-run setup), 413 too large, 502 LLM down.
- The upload runs in a background task, so the thinking face keeps animating. If the cube is turned to another face meanwhile, the reply is dropped.
- Not done yet (optional): `POST /reset` to clear the conversation. The server forgets it after 10 minutes idle anyway.

### 3.5 Faces and text (`faces.cpp`)

- Emotions from the server: `happy, sad, angry, surprised, neutral, thinking, love`.
- Mono screen, so faces differ by shape only: `happy` smile, `sad` frown, `angry` slanted brows, `surprised` "O" mouth, `neutral` flat line, `thinking` squint and dots, `love` heart eyes.
- Layout follows the face: portrait (64 × 128) puts the face on top and 7 lines of 12 characters below; landscape puts the face on the left and 7 lines of 16 characters on the right. Replies are about 15 words, so usually 1–2 pages. Pages advance every 5 s or on tap; the last page stays 20 s.
- Unicode quotes and dashes are mapped to ASCII; emoji are dropped (the fonts are ASCII/Latin-1).

### 3.6 Touch in this mode

- Recording starts as soon as the touch is detected (3 samples). Release after 3 below.
- Shorter than 0.3 s is a **tap**: next reply page, dismiss an error, or (idle) show WiFi and server status. Maximum 10 s (sent automatically), then a 300 ms lockout.
- The touch baseline drifts slowly only while untouched (Stage 1).

### 3.7 Stage 3 test checklist

- [ ] "Hello" returns a reply and the correct face.
- [ ] Recording plays back clearly when dumped to serial and opened in Audacity.
- [ ] Wrong API key shows a 401 screen.
- [ ] Server stopped shows an error, and the cube recovers when it returns.
- [ ] A second question uses context (same device ID). After 10 minutes idle, the history resets.
- [ ] 50 requests in a row with no memory leak (log free heap and PSRAM).

---

## Stage 4: Clock, alarm, health reminders, focus timer

**Goal:** Modes B, C and D work with no server, using NTP time and a speaker.

### 4.1 Wiring

MAX98357A I2S amplifier and a 4 Ω 5 W 40 mm speaker. The speaker replaces the buzzer, so GPIO 12 stays free.

| MAX98357A | ESP32-S3 |
|---|---|
| VIN | 5V (USB/VIN). At 3.3 V a 4 Ω speaker is much quieter |
| GND | GND |
| BCLK | GPIO 15 |
| LRC | GPIO 16 |
| DIN | GPIO 17 |
| SD | GPIO 18 (high = on, low = shutdown, so the amp draws almost nothing in deep sleep) |
| GAIN | Leave open (9 dB). Don't use 15 dB: the speaker is rated 5 W and the amp can clip |
| Speaker + / − | Amp output terminals (either polarity works for a single speaker) |

- The mic uses one I2S port and the speaker the other (the S3 has two), so they don't clash.
- Cap the volume in software (a gain factor on the samples) so the speaker isn't over-driven.
- Mute the mic while the speaker plays, to avoid feedback.
- Pull SD low before deep sleep. Check the board has the default SD pull-up, and if it floats, add the GPIO 18 control.
- Playing server replies out loud needs a TTS endpoint on the key-ai server (later). Stage 4 needs only tones and beeps, generated in firmware.

### 4.2 Features

No extra libraries: the ESP32 core has `ESP_I2S`, SNTP, `WebServer` and `ESPmDNS`.

- **Speaker (`speaker.cpp`):** sine tones with a short fade in and out (no clicks), played by a background task so screens keep updating. 16 kHz, 16-bit, the same sample on both I2S slots. The I2S clock runs all the time (silence when idle): its pins (15–17) sit next to the touch pin on the header, and a clock that starts and stops would shift the touch reading. The amp is on (SD high) only while a sound plays, and SD is held low through deep sleep. Volume 0–100 on the settings page, capped at `SPK_MAX_AMPLITUDE`. The alarm starts at 40 % and rises to full over 30 s, and never rings below `SPK_ALARM_MIN_VOLUME`.
- **Time:** SNTP after WiFi connects, in the timezone from the setup page, re-synced every 6 hours. The RTC keeps time through deep sleep. After a power cut the clock shows `--:--` until the first sync, and alarms wait for it.
- **Alarm (Mode B, `clockmodes.cpp`):** clock and the next alarm with "in 2h 05m". Up to 4 alarms, each once or on chosen weekdays. When it rings (in any mode, also from deep sleep): the screen flashes, **tap = snooze 5 min, hold 1.5 s = stop**. It stops by itself after 10 minutes. In the mode: tap shows the alarm list, then the status page; hold switches all alarms on/off (or cancels a snooze). A one-time alarm switches itself off after it rings.
- **Health reminders (Mode C, `clockmodes.cpp`):** water, stretch and posture, each on/off with its own interval (default 60 / 50 / 40 min), only between active hours (default 09:00–18:00). The screen shows what was done today and when each is next. A reminder pops up over any side mode with a chime (again after 1 min): **tap = done** (counted for today), **hold = skip**, unanswered for 2 min = missed. The next interval counts from the answer. Hold in the mode pauses all reminders.
- **Focus timer or clock (Mode D, `focus.cpp`):** a large clock when idle. Tap starts 25 min of focus, then a 5 min break (15 min after every 4th), with a sound at each change. Tap pauses and resumes, hold resets. The timer keeps running on the other side faces; a period that ends there shows a short notice.
- **Alerts (`alarms.cpp`):** the alarm, reminder and notice screens sit on top of whatever mode is active, and the mode pauses underneath. Key buddy stops the mic, so nothing records the speaker. Reminders and notices wait while key buddy is recording or thinking; an alarm doesn't wait. A finger already on the strip when an alert starts is ignored until it is lifted.
- **Settings page (`webconfig.cpp`):** `http://keycube.local` (mDNS) or the IP address shown on the status page (tap in Alarm or Health mode). Alarms, reminders and active hours, focus lengths, volume, a test sound and a test alarm. No login, so anyone on the same WiFi can change alarms. It never shows WiFi passwords or the API key. WiFi, server and timezone stay on the setup hotspot page.
- **Storage:** alarms, reminders, focus lengths, volume and today's counts are in NVS (a factory reset clears them). Snooze, the next reminder times and the last alarm check are in RTC memory, so they survive deep sleep but not a power cut.
- **Off face:** the off face means off. No reminders, and the focus timer resets. Alarms still ring: before deep sleep a timer wake is set **15 minutes before** the next alarm (`ALARM_PREWAKE_S`). The internal RC clock that keeps time in deep sleep can drift by minutes over a night, so the cube wakes early, joins WiFi to correct the time, and waits awake with the screen off. An alarm found up to 10 minutes late (cube unplugged, or the clock jumped at a sync) still rings; older ones don't.
- **Touch:** a hold (1.5 s, `TOUCH_HOLD_MS`) is now an event of its own, so a long press never counts as a tap. The stored touch baseline has a version number: the first boot of this firmware measures it again with the speaker clock running.

### 4.3 Stage 4 test checklist

- [ ] The test sound on the settings page plays cleanly at volume 30, 60 and 100, with no click when the amp switches on or off, and no resets (USB power).
- [ ] Touch still works in every mode with the speaker clock running. Check the boot log baseline, then use `touch_cal` if taps are missed.
- [ ] Clock matches your phone within 1 s after sync.
- [ ] The settings page opens at `http://keycube.local` (or the IP), and saving keeps the values after a reboot.
- [ ] Alarm fires on time in Modes A–D, and from deep sleep (log: `timer wake at ...`, then `staying awake until it rings`).
- [ ] Tap snoozes for 5 min, hold stops. A finger already on the strip doesn't snooze it.
- [ ] "Ring a test alarm" works while talking to key buddy: the recording stops, and key buddy comes back afterwards.
- [ ] Reminders fire at their intervals, only within active hours, and survive a reboot (settings and today's counts).
- [ ] Focus timer runs, beeps at the end of each period, pauses, resets, and shows a notice when it ends on another face.

### 4.4 Later: voice-set reminders

Saying "remind me to drink water at 3" needs a server change: the LLM returns a structured reminder, the cube stores it. Plan it after Stage 4 works.

---

## Stage 5: Hardening and enclosure

- **Reliability:** task watchdog, WiFi auto-reconnect, at most one upload at a time.
- **OLED burn-in:** screen off after 60 s idle in any mode except clock (`setPowerSave(1)`). Shift static elements by a pixel now and then.
- **Firmware updates:** over-the-air (OTA) updates from the local settings page, so the cube doesn't need USB once enclosed.
- **Security before sharing devices:** enable flash encryption so the API key and WiFi passwords can't be read from flash. HTTPS only.
- **Enclosure:** about 7–8 cm cube. OLED window on the front, touch strip under a thin wall, mic hole, speaker grille (a round opening of at least 40 mm, sealed to the speaker rim with foam, a few cm from the mic), USB-C access. Mount the sensor board flat behind the screen, X to the right and Y up, to match the `FACE_DIR_*` defaults (about 30° of tilt is tolerated). Use strain relief on wires.
- **Power:** USB-C powered. Battery is a later decision (needs deep sleep tuning and a charger board).
- **Soak test:** 24 hours on the desk, mode changes every few minutes, alarms and reminders firing, with no resets or memory growth.

---

## Risks across stages

| Risk | Mitigation |
|---|---|
| Wrong PSRAM settings (boot loop, PSRAM 0) | Keep the `qio_opi` and `-DBOARD_HAS_PSRAM` overrides. Log `ESP.getPsramSize()` at boot |
| OLED driver mismatch (SH1106 vs SSD1306) | Try SH1106 first, switch if offset or garbled |
| Touch unstable (long wire, metal desk, held in hand) | Short wire, calibration, hysteresis. Fallback: TTP223 |
| Sensor mounted at an angle | Calibration command |
| Whisper mishears (quiet mic) | Tune the `>>` shift. Consider `base.en` on the server |
| Total reply time 3–8 s | Thinking face, small LLM, low `MAX_TOKENS` |
| API key exposure | HTTPS with CA pinning. Flash encryption before shipping |

## Open questions

- Which function goes on each of the 4 side faces? (Stage 2 table)
- Audio replies later? Needs a speaker, an I2S amp (MAX98357A) and server-side text-to-speech.
- Wake word or auto-stop (VAD) instead of touch-to-talk?
- Battery power for the cube?
