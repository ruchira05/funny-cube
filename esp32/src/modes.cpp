#include "modes.h"

#include "config.h"
#include "display.h"
#include "keybuddy.h"
#include "net.h"
#include "power.h"
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
    offSince_ = millis();
    usbNoSleepLogged_ = false;
    return;
  }
  display.on();
  display.setRotation((uint8_t)f - (uint8_t)Face::R0);  // R0..R3 -> 0..3
  dirty_ = true;
  if (m == Mode::KeyBuddy) keyBuddy.enter();  // Stage 3
}

void Modes::exit(Mode m) {
  // Modes stop recording, timers and sounds here.
  if (m == Mode::KeyBuddy) keyBuddy.exit();
}

void Modes::loop() {
  if (orientation.present() && orientation.consumeFaceChange()) apply(orientation.face());

  if (mode_ == Mode::Off) {
    touch.consumeTap();
    if (millis() - offSince_ < OFF_SLEEP_DELAY_MS) return;
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

  if (mode_ == Mode::KeyBuddy) {  // Stage 3: the mode draws and handles touch itself
    keyBuddy.loop();
    return;
  }

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
  bool noSensor = !orientation.present();
  if (overlayUntil_ || noSensor) {
    String server = net.healthPending()  ? String("Checking server...")
                    : net.healthKnown() ? net.health().message
                                        : net.shortStatus();
    if (noSensor) server += " | No tilt sensor";
    display.showStatus(net.ssid(), net.ip(), server, net.connected() && net.health().ok);
    return;
  }
  const ModeInfo& info = MODE_FOR_FACE[(int)face_];
  display.showMode(info.name, info.label, info.icon, net.shortStatus());
}
