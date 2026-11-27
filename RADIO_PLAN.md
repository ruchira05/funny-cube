# Radio mode plan

**Status:** implemented in firmware 0.5.0-radio (`radio.cpp`, `radioscreen.cpp`). Not yet tested on the cube: run the checklist in section 10. Differences from the plan as first written:

- The default station list lives in `settings.cpp` (`DEFAULT_STATIONS`).
- Touching the strip while paused or after the sleep timer plays again (tap or hold).
- No `Icon::Radio`: the radio draws its own screens, and the mode banner isn't used.
- The mic files were renamed `audio.*` → `mic.*`, because macOS (case-insensitive) confused `audio.h` with the library's `Audio.h`.
- The library always outputs 48 kHz, so the touch baseline only changes when the decoder starts or stops, not per station.
- The audio library (ESP32-audioI2S) is GPL-3.0 licensed. That matters only if the firmware is distributed.

Internet radio on the cube. It plays when the cube rests on its **back** (the speaker side), with the screen facing up. Stations come from `selectedChannels.md` and are edited on the web settings page.

## 1. Faces after this change

| Resting on | Before | After |
|---|---|---|
| Back (screen up, speaker down) | Off (sleeps) | **Radio** |
| Screen down | Off (sleeps) | Off (sleeps), now the only off position |
| R0..R3 sides | Health, Key buddy, Focus, Alarm | Unchanged |

- Laying the cube on its screen is now the only way to put it to sleep. Motion wake already handles the screen-down position (`armMotionWake()` picks whichever off direction the cube is lying in).
- The radio screen uses `DISPLAY_UPRIGHT_ROTATION`, the same rotation as alerts on the back face.
- Health reminders currently skip the back face because it means "off". After this change they will fire during radio, pausing it. Section 7 has a setting to turn that off.

## 2. Touch controls (radio face)

| Gesture | Action |
|---|---|
| Tap | Next station (wraps to the first) |
| Hold (1.5 s, `TOUCH_HOLD_MS`) | Pause or resume |

Volume and the station list are changed on the web page. Touch has no double tap, and adding one would delay every single tap by about 300 ms, so there is no "previous station" gesture.

## 3. Audio playback

**Library:** [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) (schreibfaul1). It handles HTTP and HTTPS streams, Icecast/Shoutcast with ICY metadata, redirects and playlists, and decodes MP3, AAC and AAC+ (HE-AAC). It also reports the current song title and a VU level. Its buffer goes in PSRAM. I'll pin a release that supports this Arduino core (3.x, `ESP_I2S`).

**Sharing the I2S port with `speaker.cpp`.** Only one driver can own the speaker pins (15/16/17) at a time.

- New `Speaker::release()` stops the tone task, calls `i2s.end()` and keeps the amp off.
- New `Speaker::reclaim()` reverses this.
- Entering radio: `speaker.release()`, then the radio library opens I2S on the same pins and switches the amp on (SD pin 18 high).
- Leaving radio, or when an alert starts: stop the stream, close I2S, then `speaker.reclaim()`.
- The I2S clock stops briefly at each handover, and the stream's sample rate (often 44.1 or 22.05 kHz, not 16 kHz) shifts the touch reading. So the radio calls `touch.rebaseline()` after each start, stop and sample-rate change, like the mic does.

**Task:** the decoder runs in its own FreeRTOS task, high priority, on core 1 next to the main loop, with core 0 left for WiFi. The main loop only draws and handles touch, so display updates can't cause gaps in the audio.

**Volume:**
- **Setting:** a separate radio volume (0..100) on the settings page, because a comfortable music volume differs from a beep volume.
- **Cap:** it's limited by `SPK_MAX_AMPLITUDE` like the tones.
- **Curve:** it's squared the same way, so it sounds even across the range.

**Station-specific details (already found):**
- **SomaFM:** rejects `curl`'s default user agent, so the radio sends `KeyCube-Radio/1.0`.
- **Shoutcast servers** (for example `live.trusl.com:1150`): they return an HTML page at `/` to some clients. Seeded URLs use the `/stream` path.
- **HTTPS:** each TLS stream costs about 40 KB of internal RAM. Key buddy never runs at the same time, so this fits.

## 4. Station list

**Storage:**
- **Where:** NVS, as one JSON blob of `{name, url}` entries (ArduinoJson is already a dependency).
- **Limit:** 20 stations (`RADIO_MAX_STATIONS`).
- **Also saved:** the last station played, so the radio resumes it.

**Seed list.** On first boot, or after a factory reset, the radio loads the stations from `selectedChannels.md`, with two corrections:
- The duplicate Gold FM entry is removed.
- Yes FM's URL is `http://live.trusl.com:1150/stream`, because the bare `/` returns a web page.

| # | Name | URL |
|---|---|---|
| 1 | Gold FM | `https://radio.lotustechnologieslk.net:2020/stream/goldfmgarden` |
| 2 | SomaFM Indie Pop | `http://ice1.somafm.com/indiepop-128-mp3` |
| 3 | SomaFM Drone Zone | `http://ice1.somafm.com/dronezone-128-mp3` |
| 4 | SomaFM Secret Agent | `http://ice1.somafm.com/secretagent-128-mp3` |
| 5 | SomaFM Deep Space | `http://ice1.somafm.com/deepspaceone-128-mp3` |
| 6 | Radio Paradise | `http://stream.radioparadise.com/mp3-128` |
| 7 | FIP | `https://icecast.radiofrance.fr/fip-midfi.mp3` |
| 8 | BBC World Service | `https://stream.live.vc.bbcmedia.co.uk/bbc_world_service` |
| 9 | WNYC | `http://fm939.wnyc.org/wnycfm` |
| 10 | Kiss FM | `https://srv02.onlineradio.voaplus.com/kissfm` |
| 11 | Sun FM | `https://radio.lotustechnologieslk.net:2020/stream/sunfmgarden` |
| 12 | Gold FM (backup) | `https://radio.lotustechnologieslk.net:8000/` |
| 13 | Yes FM | `http://live.trusl.com:1150/stream` |
| 14 | Legends 96.6 | `https://mbc.thestreamtech.com:8585/stream` |

**Failures:**
- **Retries:** if a station fails to connect, or plays nothing for 10 s, the radio retries twice with a short pause.
- **Error screen:** after that it shows "Station unavailable" and waits for a tap. It doesn't skip on its own, so a dead station doesn't hop through the list unnoticed.
- **No WiFi:** the screen says so, and the radio starts as soon as the network is back.

## 5. Screens and animations (128 × 64, screen up)

```
+--------------------------------+
| ((·))  RADIO          3/14  ▂▄▆ |  status line: icon, station number, WiFi signal
|                                |
|        SomaFM Drone Zone       |  station name, large font; scrolls if too wide
|  ~ Now: Artist - Song Title ~  |  ICY song title, scrolling marquee
|                                |
| ▁▃▅▇▅▃▁▂▄▆█▆▄▂▁▃▅▇▅▃▁▂▄▆▄▂▁▂▄ |  animated level bars
+--------------------------------+
```

| State | Animation |
|---|---|
| Connecting / buffering | Radio waves pulse out of an antenna icon, with "Tuning in…" and the station name |
| Playing | Level bars move with the real VU level from the library (smoothed, with falling peak dots). The song title scrolls when the station sends one |
| Station change (tap) | The old name slides out to the left and the new one slides in (about 250 ms), then connecting starts |
| Paused (hold) | Large pause icon; the bars sink to the floor |
| Unavailable / no WiFi | Antenna icon with a cross, the reason, and "Tap: next" |
| Volume changed on the web page | Volume bar for 2 s |

- **Frame rate:** about 20 fps while animating, and lower when nothing changes. The animation clock runs on `millis()`, so drawing at a slower rate never changes the speed.
- **Code:** drawing goes in a new `radioscreen.cpp`, so `display.cpp` doesn't grow; it uses `display.gfx()` and the existing font helpers.

## 6. Mode integration (`modes.cpp`)

- **New mode:** `Mode::Radio` in the face table for `Face::Back`, with a new `Display::Icon::Radio`. `Face::Back` currently means Off in several checks, so those change:
  - **Off becomes screen down only:** a new `Face::ScreenDown`, or `classify()` keeps them apart. Today screen down is folded into `Face::Back`.
  - **Sleep:** only the screen-down position starts the sleep countdown.
  - **Schedule:** `schedule.loop(offFace, …)` treats radio as "on" (alarms and reminders allowed).
  - **Alerts:** an alert pauses the radio (release I2S, then play the alert). When it ends and the cube is still on its back, the radio resumes the same station.
- **Leaving the face:** turning the cube off its back stops the stream and frees the memory. The focus timer keeps running, as it does on other faces.
- **Sleep timer (optional setting):** stop the radio after N minutes, like a bedside radio. The screen then dims to a clock, but the cube doesn't sleep, because it isn't lying on its screen.

## 7. Web settings page (`webconfig.cpp`): new "Radio" card

- **Station table:** name and URL fields per row, with Up, Down and Delete buttons, and an "Add station" row. Up to 20 stations.
- **Station checks:** each URL must start with `http://` or `https://`. Names are cut to 24 characters.
- **Radio volume:** a 0–100 slider.
- **Sleep timer:** Off, 15, 30, 60 or 90 min.
- **Reminders during radio:** on or off.
- **"Play on cube" button for each row:** switches the cube to that station now, if it's in radio mode. This tests a URL quickly.
- **"Reset to default list" button:** restores the seed list.
- **Reminders text:** the help text changes to "not while the cube lies on its screen (off)".

Each row submits as indexed fields (`sn0`/`su0` … `sn19`/`su19`) in the existing Save form, so no new request type is needed. "Play on cube" is a small `POST /radio/play?i=N`.

## 8. Files

| File | Change |
|---|---|
| `platformio.ini` | Add `schreibfaul1/ESP32-audioI2S` (pinned version) |
| `config.h` | `RADIO_*` constants: max stations, retry and timeout values, default volume, user agent, frame interval |
| `radio.h/.cpp` (new) | Player: audio task, connect and retry, pause and resume, station switching, ICY title, VU level, I2S handover, touch rebaseline |
| `radioscreen.h/.cpp` (new) | All radio screens and animations |
| `radiomode` in `radio.cpp` | Mode wrapper: `enter()`, `exit()`, `loop()` (touch to player, player state to screen) |
| `speaker.h/.cpp` | `release()` / `reclaim()` |
| `orientation.h/.cpp` | Keep screen down separate from back |
| `modes.h/.cpp` | `Mode::Radio` on the back face, off and sleep only on screen down, alert pause and resume |
| `settings.h/.cpp` | Station list JSON, last station, radio volume, sleep timer, reminders-during-radio flag, seed list |
| `webconfig.cpp` | Radio card, `/radio/play` |
| `display.h/.cpp` | `Icon::Radio` for the mode banner |
| `ESP32_PLAN.md` | A Stage 5 radio section and a face table update |

## 9. Build order

1. **Player only:** add the library, plus `Speaker::release/reclaim` and a small `radio_test` build env that plays station 1 from serial commands (`n` = next, `p` = pause). This shows the sound, the I2S handover and the touch shift early.
2. **Mode:** split off screen down from Off, add `Mode::Radio` on the back face, touch controls and a plain text screen.
3. **Station storage:** settings, seed list and the web Radio card.
4. **Animations:** the screens and animations, tuned on the real OLED.
5. **Alerts and power:** alarm and reminder pause and resume, retries, the no-WiFi state and the sleep timer.
6. **Docs:** update `ESP32_PLAN.md` and run the test checklist.

## 10. Test checklist

- [ ] Back face plays the last station within about 3 s on a good WiFi connection; screen down still sleeps, and tilting wakes it.
- [ ] Every seeded station plays, MP3 and AAC+ alike, including SomaFM (user agent) and Yes FM (`/stream`).
- [ ] Tap changes the station; hold pauses and resumes; no stray taps after a hold.
- [ ] Touch still works while streaming and after switching between stations with different sample rates.
- [ ] An alarm during radio rings at alarm volume, and the radio resumes after it's dismissed.
- [ ] Turning the cube to another face stops the audio at once, and that mode's tones still work.
- [ ] A dead URL shows "Station unavailable" and doesn't freeze or reboot.
- [ ] WiFi drop: the screen shows no WiFi, and playback resumes when the network returns.
- [ ] Web page: add, reorder, delete and play-on-cube work; the list survives a reboot; reset restores the seed list.
- [ ] One hour of streaming: no audio gaps from display updates, free heap stays steady, and the amp and speaker don't overheat.

## 11. Risks

| Risk | Mitigation |
|---|---|
| The radio library and `speaker.cpp` both claim I2S | A clear handover (release / reclaim); never both at once |
| Touch reading shifts with the I2S clock rate | `touch.rebaseline()` after every start, stop and rate change |
| HTTPS streams use too much internal RAM | Prefer the `http://` URL where a station offers one; buffers in PSRAM; key buddy never runs at the same time |
| Audio stutters while the screen redraws | Decoder in its own high-priority task; frame-rate limit; full redraw only when something changed |
| Station URLs change (common for Sri Lankan stations) | Editable list, a "Play on cube" test button and a clear error screen |
| A small speaker at high volume distorts | Radio volume capped by `SPK_MAX_AMPLITUDE`; GAIN pin left at 9 dB |
| Back face no longer sleeps, so battery use rises | Sleep timer, and screen down as the off position (the cube is USB powered for now) |
