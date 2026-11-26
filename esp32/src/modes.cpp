#include "modes.h"

#include "alarms.h"
#include "clockmodes.h"
#include "config.h"
#include "display.h"
#include "focus.h"
#include "keybuddy.h"
#include "net.h"
#include "power.h"
#include "speaker.h"
#include "touch.h"

Modes modes;

namespace {
struct ModeInfo {
  Mode mode;
  const char* name;
  const char* label;
  Display::Icon icon;
};

// Face-to-mode table: which mode runs on which face. To swap two modes, swap
// their rows' first four fields, and keep the row order (it follows Face).
// The screen rotation comes from the face, not the mode: R1/R3 are portrait,
// R0/R2 landscape. Face directions are set in config.h (FACE_DIR_*).
const ModeInfo MODE_FOR_FACE[FACE_COUNT] = {
    {Mode::Off, "Off", "", Display::Icon::Buddy},                   // Face::Back (+ screen down)
    {Mode::Health, "Health", "Mode C", Display::Icon::Health},      // Face::R0 landscape
    {Mode::KeyBuddy, "Key buddy", "Mode A", Display::Icon::Buddy},  // Face::R1 portrait
    {Mode::Focus, "Focus", "Mode D", Display::Icon::Focus},         // Face::R2 landscape
    {Mode::Alarm, "Alarm", "Mode B", Display::Icon::Alarm},         // Face::R3 portrait
};

const char* modeName(Mode m) {
  for (const auto& i : MODE_FOR_FACE)
    if (i.mode == m) return i.name;
  return "none";
}
}  // namespace

void Modes::begin() {
  mode_ = Mode::None;
  face_ = Face::Unknown;
  overlayUntil_ = 0;
  if (!orientation.present()) {
    // No accelerometer: behave like Stage 1 (status screen, never sleep).
    display.setRotation(0);
    dirty_ = true;
    return;
  }
  Face f = orientation.waitForFace(WAKE_FACE_TIMEOUT_MS);
  orientation.consumeFaceChange();
  // Not lying still on any face (held in the hand): start in a side mode, and
  // switch as soon as it settles.
  apply(f == Face::Unknown ? Face::R0 : f);
}

void Modes::apply(Face f) {
  if (f == Face::Unknown) return;
  Mode m = MODE_FOR_FACE[(int)f].mode;
  if (m == mode_ && f == face_) return;
  exit(mode_);
  enter(m, f);
}

void Modes::enter(Mode m, Face f) {
  Serial.printf("[mode] %s -> %s (face %s)\n", modeName(mode_), modeName(m), faceName(f));
  mode_ = m;
  face_ = f;
  overlayUntil_ = 0;
  if (m == Mode::Off) {
    display.off();
    focus.reset();  // the off face stops the focus timer
    offSince_ = millis();
    usbNoSleepLogged_ = false;
    awakeForAlarmLogged_ = false;
    return;
  }
  display.on();
  display.setRotation((uint8_t)f - (uint8_t)Face::R0);  // R0..R3 -> 0..3
  dirty_ = true;
  switch (m) {
    case Mode::KeyBuddy: keyBuddy.enter(); break;   // Stage 3
    case Mode::Alarm: alarmMode.enter(); break;     // Stage 4
    case Mode::Health: healthMode.enter(); break;   // Stage 4
    case Mode::Focus: focusMode.enter(); break;     // Stage 4
    default: break;
  }
}

void Modes::exit(Mode m) {
  // Modes stop recording here. The focus timer keeps running on other faces.
  if (m == Mode::KeyBuddy) keyBuddy.exit();
}

void Modes::loop() {
  // ---- Stage 4: schedule, focus timer and alerts, in every mode ----
  bool busy = mode_ == Mode::KeyBuddy && keyBuddy.busy();
  bool offFace = mode_ == Mode::Off || (inAlert_ && face_ == Face::Back);
  schedule.loop(offFace, busy);
  focus.tick();
  if (!alerts.active() && !busy && !offFace && mode_ != Mode::Focus) {
    String title, body;
    Sound sound;
    if (focus.consumeNotice(title, body, sound)) alerts.startNotice(title, body, sound);
  }

  if (alerts.consumeStarted()) {
    // Pause the mode underneath (key buddy stops the mic: no recording of the
    // speaker). Lying on the back, the screen faces up: draw it unrotated.
    Serial.printf("[mode] alert over %s\n", modeName(mode_));
    exit(mode_);
    mode_ = Mode::None;
    inAlert_ = true;
    display.on();
    if (face_ == Face::Back || face_ == Face::Unknown) display.setRotation(0);
  }

  if (orientation.present() && orientation.consumeFaceChange()) {
    Face f = orientation.face();
    if (!inAlert_) {
      apply(f);
    } else if (f != Face::Unknown && f != face_) {
      // Picked up while ringing: keep the alert, turn the screen to face the user.
      face_ = f;
      if (f != Face::Back) display.setRotation((uint8_t)f - (uint8_t)Face::R0);
      alerts.redraw();
    }
  }

  if (inAlert_) {
    alerts.loop();
    if (alerts.consumeEnded()) {
      inAlert_ = false;
      Face f = face_;
      face_ = Face::Unknown;
      if (orientation.present()) {
        apply(f == Face::Unknown ? Face::R0 : f);  // resume the mode for the face
      } else {
        display.setRotation(0);
        dirty_ = true;
      }
    }
    return;
  }

  if (mode_ == Mode::Off) {
    touch.consumeTap();
    touch.consumeHold();
    if (millis() - offSince_ < OFF_SLEEP_DELAY_MS) return;
    if (speaker.playing()) return;
    if (schedule.alarmSoon(ALARM_PREWAKE_S)) {
      // Close to an alarm: stay awake (screen off, WiFi on to correct the time).
      if (!awakeForAlarmLogged_) {
        awakeForAlarmLogged_ = true;
        Serial.printf("[sleep] alarm at %s: staying awake until it rings\n",
                      hhmm(schedule.nextAlarm()).c_str());
      }
      return;
    }
    awakeForAlarmLogged_ = false;
#if DEV_NO_SLEEP_ON_USB  // DEV-ONLY: remove after development
    if (usbHostConnected()) {
      if (!usbNoSleepLogged_) {
        usbNoSleepLogged_ = true;
        Serial.println("[sleep] USB computer connected: staying awake with the screen off "
                       "(DEV_NO_SLEEP_ON_USB)");
      }
      return;
    }
#endif
    goToSleep();
  }

  // The modes draw and handle touch themselves.
  switch (mode_) {
    case Mode::KeyBuddy: keyBuddy.loop(); return;
    case Mode::Alarm: alarmMode.loop(); return;
    case Mode::Health: healthMode.loop(); return;
    case Mode::Focus: focusMode.loop(); return;
    default: break;
  }

  // No accelerometer: the Stage 1 status screen. Tap re-checks the server.

  uint32_t now = millis();
  if (touch.consumeTap()) {
    overlayUntil_ = now + STATUS_OVERLAY_MS;  // show status and re-check the server
    net.requestHealthCheck();
    dirty_ = true;
  }
  if (overlayUntil_ && (int32_t)(now - overlayUntil_) >= 0) {
    overlayUntil_ = 0;
    dirty_ = true;
  }
  if (net.version() != netVersion_) {
    netVersion_ = net.version();
    dirty_ = true;
  }
  if (dirty_) {
    dirty_ = false;
    draw();
  }
}

void Modes::draw() {
  String server = net.healthPending()  ? String("Checking server...")
                  : net.healthKnown() ? net.health().message
                                      : net.shortStatus();
  server += " | No tilt sensor";
  display.showStatus(net.ssid(), net.ip(), server, net.connected() && net.health().ok);
}
