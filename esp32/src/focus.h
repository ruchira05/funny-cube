// Stage 4, Mode D: focus timer (Pomodoro) and a large clock.
//
//   IDLE (big clock) --tap--> FOCUS 25 min --> BREAK 5 min (15 min after every 4th)
//                                  ^                          |
//                                  +----------tap-------------+  (waits after a break)
// Tap: start / pause / resume. Hold: reset to the clock.
// The timer keeps running on the other side faces; when a period ends there, a
// notice pops up. The off face resets it.
#pragma once

#include <Arduino.h>

#include "speaker.h"

class Focus {
 public:
  enum class Phase : uint8_t { Idle, Work, Break };

  void tick();    // every loop, in every mode: ends periods
  void toggle();  // tap: start, pause or resume
  void reset();   // hold, or the off face

  Phase phase() const { return phase_; }
  bool paused() const { return paused_; }
  bool longBreak() const { return long_; }
  bool breakOver() const { return breakOver_; }  // idle after a break: "tap to focus"
  uint32_t leftMs() const;
  uint32_t totalMs() const { return totalMs_; }
  int sessions() const { return sessions_; }  // focus periods finished since the last reset

  // A period ended while the cube showed another mode: title and text for a notice.
  bool consumeNotice(String& title, String& body, Sound& sound);
  void clearNotice();

 private:
  void startPeriod(Phase p, uint32_t minutes);

  Phase phase_ = Phase::Idle;
  bool paused_ = false, long_ = false, breakOver_ = false;
  uint32_t endsAt_ = 0, totalMs_ = 0, pausedLeft_ = 0;
  int sessions_ = 0;
  bool noticePending_ = false;
  String noticeTitle_, noticeBody_;
  Sound noticeSound_ = Sound::None;
};

// The Mode D screen.
class FocusMode {
 public:
  void enter();
  void loop();

 private:
  void draw();
  bool dirty_ = true;
  uint32_t lastDraw_ = 0;
};

extern Focus focus;
extern FocusMode focusMode;
