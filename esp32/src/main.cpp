// Key Cube firmware, Stage 4: clock, alarm, health reminders and focus timer
// (on top of Stage 1 WiFi setup, Stage 2 orientation and Stage 3 key buddy).
//
// Power-on:
//   - boot screen says "Hold strip: setup": touch it within 2 s and hold 3 s -> setup,
//     10 s -> factory reset. A hand already on the strip at power-on is ignored.
//   - no saved networks -> setup mode
//   - otherwise connect to saved networks (strongest first); on failure -> setup mode
// Running: the face the cube stands on selects the mode, the screen rotates to match.
//   - back face (screen up): screen off, deep sleep after 3 s
//   - side faces: A key buddy, B alarm, C health reminders, D focus timer and clock
//   - alarms ring in every mode (and wake the cube); reminders in the side modes
//   - settings page for alarms, reminders, timer and volume: http://keycube.local
// Wake (tilt, touch or the alarm timer): read the face; still on the back face and
//   no alarm due soon -> sleep again; otherwise show the mode at once and reconnect
//   WiFi in the background.
//   After a touch wake, keep holding the strip 3 s / 10 s for setup / factory reset.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "alarms.h"
#include "config.h"
#include "display.h"
#include "modes.h"
#include "net.h"
#include "orientation.h"
#include "power.h"
#include "settings.h"
#include "speaker.h"
#include "touch.h"
#include "webconfig.h"
#include "wifi_setup.h"

namespace {

enum class State { Setup, Run };
enum class SetupScreen { JoinQR, UrlQR };
enum class Gesture { None, Setup, FactoryReset };

State state = State::Setup;
SetupScreen setupScreen = SetupScreen::JoinQR;
bool setupDirty = true;
bool lastPhone = false;
uint32_t lastSpin = 0;

void printBoardInfo() {
  Serial.println();
  Serial.println("=== Key Cube " FW_VERSION " ===");
  Serial.printf("Chip:   %s rev %d, %d cores @ %d MHz\n", ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), (int)ESP.getCpuFreqMHz());
  Serial.printf("Flash:  %u MB (expect 16)\n", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  Serial.printf("PSRAM:  %u MB (expect about 8)\n", (unsigned)(ESP.getPsramSize() / (1024 * 1024)));
  Serial.printf("Heap:   %u KB free\n", (unsigned)(ESP.getFreeHeap() / 1024));
  if (ESP.getPsramSize() == 0)
    Serial.println("WARNING: no PSRAM. Check memory_type = qio_opi and -DBOARD_HAS_PSRAM.");
}

// The display driver has already started I2C on GPIO 8/9.
void scanI2C() {
  Serial.print("I2C:    ");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", a);
      found++;
    }
  }
  Serial.println(found ? "(OLED 0x3C, MMA8452 0x1C or 0x1D)" : "nothing found: check wiring");
}

// Touch held while powering on or waking. Uses the baseline stored on a previous boot.
// freshTouchOnly (power-on): only a touch that starts after the boot screen counts.
// After a touch wake the finger that woke the cube is the start of the hold.
Gesture readBootGesture(bool freshTouchOnly) {
  touch.waitSettled(6000);
  Serial.printf("[boot] touch raw %u, stored baseline %u (counts as touched above %u)\n",
                (unsigned)touch.raw(), (unsigned)touch.baseline(),
                (unsigned)(touch.baseline() * TOUCH_ON_FACTOR));
  if (freshTouchOnly) {
    if (touch.rawTouched()) {
      Serial.println("[boot] touch present at power-on, ignoring it (a hand on the cube?)");
      display.showBoot("Release the strip");
      uint32_t w0 = millis();
      while (!touch.rawReleased() && millis() - w0 < 30000) delay(20);
    }
    display.showBoot("Hold strip: setup");
    uint32_t w0 = millis();
    while (millis() - w0 < BOOT_GESTURE_WINDOW_MS && !touch.rawTouched()) delay(10);
  }
  if (!touch.rawTouched()) return Gesture::None;
  delay(50);
  if (!touch.rawTouched()) return Gesture::None;  // ignore a single spike

  Serial.println("[boot] touch held at start");
  display.on();
  display.setRotation(0);
  uint32_t t0 = millis();
  while (!touch.rawReleased()) {
    uint32_t held = millis() - t0;
    if (held > 30000) {
      // Never released: more likely a stale calibration than a real hold.
      // Don't risk a factory reset; recalibrate and boot normally.
      Serial.println("[boot] touch stuck for 30 s, ignoring (stale calibration?)");
      return Gesture::None;
    }
    if (held > 300) display.showHoldGesture(held);  // a quick tap to wake shows nothing
#if TOUCH_DEBUG
    if (held % 250 < 50) Serial.printf("[boot] held %u ms, raw %u\n", (unsigned)held, (unsigned)touch.raw());
#endif
    delay(50);
  }
  uint32_t held = millis() - t0;
  if (held >= RESET_HOLD_MS) return Gesture::FactoryReset;
  if (held >= SETUP_HOLD_MS) return Gesture::Setup;
  return Gesture::None;
}

void enterSetup() {
  display.on();
  display.setRotation(0);
  portal.start();
  state = State::Setup;
  setupScreen = SetupScreen::JoinQR;
  setupDirty = true;
  lastPhone = false;
}

void enterRun() {
  state = State::Run;
  modes.begin();
}


void loopSetup() {
  portal.loop();


  bool phone = portal.phoneConnected();
  if (phone != lastPhone) {
    // When a phone joins, show the page QR in case the setup page didn't open by itself.
    if (phone) setupScreen = SetupScreen::UrlQR;
    lastPhone = phone;
    setupDirty = true;
  }
  if (portal.consumeAttemptChange()) setupDirty = true;

  switch (portal.attempt()) {
    case SetupPortal::Attempt::Testing:
      if (millis() - lastSpin > 150) {
        lastSpin = millis();
        display.showSetupTesting(portal.attemptSsid());
      }
      touch.consumeTap();
      return;

    case SetupPortal::Attempt::Connected:
      if (setupDirty) {
        display.showMessage("Connected!", "Joined " + portal.attemptSsid() +
                                              ". Closing the setup WiFi...");
        setupDirty = false;
      }
      // Keep the hotspot up briefly so the phone page can show "Connected".
      if (millis() - portal.attemptChangedAt() > SETUP_SUCCESS_LINGER_MS) {
        portal.stop();
        net.adoptConnection();
        enterRun();
      }
      return;

    case SetupPortal::Attempt::Failed:
      if (setupDirty) {
        display.showSetupFailed(portal.attemptSsid());
        setupDirty = false;
      }
      if (millis() - portal.attemptChangedAt() > 4000) portal.clearAttempt();
      return;

    case SetupPortal::Attempt::None:
      break;
  }

  if (touch.consumeTap()) {
    setupScreen = setupScreen == SetupScreen::JoinQR ? SetupScreen::UrlQR : SetupScreen::JoinQR;
    setupDirty = true;
  }

  // No phone for 5 minutes: if there are saved networks, try them again.
  if (!portal.everHadPhone() && !settings.networks().empty() &&
      millis() - portal.startedAt() > SETUP_PORTAL_TIMEOUT_MS) {
    Serial.println("[setup] timed out with no phone, retrying saved networks");
    portal.stop();
    if (connectToSavedNetworks()) {
      net.adoptConnection();
      enterRun();
    } else {
      enterSetup();
    }
    return;
  }

  if (setupDirty) {
    if (setupScreen == SetupScreen::JoinQR)
      display.showSetupJoinQR(settings.apName(), settings.apPass());
    else
      display.showSetupUrlQR(phone);
    setupDirty = false;
  }
}

void loopRun() {
  net.loop();
  webConfig.loop();
  modes.loop();
}

}  // namespace

void setup() {
  WakeReason wake = wakeReason();
  bool woke = wake != WakeReason::PowerOn;

  Serial.begin(115200);
  settings.begin();
  // The speaker's I2S clock runs from now on (amp off): it shifts the touch
  // reading, so it must be running before touch is calibrated.
  speaker.begin();
  // Start the touch sensor early: its reading settles during the serial delay.
  touch.begin(PIN_TOUCH, settings.touchBaseline());
  if (!woke) {
    delay(1500);  // let the USB serial monitor attach (skipped on wake for speed)
    printBoardInfo();
  } else {
    Serial.printf("[wake] by %s\n", wakeReasonName(wake));
  }

  display.begin();
  if (!woke) {
    scanI2C();
    display.showBoot();
  }
  orientation.begin();
  schedule.begin();  // timezone; the clock kept running through deep sleep

  // Setup gesture: touch held at power-on, or after a touch wake. A tilt wake
  // skips it, so the cube shows its mode at once (touch settles in the background).
  Gesture g = Gesture::None;
  if (wake == WakeReason::PowerOn || wake == WakeReason::Touch) {
    g = readBootGesture(wake == WakeReason::PowerOn);
    // The finger is off the strip now: recalibrate and store for the next boot.
    touch.calibrate();
    settings.setTouchBaseline(touch.baseline());
  }

  if (g == Gesture::FactoryReset) {
    Serial.println("[boot] factory reset");
    settings.factoryReset();
    settings.setTouchBaseline(touch.baseline());
    display.showFactoryReset();
    delay(2500);
  }

  Serial.printf("[boot] %u saved network(s), gesture %d\n", (unsigned)settings.networks().size(),
                (int)g);
  if (g != Gesture::None || settings.networks().empty()) {
    enterSetup();
    return;
  }

  if (woke) {
    // Bumped or tapped while lying on the off face: go straight back to sleep
    // without turning on WiFi or the screen. Unless an alarm is close (the timer
    // wake): then stay up, re-sync the time over WiFi, and ring.
    if (orientation.waitForFace(WAKE_FACE_TIMEOUT_MS) == Face::Back &&
        !schedule.alarmSoon(ALARM_PREWAKE_S)) {
      Serial.println("[wake] still on the off face");
      goToSleep();
    }
    net.beginBackground();
    enterRun();
    return;
  }

  if (connectToSavedNetworks()) {
    net.adoptConnection();
    enterRun();
  } else {
    display.showNoWifi();
    delay(2000);
    enterSetup();
  }
}

void loop() {
  touch.update();
  orientation.update();
  switch (state) {
    case State::Setup: loopSetup(); break;
    case State::Run: loopRun(); break;
  }
  delay(2);
}
