# Key Cube – Components and datasheets

Every part in the cube, with its key specs, its pins, and links to its datasheet. The specs come from the datasheets linked below, or from the seller's spec sheet where no maker's datasheet exists.

The **bare-module build** (this branch) changes two parts:

| Now (prototype) | Bare-module build | Why |
|---|---|---|
| ESP32-S3 DevKitC-1 N16R8 board (65 × 29 mm) | **ESP32-S3-WROOM-1 module** (18 × 25.5 mm), on our own board | Much smaller. The board adds the 3.3 V regulator, USB-C, reset and boot buttons that the DevKit had |
| 1.3" 128 × 64 mono OLED, I2C | **2.4" 240 × 320 colour TFT, ST7789V, SPI** | Colour, higher resolution, bigger faces and text |

All other parts stay as they are.

---

## Summary

| # | Part | Key spec | Supply | Interface | Datasheet | Bought from |
|---|---|---|---|---|---|---|
| 1 | ESP32-S3-WROOM-1 (N8R2 / N8R8 / N16R8) | Dual-core 240 MHz, WiFi 2.4 GHz, BLE 5, PCB antenna | 3.0–3.6 V, ≥ 0.5 A | 41 pins, castellated | [Module][wroom] | EC Buying (online) |
| 2 | 2.4" TFT LCD, ST7789V | 240 × 320, 262K colours, 4 white LED backlight | 3.3 V | 4-wire SPI | [ST7789V][st7789v] | EC Buying (online) |
| 3 | ESP32-S3 DevKitC-1 N16R8 (prototype) | WROOM-1 N16R8 on a dev board | 5 V USB or 3.3 V | 2 × 22 pin header | [Board guide][devkitc] | [tronic.lk][t-devkit] |
| 4 | 1.3" OLED, SH1106 (prototype) | 128 × 64 mono, white | 3.3 V | I2C `0x3C` | [SH1106][sh1106] | [tronic.lk][t-oled] |
| 5 | INMP441 MEMS microphone | 24-bit, omnidirectional | 1.8–3.3 V | I2S | [INMP441][inmp441] | [tronic.lk][t-mic] |
| 6 | MAX98357A amplifier | Class D mono, 3.2 W into 4 Ω at 5 V | 2.5–5.5 V | I2S | [Adafruit guide][max-ada], [Analog Devices][max-adi] | [tronic.lk][t-amp] |
| 7 | Speaker 4 Ω 5 W, 40 mm | Full range | — | 2 wires | Seller page | [tronic.lk][t-spk] |
| 8 | MMA8452Q accelerometer (GY-45 board) | 3-axis, ±2/4/8 g, 12-bit | 1.95–3.6 V | I2C `0x1C`/`0x1D` | [MMA8452Q][mma] | [tronic.lk][t-accel] |
| 9 | Touch strip (copper or aluminium foil) | About 31 × 5.5 mm | — | ESP32 touch pin | — | Hardware shop |
| 10 | LiPo battery 3.7 V 800 mAh | 10 × 25 × 47 mm (measured), JST-PH | — | — | Seller page | [tronic.lk][t-bat] |
| 11 | TP4056 USB-C charger with protection | 4.2 V CC/CV, 1 A default | 5 V USB-C | — | [TP4056][tp4056], [DW01A][dw01a] | [tronic.lk][t-chg] |
| 12 | Mini boost converter 5/8/9/12 V | Set to 5 V for the amp and the board | 3–4.2 V in | — | Seller page | [tronic.lk][t-boost] |

---

## 1. ESP32-S3-WROOM-1 module

**Datasheets:** [ESP32-S3-WROOM-1/1U module][wroom] · [ESP32-S3 chip][s3chip] · [Technical reference manual][s3trm] · [Hardware design guidelines][hwguide]

### Which variant

The listing sold three variants. They differ only in memory, but that decides which pins are free and how the firmware is built. **Check the marking on the metal shield** (e.g. `ESP32-S3-WROOM-1 N16R8`).

| Variant | Flash | PSRAM | GPIO 35, 36, 37 | Temperature | PlatformIO memory setting |
|---|---|---|---|---|---|
| N8R2 | 8 MB (Quad) | 2 MB (Quad) | Free | −40 to 85 °C | `qio_qspi` |
| N8R8 | 8 MB (Quad) | 8 MB (Octal) | **Used by PSRAM** | −40 to 65 °C | `qio_opi` |
| N16R8 | 16 MB (Quad) | 8 MB (Octal) | **Used by PSRAM** | −40 to 65 °C | `qio_opi` (the current firmware) |

- The firmware today is built for N16R8 (`esp32/platformio.ini`: 16 MB flash, `default_16MB.csv`, `qio_opi`). For N8R8, change the flash size to 8 MB and the partitions to `default_8MB.csv`. For N8R2, also change the memory type to `qio_qspi`.
- 2 MB of PSRAM (N8R2) is enough for the 320 KB recording buffer and a 150 KB colour frame buffer, but tight next to the radio's stream buffer.
- Design the board as if GPIO 35–37 are taken: then any variant fits.

### Key specs (module datasheet)

| Item | Value |
|---|---|
| Size | 18.0 × 25.5 × 3.1 mm, PCB antenna at one end |
| Pins | 41 (castellated edge pins plus a ground pad underneath) |
| Supply | 3.0–3.6 V. The supply must deliver **at least 0.5 A** |
| Current peaks | About 355 mA while transmitting WiFi (802.11b at 20.5 dBm) |
| GPIO drive | About 40 mA source and 28 mA sink at 3.3 V, internal pull-ups and pull-downs about 45 kΩ |
| USB | Native USB on GPIO 19 (D−) and GPIO 20 (D+): flashing and serial log, no USB-serial chip needed |

### What the module needs around it

The DevKit board had all of this. On our own board we add it. From the module datasheet's peripheral schematic and the hardware design guidelines:

- **3.3 V regulator** that delivers at least 500 mA, with 22 µF + 0.1 µF next to the 3V3 pin.
- **EN (reset):** must not float. Use a 10 kΩ pull-up to 3.3 V and 1 µF to GND (a reset delay), plus a RESET button to GND.
- **GPIO 0 (BOOT):** a BOOT button to GND. Hold it while pressing RESET to enter the download mode.
- **USB-C for flashing:** D− to GPIO 19, D+ to GPIO 20, with 5.1 kΩ resistors from CC1 and CC2 to GND so a USB-C charger or computer supplies 5 V.
- **Antenna:** keep copper, metal and the battery away from the antenna end (the keep-out area is drawn in the datasheet). In the cube, the antenna end should face an acrylic wall, not the speaker or the battery.

### Pin rules (from the datasheet)

- Strapping pins **GPIO 0, 3, 45, 46**: they set the boot mode, so don't pull them at reset.
- **GPIO 35, 36, 37**: octal PSRAM on N8R8 and N16R8.
- **GPIO 19, 20**: USB. **GPIO 43, 44**: UART0 (serial log).
- Touch sensing works only on GPIO 1–14. Deep-sleep wake works only on GPIO 0–21.

### Pins the cube uses (from `esp32/src/config.h`)

| Function | GPIO |
|---|---|
| I2C SDA / SCL (accelerometer; the OLED on the prototype) | 8 / 9 |
| Touch strip | 4 |
| Accelerometer INT1 (motion wake) | 10 |
| Mic SCK / WS / SD | 5 / 6 / 7 |
| Amp BCLK / LRC / DIN / SD | 15 / 16 / 17 / 18 |
| USB D− / D+ | 19 / 20 |

Still free for the TFT: GPIO 1, 2, 11, 12, 13, 14, 21, 38, 39, 40, 41, 42, 47, 48 (and 35–37 on N8R2 only). The TFT pins are chosen in the circuit diagram.

---

## 2. 2.4" TFT LCD (ST7789V)

**Datasheet:** [ST7789V controller (Sitronix)][st7789v] · Panel spec: the seller's spec sheet (summarised below)

### Panel (seller spec sheet)

| Item | Value |
|---|---|
| Size | 2.4" |
| Resolution | 240 × 320 |
| Interface | 4-wire SPI |
| Controller | ST7789 (ST7789V) |
| Colour | Full colour (262K) |
| Active area | 36.72 × 48.96 mm |
| Panel size | 42.72 (W) × 60.26 (H) × 2.45 (D) mm |
| Supply | 3.3 V |
| View direction | 12 o'clock |
| Backlight | 4 white LEDs in parallel: Vf 2.8–3.2 V, 20 mA each (80 mA total) |
| Operating temperature | −20 to 70 °C |

Three versions were sold:

| Version | Connector | Pitch | What it needs |
|---|---|---|---|
| 10-pin bare, welded type | FPC tail, solder-on | 1.0 mm | A 10-pin 1.0 mm FPC connector or adapter board, and our own backlight driver |
| 10-pin bare, spliced type | FPC tail | 0.5 mm | A 10-pin 0.5 mm FPC connector or adapter board, and our own backlight driver |
| 8-pin module | Pin header | 2.54 mm | Plugs in. The backlight resistor is on the module |

### 10-pin bare FPC pinout

| Pin | Name | Function | Connect to |
|---|---|---|---|
| 1 | GND | Ground | GND |
| 2 | RST | Reset, active low | GPIO (or the EN reset net) |
| 3 | SCL | SPI clock | GPIO (SPI SCK) |
| 4 | RS | Register select (D/C): 0 = command, 1 = data | GPIO |
| 5 | CS | Chip select, active low | GPIO (SPI CS) |
| 6 | SDA | SPI data in (MOSI) | GPIO (SPI MOSI) |
| 7 | SDO | SPI data out (MISO) | Optional. Only needed to read the display back |
| 8 | VCC | Logic and analog supply | 3.3 V |
| 9 | A | Backlight anode (+) | 3.3 V through a current-limiting resistor |
| 10 | K | Backlight cathode (−) | GND through a transistor for on/off and PWM dimming |

### Controller (ST7789V datasheet)

| Item | Value |
|---|---|
| Analog supply VDD | 2.4–3.3 V |
| I/O supply VDDI | 1.65–3.3 V |
| Frame memory | 240 × 320 × 18 bit, built in |
| Colour formats | 16-bit RGB565 (65K) or 18-bit RGB666 (262K) |
| SPI write clock | Shortest cycle 66 ns in the datasheet (about 15 MHz). Many modules run at 40 MHz or more: test, and lower it if the image breaks up |

### Notes

- **Backlight:** 80 mA is more than a GPIO can drive (40 mA). Switch the cathode (K) with a small N-MOSFET or NPN transistor from a PWM pin. From 3.3 V, a resistor of a few ohms limits the current (for example (3.3 V − 3.0 V) / 80 mA ≈ 3.9 Ω). Check the real current with a multimeter.
- **Power:** the backlight is the biggest steady load after WiFi. Dim it or turn it off on the off face.
- **Memory:** a full colour frame is 240 × 320 × 2 bytes = 150 KB. It goes in PSRAM. Partial updates (only the part of the screen that changed) save SPI time.
- **Firmware:** the current screens use U8g2 for the mono OLED. The colour display needs a colour driver (for example LovyanGFX or TFT_eSPI) and new screen code.

---

## 3. ESP32-S3 DevKitC-1 N16R8 (prototype board)

**Docs:** [ESP32-S3-DevKitC-1 user guide][devkitc]

The board used so far: an ESP32-S3-WROOM-1 N16R8 with a 3.3 V regulator, two USB-C ports (native USB and USB-serial), and BOOT and RESET buttons. It is the reference for what the bare-module board must provide.

---

## 4. 1.3" OLED, SH1106 (prototype display)

**Datasheet:** [SH1106 controller][sh1106]

| Item | Value |
|---|---|
| Resolution | 128 × 64, white |
| Interface | I2C, address `0x3C` |
| Supply | 3.3 V |
| Wiring | GND · VCC → 3V3 · SCL → GPIO 9 · SDA → GPIO 8 |

Replaced by the 2.4" TFT in the bare-module build. The firmware selects SH1106 or SSD1306 with `OLED_SH1106` in `config.h`.

---

## 5. INMP441 I2S microphone

**Datasheet:** [INMP441 (TDK InvenSense)][inmp441]

| Item | Value |
|---|---|
| Output | 24-bit I2S, left-aligned in 32-bit slots |
| Supply | 1.8–3.3 V, about 1.4 mA |
| Performance | SNR 61 dBA, sensitivity −26 dBFS, 60 Hz – 15 kHz |
| Wiring | VDD → 3V3 · GND · L/R → GND (left channel) · SCK → GPIO 5 · WS → GPIO 6 · SD → GPIO 7 |

The mic clock on GPIO 5 raises the touch reading on GPIO 4: the firmware re-measures the touch baseline when the mic starts and stops.

---

## 6. MAX98357A I2S amplifier

**Datasheets:** [Adafruit MAX98357 guide (PDF)][max-ada] · [Analog Devices MAX98357A product page][max-adi]

| Item | Value |
|---|---|
| Output | Class D mono, 3.2 W into 4 Ω (10 % THD) at 5 V |
| Supply | 2.5–5.5 V on VIN. 5 V from the boost converter (much quieter at 3.3 V) |
| Gain | 9 dB with GAIN open (the setting used). 3, 6, 12 or 15 dB with a resistor |
| SD pin | Low = shutdown. Its voltage also picks the channel: by default (L+R)/2 |
| Wiring | VIN → 5 V · GND · BCLK → GPIO 15 · LRC → GPIO 16 · DIN → GPIO 17 · SD → GPIO 18 |

- Full volume can draw up to about 0.7 A from 5 V. The firmware caps the level (`SPK_MAX_AMPLITUDE`).
- The tones (Stage 4) and the radio share these pins: only one of them drives the amp at a time.

---

## 7. Speaker, 4 Ω 5 W, 40 mm

**Datasheet:** none published: see the [seller page][t-spk].

Full range, 4 Ω, rated 5 W. Glued inside the back lid behind the grille, about 20 mm deep. Connects to the amp's + and − output terminals (polarity doesn't matter for a single speaker).

---

## 8. MMA8452Q accelerometer (GY-45 board)

**Datasheet:** [MMA8452Q (NXP)][mma]

| Item | Value |
|---|---|
| Range | ±2, ±4 or ±8 g, 12-bit (the firmware uses ±2 g) |
| Supply | 1.95–3.6 V (I/O 1.6–3.6 V), 6–165 µA |
| Output rate | 1.56–800 Hz (50 Hz awake, 12.5 Hz for motion wake) |
| Interface | I2C, address `0x1C` or `0x1D` |
| Interrupts | INT1 and INT2: motion and freefall, used for wake from deep sleep |
| Wiring | VCC → 3V3 · GND · SDA → GPIO 8 · SCL → GPIO 9 · INT1 → GPIO 10 |

Mount it flat behind the screen, X to the right and Y up, to match the face directions in `config.h`.

---

## 9. Touch strip

Copper or aluminium foil tape, about 31 × 5.5 mm, inside the front panel, covered with Kapton tape. One short wire to GPIO 4 (touch channel 4). Keep the wire short and away from the I2C and I2S clock wires.

---

## 10. LiPo battery, 3.7 V 800 mAh

**Datasheet:** none published: see the [seller page][t-bat].

| Item | Value |
|---|---|
| Nominal / full | 3.7 V / 4.2 V |
| Capacity | 800 mAh |
| Size | 10 × 25 × 47 mm (measured) |
| Plug | JST-PH 2.0. Check the polarity with a multimeter: it is not standard between sellers |

Charge current should stay at or below 1C (800 mA). See the charger note below.

---

## 11. TP4056 USB-C charger with protection

**Datasheets:** [TP4056 charger IC][tp4056] · [DW01A protection IC][dw01a]

| Item | Value |
|---|---|
| Charging | Constant current, then constant voltage at 4.2 V (±1.5 %) |
| Charge current | Set by R_PROG: 1.2 kΩ = 1000 mA (the usual module default), 1.5 kΩ = 780 mA, 2 kΩ = 580 mA |
| Protection | DW01A + dual MOSFET: over-charge, over-discharge, short circuit |
| LEDs | Red = charging, green = full |
| Wiring | USB-C in · B+/B− → LiPo · OUT+/OUT− → boost converter IN |

**Check before use:** most TP4056 modules ship with 1.2 kΩ (marked `122`), which charges at 1 A: more than 1C for an 800 mAh cell. Replacing it with 1.5 kΩ (780 mA) or 2 kΩ (580 mA) is gentler on the battery.

---

## 12. Mini boost converter, 5 / 8 / 9 / 12 V

**Datasheet:** none from the seller: see the [seller page][t-boost]. Read the chip marking if a datasheet is needed.

| Item | Value |
|---|---|
| Input | The LiPo through the charger, 3.0–4.2 V |
| Output | Set by solder pads. **Ships at 12 V:** open pads A and B for 5 V, and measure before connecting anything |
| Feeds | 5 V to the amp and to the board (DevKit 5V pin now, the 3.3 V regulator on the bare-module board) |

---

## Power budget (rough)

| Load | Typical | Peak | Source |
|---|---|---|---|
| ESP32-S3, WiFi on | 80–100 mA (estimate) | 355 mA (WiFi transmit) | Peak: module datasheet |
| 2.4" TFT backlight | 80 mA (full) | 80 mA | Seller spec |
| TFT logic | A few mA (estimate) | — | — |
| MAX98357A + speaker | Near 0 (amp off) | About 0.7 A at full volume (estimate from 3.2 W at 5 V) | Adafruit guide |
| INMP441 | 1.4 mA | — | Datasheet |
| MMA8452Q | 0.165 mA | — | Datasheet |

From the battery side, through the 5 V boost and the 3.3 V regulator, expect roughly 0.3–0.4 A in normal use with the screen on, and short peaks above 1 A when the alarm or radio plays loud during a WiFi transmit. The boost converter and the battery protection must handle those peaks, or the cube resets. The 3.3 V regulator must be rated for at least 0.5 A (module datasheet), better 0.8–1 A for the ESP32 plus the backlight.

---

## Links

[wroom]: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
[s3chip]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
[s3trm]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
[hwguide]: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/index.html
[devkitc]: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html
[st7789v]: https://www.newhavendisplay.com/appnotes/datasheets/LCDs/ST7789V.pdf
[sh1106]: https://cdn.velleman.eu/downloads/29/infosheets/sh1106_datasheet.pdf
[inmp441]: https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf
[max-ada]: https://cdn-learn.adafruit.com/downloads/pdf/adafruit-max98357-i2s-class-d-mono-amp.pdf
[max-adi]: https://www.analog.com/en/products/max98357a.html
[mma]: https://www.nxp.com/docs/en/data-sheet/MMA8452Q.pdf
[tp4056]: https://dlnmh9ip6v2uc.cloudfront.net/datasheets/Prototyping/TP4056.pdf
[dw01a]: https://www.best-microcontroller-projects.com/support-files/dw01a.pdf

[t-devkit]: https://tronic.lk/product/esp32-s3-devkitc-1-dev-board-n16r8-wifi-bluetooth-iot
[t-oled]: https://tronic.lk/product/1-3-inch-128x64-oled-display-module-white-i2c-iic
[t-mic]: https://tronic.lk/product/inmp441-omnidirectional-microphone-module-i2s-interface
[t-amp]: https://tronic.lk/product/max98357-i2s-3w-class-d-amplifier-breakout-interface-da
[t-spk]: https://tronic.lk/product/4-ohm-5w-speaker-1-5-inch-for-portable-amplifiers
[t-accel]: https://tronic.lk/product/mma8452-3-axis-accelerometer-module-gy-45
[t-bat]: https://tronic.lk/product/3-7v-800mah-lipo-battery
[t-chg]: https://tronic.lk/product/tp4056-5v-1a-type-c-18650-lithium-battery-charging-modu
[t-boost]: https://tronic.lk/product/multi-function-mini-boost-module-step-up-board-58912v-1
