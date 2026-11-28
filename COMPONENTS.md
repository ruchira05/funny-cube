# ESP32-S3 display module – Components and datasheets

A standalone module: an **ESP32-S3-WROOM-1** module driving a **2.4" 240 × 320 colour TFT (ST7789V)** over SPI. This file lists both parts with their key specs, pins and datasheets, and the support parts the bare module needs around it. The specs come from the datasheets linked below, or from the seller's spec sheet where no maker's datasheet exists.

---

## Summary

| # | Part | Key spec | Supply | Interface | Datasheet | Bought from |
|---|---|---|---|---|---|---|
| 1 | ESP32-S3-WROOM-1 (N8R2 / N8R8 / N16R8) | Dual-core 240 MHz, WiFi 2.4 GHz, BLE 5, PCB antenna | 3.0–3.6 V, ≥ 0.5 A | 41 pins, castellated | [Module][wroom] | EC Buying (online) |
| 2 | 2.4" TFT LCD, ST7789V | 240 × 320, 262K colours, 4 white LED backlight | 3.3 V | 4-wire SPI | [ST7789V][st7789v] | EC Buying (online) |

---

## 1. ESP32-S3-WROOM-1 module

**Datasheets:** [ESP32-S3-WROOM-1/1U module][wroom] · [ESP32-S3 chip][s3chip] · [Technical reference manual][s3trm] · [Hardware design guidelines][hwguide] · Reference design: [ESP32-S3-DevKitC-1][devkitc]

### Which variant

The listing sold three variants. They differ only in memory, but that decides which pins are free and how the firmware is built. **Check the marking on the metal shield** (e.g. `ESP32-S3-WROOM-1 N16R8`).

| Variant | Flash | PSRAM | GPIO 35, 36, 37 | Temperature | PlatformIO memory type |
|---|---|---|---|---|---|
| N8R2 | 8 MB (Quad) | 2 MB (Quad) | Free | −40 to 85 °C | `qio_qspi` |
| N8R8 | 8 MB (Quad) | 8 MB (Octal) | **Used by PSRAM** | −40 to 65 °C | `qio_opi` |
| N16R8 | 16 MB (Quad) | 8 MB (Octal) | **Used by PSRAM** | −40 to 65 °C | `qio_opi` |

- PlatformIO: `board = esp32-s3-devkitc-1`, then set `board_upload.flash_size` (8MB or 16MB), `board_build.partitions` (`default_8MB.csv` or `default_16MB.csv`), `board_build.arduino.memory_type` from the table, and `-DBOARD_HAS_PSRAM`. With the wrong memory type, PSRAM reads as 0 or the board boot-loops.
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

A bare module has no regulator, buttons or USB connector. From the module datasheet's peripheral schematic and the hardware design guidelines:

- **3.3 V regulator** that delivers at least 500 mA (better 0.8–1 A with the display backlight), with 22 µF + 0.1 µF next to the 3V3 pin.
- **EN (reset):** must not float. Use a 10 kΩ pull-up to 3.3 V and 1 µF to GND (a reset delay), plus a RESET button to GND.
- **GPIO 0 (BOOT):** a BOOT button to GND. Hold it while pressing RESET to enter the download mode.
- **USB-C for power and flashing:** D− to GPIO 19, D+ to GPIO 20, with 5.1 kΩ resistors from CC1 and CC2 to GND so a USB-C charger or computer supplies 5 V.
- **Antenna:** keep copper, metal and batteries away from the antenna end (the keep-out area is drawn in the module datasheet).

### Pin rules (from the datasheet)

- Strapping pins **GPIO 0, 3, 45, 46**: they set the boot mode, so don't pull them at reset.
- **GPIO 35, 36, 37**: octal PSRAM on N8R8 and N16R8.
- **GPIO 19, 20**: USB. **GPIO 43, 44**: UART0 (serial log).
- Touch sensing works only on GPIO 1–14. Deep-sleep wake works only on GPIO 0–21.

### Suggested pins for the display

The SPI2 (FSPI) peripheral has fixed IO MUX pins on GPIO 10–13. Using them gives the fastest SPI clock, without going through the GPIO matrix.

| Display signal | GPIO | Note |
|---|---|---|
| SCL (SPI clock) | 12 | FSPICLK |
| SDA (SPI MOSI) | 11 | FSPID |
| CS | 10 | FSPICS0 |
| SDO (SPI MISO) | 13 | FSPIQ, optional |
| RS (D/C) | 9 | Any free GPIO |
| RST | 14 | Any free GPIO, or tie to the module's EN reset |
| Backlight PWM (to the transistor) | 21 | Any free GPIO with PWM (LEDC) |

These are a suggestion, not fixed yet. The final pins are set in the circuit diagram.

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
| 10-pin bare, welded type | FPC tail, solder-on | 1.0 mm | A 10-pin 1.0 mm FPC connector or adapter board, and a backlight driver |
| 10-pin bare, spliced type | FPC tail | 0.5 mm | A 10-pin 0.5 mm FPC connector or adapter board, and a backlight driver |
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
- **Memory:** a full colour frame is 240 × 320 × 2 bytes = 150 KB. It fits in PSRAM on all three module variants. Partial updates (only the part of the screen that changed) save SPI time.
- **Firmware:** a colour driver library that supports the ST7789, for example LovyanGFX or TFT_eSPI.

---

## Power budget (rough)

| Load | Typical | Peak | Source |
|---|---|---|---|
| ESP32-S3, WiFi on | 80–100 mA (estimate) | 355 mA (WiFi transmit) | Peak: module datasheet |
| Display backlight | 80 mA (full) | 80 mA | Seller spec |
| Display logic | A few mA (estimate) | — | — |

Plan for about 0.2 A typical and 0.45 A peak on the 3.3 V rail. A 3.3 V regulator rated for 0.8–1 A leaves margin.

---

## Links

[wroom]: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
[s3chip]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
[s3trm]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
[hwguide]: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/index.html
[devkitc]: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html
[st7789v]: https://www.newhavendisplay.com/appnotes/datasheets/LCDs/ST7789V.pdf
