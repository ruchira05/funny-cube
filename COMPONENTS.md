# ESP32-S3 display module – Parts list and datasheets

A standalone PCB: an **ESP32-S3-WROOM-1** module driving a **2.4" 240 × 320 colour TFT (ST7789V)**, with a **LIS3DH accelerometer**, an **I2S MEMS microphone**, and power from **USB-C and a LiPo battery** (charging while in use). The board is **hand-soldered**, so through-hole and larger SMD parts are chosen wherever there is a choice.

The specs come from the datasheets linked at the end, or from the seller's spec sheet where no maker's datasheet exists.

---

## Decisions

| Topic | Choice |
|---|---|
| Display | 2.4" ST7789V, **10-pin welded type** (1.0 mm pitch FPC tail soldered onto PCB pads) |
| Accelerometer | **LIS3DH** (ST), I2C |
| Microphone | **ICS-43434** (TDK), I2S. Fallback: pads for an INMP441 breakout board |
| Power | USB-C and a 1-cell LiPo. Runs from USB when plugged in, from the battery otherwise, and charges at the same time |
| Assembly | Hand-soldered. Passives in 0805, through-hole connectors and switches |
| Not decided | microSD socket (optional footprint, can be left empty) |

---

## Bill of materials

### Main parts

| Ref | Part | Value / part number | Package | Qty | Purpose |
|---|---|---|---|---|---|
| U1 | ESP32-S3 module | ESP32-S3-WROOM-1 (N8R2, N8R8 or N16R8) | SMD module, 18 × 25.5 mm | 1 | Microcontroller, WiFi, BLE |
| DS1 | 2.4" TFT | ST7789V, 240 × 320, 10-pin welded | FPC tail, 1.0 mm pitch | 1 | Display |
| U5 | Accelerometer | LIS3DHTR | LGA-16, 3 × 3 mm | 1 | Tilt, motion, wake on movement |
| U6 | Microphone | ICS-43434 | LGA, 3.5 × 2.65 mm, bottom port | 1 | Voice recording |

### Display backlight

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| Q1 | N-MOSFET | AO3400A | SOT-23 | 1 | Switches the 80 mA backlight (a GPIO can only give 40 mA), PWM dimming |
| R1 | Resistor | 4.7 Ω (fit 3.9–6.8 Ω after measuring) | 0805 | 1 | Backlight current limit from 3.3 V |
| R2 | Resistor | 10 kΩ | 0805 | 1 | Q1 gate pull-down: backlight off while the ESP32 boots |

### ESP32-S3 support

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| C1 | Capacitor | 22 µF, 10 V | 0805 | 1 | 3V3 bulk decoupling at the module |
| C2 | Capacitor | 100 nF | 0805 | 1 | 3V3 decoupling at the module |
| R3 | Resistor | 10 kΩ | 0805 | 1 | EN pull-up (EN must not float) |
| C3 | Capacitor | 1 µF | 0805 | 1 | EN reset delay |
| SW1, SW2 | Tactile switch | 6 × 6 mm | Through-hole | 2 | RESET (EN) and BOOT (GPIO 0) |
| TP1–TP3 | Test pads | TX0, RX0, GND | — | 3 | Serial log on GPIO 43/44 if USB fails |
| Y1 | Crystal (optional) | 32.768 kHz | 3215 SMD | 0–1 | Accurate clock in deep sleep (GPIO 15/16) |
| C4, C5 | Capacitor (optional) | 12 pF | 0805 | 0–2 | Load capacitors for Y1 |

### USB-C input

| Ref | Part | Value / part number | Package | Qty | Purpose |
|---|---|---|---|---|---|
| J1 | USB-C socket | HRO TYPE-C-31-M-12 (16-pin, USB 2.0) | SMD, 0.5 mm pitch + through-hole shell | 1 | Power, charging, flashing, serial |
| R4, R5 | Resistor | 5.1 kΩ | 0805 | 2 | CC1/CC2 pull-downs: USB-C sources only give 5 V with these |
| U2 | ESD protection | USBLC6-2SC6 | SOT-23-6 | 1 | Protects D+/D− and VBUS from static |
| F1 | Resettable fuse | 1.1 A hold | 1206 | 1 | Short-circuit protection on VBUS |
| C6 | Capacitor | 10 µF, 10 V | 0805 | 1 | VBUS input |

### Battery charging and power-path

| Ref | Part | Value / part number | Package | Qty | Purpose |
|---|---|---|---|---|---|
| U3 | LiPo charger | MCP73831T-2ACI/OT (4.20 V) | SOT-23-5 | 1 | Charges the battery from USB |
| R6 | Resistor | 2.0 kΩ (500 mA charge) | 0805 | 1 | Sets the charge current: I = 1000 / R (kΩ) mA. Use 3.3 kΩ (300 mA) for a battery under 500 mAh |
| C7, C8 | Capacitor | 4.7 µF, 10 V | 0805 | 2 | Charger input and battery side |
| LED1 | LED | Red | 0805 | 1 | On while charging |
| R7 | Resistor | 1 kΩ | 0805 | 1 | LED current |
| D1 | Schottky diode | SS14 (1 A, 40 V) | SMA | 1 | Feeds the system from USB |
| Q2 | P-MOSFET | DMG2305UX | SOT-23 | 1 | Connects the battery to the system only when USB is absent |
| R8 | Resistor | 100 kΩ | 0805 | 1 | Q2 gate pull-down |
| J2 | Battery connector | JST-PH 2-pin, B2B-PH-K-S | Through-hole | 1 | LiPo plug |
| BT1 | LiPo battery | 3.7 V, 500–1200 mAh, **with built-in protection board** | Pouch | 1 | Battery (protection against over-discharge and short circuit is on the cell) |

### 3.3 V regulator and power switch

| Ref | Part | Value / part number | Package | Qty | Purpose |
|---|---|---|---|---|---|
| U4 | LDO regulator | AP2112K-3.3TRG1 (600 mA) | SOT-23-5 | 1 | 3.3 V for everything |
| C9, C10 | Capacitor | 10 µF, 10 V | 0805 | 2 | Regulator input and output |
| SW3 | Slide switch | SS-12D00 (SPDT) | Through-hole | 1 | On/off: switches the regulator's EN pin. Charging still works when off |
| R9 | Resistor | 100 kΩ | 0805 | 1 | EN pull-down: off when the switch is open |

### Sensing (battery and USB)

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| R10, R11 | Resistor | 100 kΩ + 100 kΩ | 0805 | 2 | Battery voltage ÷ 2 → GPIO 1 (ADC) |
| C11 | Capacitor | 100 nF | 0805 | 1 | Steadies the ADC reading |
| R12, R13 | Resistor | 100 kΩ + 100 kΩ | 0805 | 2 | USB present: VBUS ÷ 2 → GPIO 38 |

### Accelerometer support

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| C12 | Capacitor | 100 nF | 0805 | 1 | Decoupling |
| C13 | Capacitor | 10 µF | 0805 | 1 | Decoupling |
| R14, R15 | Resistor | 4.7 kΩ | 0805 | 2 | I2C pull-ups on SDA and SCL |

LIS3DH straps: CS → 3.3 V (I2C mode), SDO/SA0 → GND (I2C address `0x18`).

### Microphone support

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| C14 | Capacitor | 100 nF | 0805 | 1 | Decoupling |
| J3 | Header pads (fallback) | 1 × 6, 2.54 mm | Through-hole | 0–1 | For an INMP441 breakout board instead of U6 |

ICS-43434 strap: SELECT (L/R) → GND (left channel). Drill the sound hole under the mic as the datasheet's land pattern shows.

### microSD (optional, not decided)

| Ref | Part | Value | Package | Qty | Purpose |
|---|---|---|---|---|---|
| J4 | microSD socket | Push-push type | SMD | 0–1 | Long recordings, offline storage |
| R16–R20 | Resistor | 10 kΩ | 0805 | 0–5 | Pull-ups on CMD and D0–D3 |
| C15 | Capacitor | 10 µF | 0805 | 0–1 | Card supply (writes draw up to about 100 mA) |

---

## Pin map (ESP32-S3-WROOM-1)

| GPIO | Function | Note |
|---|---|---|
| 0 | BOOT button | Strapping pin |
| 1 | Battery voltage (÷ 2) | ADC1 channel 0 |
| 4 / 5 / 6 | Mic SCK / WS / SD | I2S |
| 7 | LIS3DH INT1 | Motion wake from deep sleep (RTC pin) |
| 8 / 9 | I2C SDA / SCL | LIS3DH |
| 10 | Display CS | FSPICS0 (IO MUX) |
| 11 | Display SDA (MOSI) | FSPID (IO MUX) |
| 12 | Display SCL (SCK) | FSPICLK (IO MUX) |
| 13 | Display RS (D/C) | |
| 14 | Display RST | |
| 15 / 16 | 32.768 kHz crystal (optional) | XTAL_32K_P / N |
| 17 | LIS3DH INT2 (optional) | |
| 19 / 20 | USB D− / D+ | Native USB |
| 21 | Backlight PWM → Q1 gate | LEDC PWM |
| 38 | USB present (VBUS ÷ 2) | Digital input |
| 39 / 40 / 41 | microSD CLK / CMD / D0 (optional) | SDMMC, 1-bit |
| 43 / 44 | UART0 TX / RX | Test pads |

Free: GPIO 2, 18, 42, 47, 48. Avoid GPIO 3, 45, 46 (strapping) and 35–37 (PSRAM on N8R8 and N16R8). Display SDO is not connected: the firmware never reads the display.

---

## Power path

```
USB-C VBUS ── F1 ──┬──────────── MCP73831 ──── VBAT ──┬── J2 ── LiPo (with protection)
  (USBLC6 on       │                                  │
   D+/D−/VBUS)     ├── D1 SS14 ──►┐                   │ Q2 drain
                   │              ├── VSYS ◄──────────┘ Q2 DMG2305UX (source → VSYS)
                   └── Q2 gate ◄──┼── (R8 100 kΩ to GND)
                                  │
                                  └── U4 AP2112K-3.3 ── 3V3 ──► ESP32, display, LIS3DH, mic
                                          EN ◄── SW3 from VSYS (R9 100 kΩ to GND)
```

- **Q2 orientation:** drain to the battery (VBAT), source to VSYS, gate to VBUS with R8 to GND. Wired the other way round, its body diode connects the battery to VSYS all the time.
- **With USB:** the system runs from USB through D1, and the charger charges the battery. VBUS holds Q2's gate high, so Q2 is off and the battery is disconnected from the system.
- **Without USB:** R8 pulls Q2's gate low, Q2 turns on, and the battery feeds the system.
- **USBLC6** sits across D+, D− and VBUS to ground. It is not in series with anything.
- **Charge LED:** VBUS → R7 → LED1 → STAT. STAT pulls low while charging, which lights the LED.
- **AP2112K dropout** is 250 mV at 600 mA (datasheet). From a battery it holds 3.3 V down to about 3.5 V, then follows the battery down. The ESP32 runs down to 3.0 V, so the board keeps working with a slightly lower supply.
- **Charge status:** MCP73831 STAT goes to the LED only. When charging is done, STAT rises to VBUS (5 V), so it must not go to an ESP32 pin. The firmware reads charging from the battery voltage and the USB-present pin instead.

---

## Display footprint (10-pin welded type)

From the seller's drawing:

| Item | Value |
|---|---|
| Contacts | 10, pin 1 on the left seen from the front |
| Pitch | 1.00 ± 0.03 mm |
| Contact width | 0.50 ± 0.03 mm |
| Exposed contact length | 2.00 mm |
| FPC tail width | 10.10 ± 0.1 mm |
| FPC thickness | 0.12 ± 0.03 mm |
| Tail position | Left edge of the tail 16.06 ± 0.3 mm from the panel's left edge |
| Tail length | Ends 18.05 ± 0.5 mm below the panel's bottom edge |
| Panel | 42.72 × 60.26 × 2.45 mm, active area 36.72 × 48.96 mm |

- PCB pads: 10 pads, 0.6 × 3.0 mm, at 1.0 mm pitch. Tin the pads, lay the tail flat with flux, and press each contact with the iron.
- The tail bends behind the panel, so the panel can sit on the PCB (or on a spacer) with the tail folded under it.

### Pinout

| Pin | Name | Function | Connects to |
|---|---|---|---|
| 1 | GND | Ground | GND |
| 2 | RST | Reset, active low | GPIO 14 |
| 3 | SCL | SPI clock | GPIO 12 |
| 4 | RS | Register select (D/C): 0 = command, 1 = data | GPIO 13 |
| 5 | CS | Chip select, active low | GPIO 10 |
| 6 | SDA | SPI data in (MOSI) | GPIO 11 |
| 7 | SDO | SPI data out (MISO) | Not connected |
| 8 | VCC | Logic and analog supply | 3.3 V |
| 9 | A | Backlight anode (+) | 3.3 V through R1 |
| 10 | K | Backlight cathode (−) | Q1 drain |

---

## Part details

### ESP32-S3-WROOM-1 module

**Datasheets:** [Module][wroom] · [ESP32-S3 chip][s3chip] · [Technical reference manual][s3trm] · [Hardware design guidelines][hwguide] · Reference design: [ESP32-S3-DevKitC-1][devkitc]

| Variant | Flash | PSRAM | GPIO 35–37 | Temperature | PlatformIO memory type |
|---|---|---|---|---|---|
| N8R2 | 8 MB | 2 MB (Quad) | Free | −40 to 85 °C | `qio_qspi` |
| N8R8 | 8 MB | 8 MB (Octal) | Used by PSRAM | −40 to 65 °C | `qio_opi` |
| N16R8 | 16 MB | 8 MB (Octal) | Used by PSRAM | −40 to 65 °C | `qio_opi` |

- Size 18.0 × 25.5 × 3.1 mm, 41 pins, PCB antenna at one end. Supply 3.0–3.6 V, at least 0.5 A. About 355 mA peak while transmitting WiFi.
- Check the marking on the metal shield for the variant. The pin map above works for all three.
- Keep copper, the battery and the display's metal frame away from the antenna end. Leave the PCB under the antenna without copper (keep-out area in the module datasheet).

### 2.4" TFT (ST7789V)

**Datasheet:** [ST7789V controller][st7789v] · Panel: seller's spec sheet

| Item | Value |
|---|---|
| Resolution | 240 × 320, 262K colours |
| Interface | 4-wire SPI |
| Controller supply | VDD 2.4–3.3 V, VDDI 1.65–3.3 V |
| SPI write clock | Shortest cycle 66 ns in the datasheet (about 15 MHz). Many panels run at 40 MHz or more: test, and lower it if the image breaks up |
| Backlight | 4 white LEDs in parallel: Vf 2.8–3.2 V, 20 mA each (80 mA total) |
| Operating temperature | −20 to 70 °C |

A full frame is 240 × 320 × 2 bytes = 150 KB, which fits in PSRAM on all three module variants. Use a colour driver such as LovyanGFX or TFT_eSPI.

### LIS3DH accelerometer

**Datasheet:** [LIS3DH][lis3dh]

| Item | Value |
|---|---|
| Range | ±2, ±4, ±8, ±16 g, up to 12-bit |
| Supply | 1.71–3.6 V |
| Current | From about 2 µA (low-power mode, 1 Hz) to 11 µA (50 Hz) |
| Interface | I2C (address `0x18` with SA0 low) or SPI |
| Interrupts | INT1 and INT2: motion, free-fall, click, 6D orientation |
| Package | LGA-16, 3 × 3 × 1 mm |

### ICS-43434 microphone

**Datasheet:** [ICS-43434][ics43434]

| Item | Value |
|---|---|
| Output | 24-bit I2S |
| Supply | 1.65–3.63 V |
| Performance | SNR 65 dBA, sensitivity −26 dBFS |
| Package | LGA, 3.5 × 2.65 × 0.98 mm, bottom sound port (hole through the PCB) |

### MCP73831 charger

**Datasheet:** [MCP73831][mcp73831]

Linear 1-cell Li-ion/LiPo charger in SOT-23-5. Charge voltage 4.20 V (the -2 variant). Charge current 15–500 mA, set by R_PROG: I = 1000 / R_PROG (kΩ) mA, so 2.0 kΩ = 500 mA and 10 kΩ = 100 mA.

### AP2112K-3.3 regulator

**Datasheet:** [AP2112][ap2112]

600 mA LDO with enable, SOT-23-5. Dropout 250 mV typical at 600 mA, output ±1.5 %, 55 µA quiescent current.

### DMG2305UX P-MOSFET

**Datasheet:** [DMG2305UX][dmg2305]

−20 V, −4.2 A, SOT-23. Gate threshold −0.5 to −0.9 V, so it turns fully on from a 3.0–4.2 V LiPo.

### Other parts

| Part | Datasheet |
|---|---|
| AO3400A N-MOSFET | [AO3400A][ao3400a] |
| USBLC6-2SC6 ESD protection | [USBLC6-2][usblc6] |
| SS14 Schottky diode | [SS12–SS16][ss1x] |
| HRO TYPE-C-31-M-12 USB-C socket | [TYPE-C-31-M-12][usbc] |

---

## Hand-soldering notes

| Part | Difficulty | How |
|---|---|---|
| 0805 passives, SOT-23, SOT-23-5/6, SMA | Easy | Iron, fine tip, flux |
| Through-hole switches and connectors | Easy | Iron |
| USB-C socket (0.5 mm pitch pins) | Medium | Drag-solder with plenty of flux, then clean bridges with solder wick. Solder the shell tabs first |
| ESP32-S3 module | Medium | Castellated edge pins with an iron. The ground pad underneath needs hot air, or a via hole through the PCB to solder it from the back |
| Display FPC tail | Medium | Tin the pads, flux, press each contact with the iron |
| **LIS3DH (LGA-16)** | **Hard** | Pads are under the chip: needs hot air or a hot plate with solder paste. An iron alone does not work |
| **ICS-43434 (LGA)** | **Hard** | Same as LIS3DH. Never put flux or solder near the sound hole, and don't wash the mic |

If hot air isn't available, the fallback header J3 takes an INMP441 breakout board for the mic. For the accelerometer, a LIS3DH breakout board on pin headers works too.

---

## Power budget (rough)

| Load | Typical | Peak | Source |
|---|---|---|---|
| ESP32-S3, WiFi on | 80–100 mA (estimate) | 355 mA (WiFi transmit) | Peak: module datasheet |
| Display backlight | 80 mA (full) | 80 mA | Seller spec |
| Display logic | A few mA (estimate) | — | — |
| LIS3DH | 0.011 mA | — | Datasheet |
| ICS-43434 | Under 1 mA | — | Datasheet |
| microSD (if fitted) | — | About 100 mA while writing | Typical for SD cards |

About 0.2 A typical and 0.45–0.55 A peak on the 3.3 V rail, within the AP2112K's 600 mA. A 1000 mAh battery gives roughly 5 hours with the screen and WiFi on, and much longer with deep sleep between uses.

---

## Links

[wroom]: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
[s3chip]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
[s3trm]: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
[hwguide]: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/index.html
[devkitc]: https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html
[st7789v]: https://www.newhavendisplay.com/appnotes/datasheets/LCDs/ST7789V.pdf
[lis3dh]: https://www.st.com/resource/en/datasheet/lis3dh.pdf
[ics43434]: https://invensense.tdk.com/wp-content/uploads/2016/02/DS-000069-ICS-43434-v1.2.pdf
[mcp73831]: https://ww1.microchip.com/downloads/en/DeviceDoc/MCP73831-Family-Data-Sheet-DS20001984H.pdf
[ap2112]: https://www.diodes.com/assets/Datasheets/AP2112.pdf
[dmg2305]: https://www.diodes.com/assets/Datasheets/DMG2305UX.pdf
[ao3400a]: https://www.aosmd.com/res/datasheets/AO3400A.pdf
[usblc6]: https://www.st.com/resource/en/datasheet/usblc6-2.pdf
[ss1x]: https://www.vishay.com/docs/88746/ss12.pdf
[usbc]: https://www.lcsc.com/datasheet/lcsc_datasheet_1811131825_Korean-Hroparts-Elec-TYPE-C-31-M-12_C165948.pdf
