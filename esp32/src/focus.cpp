#include "focus.h"

#include "alarms.h"
#include "config.h"
#include "display.h"
#include "modes.h"
#include "net.h"
#include "settings.h"
#include "speaker.h"
#include "touch.h"

Focus focus;
FocusMode focusMode;

namespace {
U8G2& g() { return display.gfx(); }
const uint8_t* FONT_SMALL = u8g2_font_5x8_tf;
const uint8_t* FONT_BODY = u8g2_font_6x10_tf;
const uint8_t* FONT_TINY = u8g2_font_4x6_tf;

String mmss(uint32_t ms) {
  uint32_t s = (ms + 999) / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
  return buf;
}
}  // namespace

// ------------------------------------------------------------------
// Timer
// ------------------------------------------------------------------

uint32_t Focus::leftMs() const {
  if (phase_ == Phase::Idle) return 0;
  if (paused_) return pausedLeft_;
  int32_t left = (int32_t)(endsAt_ - millis());
  return left > 0 ? left : 0;
}

void Focus::startPeriod(Phase p, uint32_t minutes) {
  phase_ = p;
  paused_ = false;
  breakOver_ = false;
  totalMs_ = minutes * 60000UL;
  endsAt_ = millis() + totalMs_;
  Serial.printf("[focus] %s %u min\n", p == Phase::Work ? "focus" : "break", (unsigned)minutes);
}

void Focus::toggle() {
  if (phase_ == Phase::Idle) {
    startPeriod(Phase::Work, settings.focusWorkMin());
    speaker.play(Sound::Start);
  } else if (paused_) {
    paused_ = false;
    endsAt_ = millis() + pausedLeft_;
    speaker.play(Sound::Start);
  } else {
    pausedLeft_ = leftMs();
    paused_ = true;
    speaker.play(Sound::Pause);
  }
}

void Focus::reset() {
  if (phase_ != Phase::Idle) Serial.println("[focus] reset");
  phase_ = Phase::Idle;
  paused_ = false;
  breakOver_ = false;
  sessions_ = 0;
  noticePending_ = false;
}

void Focus::tick() {
  if (phase_ == Phase::Idle || paused_ || leftMs() > 0) return;
  bool here = modes.current() == Mode::Focus;
  Sound sound;
  if (phase_ == Phase::Work) {
    sessions_++;
    long_ = sessions_ % FOCUS_LONG_EVERY == 0;
    uint32_t min = long_ ? settings.focusLongMin() : settings.focusBreakMin();
    startPeriod(Phase::Break, min);
    sound = Sound::FocusDone;
    noticeTitle_ = "Break time";
    noticeBody_ = String(long_ ? "Long break, " : "Focus done. ") + min + " min";
  } else {
    phase_ = Phase::Idle;
    breakOver_ = true;
    sound = Sound::BreakDone;
    noticeTitle_ = "Break over";
    noticeBody_ = "Tap on the focus face to start again";
    Serial.println("[focus] break over");
  }
  noticeSound_ = sound;
  if (here) speaker.play(sound);
  else noticePending_ = true;  // modes.cpp shows it (with the sound) when it can
}

void Focus::clearNotice() { noticePending_ = false; }

bool Focus::consumeNotice(String& title, String& body, Sound& sound) {
  if (!noticePending_) return false;
  noticePending_ = false;
  title = noticeTitle_;
  body = noticeBody_;
  sound = noticeSound_;
  return true;
}

// ------------------------------------------------------------------
// Mode D screen
// ------------------------------------------------------------------

void FocusMode::enter() {
  dirty_ = true;
  touch.consumeTap();
  focus.clearNotice();  // this screen shows what happened
}

void FocusMode::loop() {
  uint32_t now = millis();
  if (touch.consumeHold()) {
    focus.reset();
    speaker.play(Sound::Pause);
    dirty_ = true;
  }
  if (touch.consumeTap()) {
    focus.toggle();
    dirty_ = true;
  }
  // Redraw every second (countdown, clock, blinking "paused"), or at once on a change.
  if (touch.isTouched() && now - lastDraw_ >= 100) dirty_ = true;  // hold progress
  if (dirty_ || now - lastDraw_ >= 500) {
    dirty_ = false;
    lastDraw_ = now;
    draw();
  }
}

void FocusMode::draw() {
  U8G2& u = g();
  int w = u.getDisplayWidth(), h = u.getDisplayHeight();
  bool portrait = h > w;
  u.clearBuffer();

  if (focus.phase() == Focus::Phase::Idle) {
    // Large clock.
    String t = hhmm(nowEpoch());
    display.drawBigText(t.c_str(), w / 2, portrait ? 40 : 26, w - 4, portrait ? 30 : 44);
    u.setFont(FONT_SMALL);
    if (!timeValid()) {
      display.drawCentered(net.connected() ? "Getting time..." : "No time yet", portrait ? 70 : 56);
    } else if (portrait) {
      struct tm d;
      localTm(nowEpoch(), d);
      char buf[16];
      strftime(buf, sizeof(buf), "%a %d %b", &d);
      display.drawCentered(buf, 70);
    }
    const char* hint = focus.breakOver() ? "Break over" : "Focus timer";
    char mins[16];
    snprintf(mins, sizeof(mins), "Tap: %u min", settings.focusWorkMin());
    if (portrait) {
      u.drawHLine(0, 92, w);
      display.drawCentered(hint, 104);
      display.drawCentered(mins, 116);
    } else {
      u.setFont(FONT_TINY);
      u.drawStr(0, h - 1, hint);
      u.drawStr(w - u.getStrWidth(mins), h - 1, mins);
    }
  } else {
    bool work = focus.phase() == Focus::Phase::Work;
    const char* label = work ? "Focus" : focus.longBreak() ? "Long break" : "Break";
    String left = mmss(focus.leftMs());
    bool blinkOff = focus.paused() && (millis() / 500) % 2;
    u.setFont(FONT_BODY);
    if (portrait) {
      display.drawCentered(label, 12);
      if (!blinkOff) display.drawBigText(left.c_str(), w / 2, 42, w - 2, 30);
    } else {
      u.drawStr(0, 10, label);
      String t = hhmm(nowEpoch());
      u.setFont(FONT_SMALL);
      u.drawStr(w - u.getStrWidth(t.c_str()), 8, t.c_str());
      if (!blinkOff) display.drawBigText(left.c_str(), w / 2, 31, w - 4, 30);
    }
    // Progress bar: fills up as the period passes.
    int by = portrait ? 66 : 50;
    uint32_t total = focus.totalMs(), done = total - focus.leftMs();
    u.drawFrame(0, by, w, 6);
    u.drawBox(1, by + 1, total ? (w - 2) * done / total : 0, 4);
    // Session dots: focus periods finished in this set of FOCUS_LONG_EVERY.
    int inSet = focus.sessions() % FOCUS_LONG_EVERY;
    if (!work && inSet == 0 && focus.sessions() > 0) inSet = FOCUS_LONG_EVERY;  // long break
    int dx = portrait ? 10 : 7;
    int x0 = portrait ? (w - (FOCUS_LONG_EVERY - 1) * dx) / 2 : 68;
    int dy = portrait ? 82 : 5;
    for (int i = 0; i < FOCUS_LONG_EVERY; i++) {
      if (i < inSet) u.drawDisc(x0 + i * dx, dy, 2);
      else u.drawCircle(x0 + i * dx, dy, 2);
    }
    const char* hint = focus.paused() ? "Tap: resume" : "Tap: pause";
    if (portrait) {
      u.setFont(FONT_SMALL);
      if (focus.paused()) display.drawCentered("Paused", 100);
      display.drawCentered(hint, 112);
      display.drawCentered("Hold: reset", 122);
    } else {
      u.setFont(FONT_TINY);
      u.drawStr(0, h - 1, hint);
      u.drawStr(w - u.getStrWidth("Hold: reset"), h - 1, "Hold: reset");
    }
  }

  if (touch.isTouched() && touch.heldMs() > 200) {  // hold progress along the top
    uint32_t held = touch.heldMs();
    u.drawBox(0, 0, held >= TOUCH_HOLD_MS ? w : held * w / TOUCH_HOLD_MS, 2);
  }
  u.sendBuffer();
}
