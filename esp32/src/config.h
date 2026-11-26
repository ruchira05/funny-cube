// Pins and constants for Key Cube (ESP32-S3 DevKitC-1 N16R8).
// No secrets here: WiFi, server URL, API key and device ID are entered on the
// phone setup page and stored in flash (see settings.h).
#pragma once

#define FW_VERSION "0.4.0-stage4"

// ---- Pins (see ESP32_PLAN.md, pin map) ----
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9
#define PIN_TOUCH 4       // Touch4
#define PIN_ACCEL_INT 10  // MMA8452 INT1: motion wake from deep sleep (RTC pin)
#define PIN_MIC_SCK 5     // INMP441 SCK (bit clock)
#define PIN_MIC_WS 6      // INMP441 WS (word select)
#define PIN_MIC_SD 7      // INMP441 SD (data). L/R pin to GND = left channel
// Speaker (Stage 4): MAX98357A I2S amplifier, 4 ohm 40 mm speaker
#define PIN_SPK_BCLK 15
#define PIN_SPK_LRC 16
#define PIN_SPK_DIN 17
#define PIN_SPK_SD 18  // amp shutdown: high = on

// ---- Display ----
// 1.3" 128x64 modules are usually SH1106. If the image is shifted by 2 px or
// garbled, set this to 0 to use the SSD1306 driver.
#define OLED_SH1106 1
// Rotation (quarter turns, 0..3 = U8G2_R0..R3) for screens that don't follow a
// face: boot, setup and configuration pages, and alerts while lying on the back.
// 2 = turned 180 degrees. The modes on the side faces use FACE_DIR_R0..R3 below.
#define DISPLAY_UPRIGHT_ROTATION 2

// ---- Touch strip ----
// On the ESP32-S3 the touchRead() value RISES when touched.
// Touched when raw > baseline * ON factor, released when raw < baseline * OFF factor.
// Tune these with the touch_cal test (pio run -e touch_cal -t upload -t monitor).
// Calibrated 2026-11-26 (touch_cal, new strip on GPIO4): untouched noise x1.000,
// hand on the cube but not the strip x1.02, finger x2.3-2.9 (mic off) and
// x1.9-2.4 (mic on, against the mic-on baseline). The mic clock raises the
// untouched reading x1.09; the baseline is re-measured when it starts or stops.
// ON is well above a gripping hand but low enough to catch a finger as it lands;
// OFF stays above the hand for hysteresis.
#define TOUCH_ON_FACTOR 1.15f
#define TOUCH_OFF_FACTOR 1.06f
#define TOUCH_DEBUG 1           // 1: print raw touch values every 500 ms
#define TOUCH_SAMPLE_MS 10      // sampling interval
#define TOUCH_FILTER_SAMPLES 3  // consecutive samples needed to change state
#define TOUCH_TAP_MAX_MS 800    // a press shorter than this is a "tap"
#define TOUCH_HOLD_MS 1500      // a press this long is a "hold" (stop alarm, reset timer, on/off)
// Re-measuring the baseline (e.g. when the mic clock starts and raises the
// untouched reading, x1.09 in touch.log): readings above baseline x this are taken
// as a finger, and the re-measure waits until it is lifted. Measured: mic jump
// x1.09, finger x2.6 against the mic-off baseline.
#define TOUCH_REBASE_MAX_FACTOR 1.35f

// ---- Boot gestures (touch held while powering on) ----
// After a power-on (not a touch wake) the strip must be touched fresh, within the
// window after the boot screen: a hand already on the cube at power-on (pressing
// RESET, holding the board) reads as a finger and is ignored.
#define BOOT_GESTURE_WINDOW_MS 2000
#define SETUP_HOLD_MS 3000   // hold 3 s: enter setup
#define RESET_HOLD_MS 10000  // hold 10 s: factory reset, then setup

// ---- WiFi ----
#define MAX_NETWORKS 5
#define WIFI_TOTAL_TIMEOUT_MS 20000     // try saved networks for up to 20 s at boot
#define WIFI_PER_NET_TIMEOUT_MS 10000   // per network
#define SETUP_CONNECT_TIMEOUT_MS 15000  // testing a network entered on the phone
#define SETUP_PORTAL_TIMEOUT_MS 300000  // 5 min with no phone, then retry saved networks
#define SETUP_SUCCESS_LINGER_MS 5000    // keep the hotspot up so the phone sees "Connected"
#define WIFI_LOST_GRACE_MS 30000        // auto-reconnect window before trying other networks

#define AP_PREFIX "KeyCube-"
#define AP_IP_STR "192.168.4.1"

// ---- Server ----
#define HTTP_CONNECT_TIMEOUT_MS 5000
#define HTTP_TIMEOUT_MS 8000

// ---- Time ----
#define DEFAULT_TZ "<+0530>-5:30"  // Sri Lanka (POSIX TZ string)
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"

// ---- Accelerometer (MMA8452, I2C 0x1C or 0x1D) ----
#define ACCEL_DEBUG 0             // 1: print raw and filtered g values every 500 ms
#define ACCEL_SAMPLE_MS 50        // read at 20 Hz (sensor runs at 50 Hz)
#define ACCEL_FILTER_ALPHA 0.3f   // low-pass filter: higher = faster, noisier
#define FACE_STABLE_MS 500        // same face for this long before switching
#define FACE_MIN_DOT 0.85f        // within about 30 degrees of a face direction
#define FACE_G_MIN 0.80f          // outside 0.8-1.2 g the cube is moving: ignore
#define FACE_G_MAX 1.20f
#define MOTION_WAKE_MARGIN_G 0.25f  // tilt needed to wake from the off face

// ---- Face directions ----
// What the accelerometer reads {x, y, z} (in g) when the cube rests on each face.
// To set them: with ACCEL_DEBUG 1, rest the cube on a face, copy the "filt"
// values from the serial log, rounded to 0, 1 or -1. Only the direction matters.
// Sensor mounting (seen from the front, screen upright): X points to the back
// (into the screen), Y to the right, Z down.
//
// Rn = the screen rotation that reads upright on that face (U8G2_R0..R3).
// To fix text that is upside down or sideways on a face, swap the directions of
// the two faces involved (e.g. R0 <-> R2). Which mode runs on each face is set in
// modes.cpp (MODE_FOR_FACE).
#define FACE_DIR_BACK        {-1,  0,  0}  // lying on the back, screen up  -> Off
#define FACE_DIR_SCREEN_DOWN { 1,  0,  0}  // lying on the screen           -> Off
#define FACE_DIR_R0          { 0,  0, -1}  // landscape
#define FACE_DIR_R1          { 0,  1,  0}  // portrait
#define FACE_DIR_R2          { 0,  0,  1}  // landscape, upside down
#define FACE_DIR_R3          { 0, -1,  0}  // portrait, the other way

// ---- Modes and power ----
#define OFF_SLEEP_DELAY_MS 3000   // on the off face this long, then deep sleep
#define STATUS_OVERLAY_MS 6000    // tap in a mode: show WiFi/server status this long
#define WIFI_RETRY_PAUSE_MS 60000 // all saved networks failed: wait, then retry
#define WAKE_FACE_TIMEOUT_MS 1500 // after wake, wait this long for a steady face

// ---- Key buddy (mode A): microphone and chat ----
#define MIC_SAMPLE_RATE 16000
#define MIC_SHIFT 14            // INMP441 24-bit in 32: >> 14 to int16. Quiet? try 13 or 12
#define MIC_SETTLE_MS 200       // drop audio for this long after the mic starts
#define AUDIO_DEBUG 1           // log level, length and peak of each recording
#define RECORD_MAX_MS 10000     // 10 s cap (320 KB in PSRAM)
#define RECORD_MIN_MS 300       // shorter press = tap (next page / status), not a recording
#define RECORD_LOCKOUT_MS 300   // after a recording, ignore touch this long
#define CHAT_TIMEOUT_MS 30000   // server reply (matches nginx proxy_read_timeout)
#define REPLY_PAGE_MS 5000      // long replies: next page after this long
#define REPLY_HOLD_MS 20000     // last page stays this long, then back to "Hold to talk"

// ---- Speaker (Stage 4): MAX98357A + 4 ohm 40 mm speaker ----
// The I2S clock runs all the time (silence when idle): its pins sit next to the
// touch pin on the header, so a clock that starts and stops would shift the touch
// reading. The amp itself is off (SD low) between sounds and in deep sleep.
#define SPK_SAMPLE_RATE 16000
#define SPK_MAX_AMPLITUDE 0.8f  // of full scale at volume 100 (caps the drive into the speaker)
#define SPK_DEFAULT_VOLUME 60   // 0..100, changed on the settings page
#define SPK_ALARM_MIN_VOLUME 30 // the alarm rings at least this loud, even at volume 0
#define SPK_AMP_WAKE_MS 10      // amp start-up after SD goes high
// Full volume draws up to about 0.7 A at 5 V. If the cube resets when the alarm
// rings on a weak USB port, lower the volume.

// ---- Time, alarms and reminders (Stage 4) ----
#define NTP_RESYNC_MS (6UL * 3600UL * 1000UL)  // re-sync every 6 hours
#define MAX_ALARMS 4
#define SNOOZE_MIN 5
#define ALARM_RING_MAX_MS 600000UL   // stop ringing after 10 minutes
#define ALARM_RAMP_MS 30000          // ring volume rises from 40% to 100% over 30 s
#define ALARM_LATE_MAX_S 600         // an alarm found up to 10 min late still rings
// Deep sleep keeps time with the chip's internal RC clock, which can drift by
// minutes over a night. So the cube wakes this long before an alarm, joins WiFi
// to correct the time, and waits awake (screen off) for it.
#define ALARM_PREWAKE_S 900
#define REMINDER_SHOW_MS 120000UL    // a reminder not answered in 2 min counts as missed
#define REMINDER_REPEAT_MS 60000UL   // chime again after 1 min
#define REMINDER_STALE_S 300         // due over 5 min ago (cube was off): skip, don't fire
#define NOTICE_SHOW_MS 10000UL       // focus timer notices in other modes
#define DEFAULT_REMIND_FROM 9        // reminders only between these hours
#define DEFAULT_REMIND_TO 18
#define DEFAULT_FOCUS_WORK_MIN 25
#define DEFAULT_FOCUS_BREAK_MIN 5
#define DEFAULT_FOCUS_LONG_MIN 15
#define FOCUS_LONG_EVERY 4           // a long break after every 4 focus periods

// ---- Settings page on the local network (Stage 4) ----
#define MDNS_NAME "keycube"          // http://keycube.local

// ================================================================
// DEV-ONLY: remove after development (grep "DEV-ONLY")
// ================================================================
// Stay awake (screen blank) on the off face while a computer is connected over
// USB, so upload and the serial monitor keep working. Set to 0 to test real
// deep sleep while on USB, or power the cube from a phone charger.
#define DEV_NO_SLEEP_ON_USB 1
