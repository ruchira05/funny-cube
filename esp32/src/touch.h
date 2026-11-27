// Capacitive touch strip on an ESP32-S3 touch pin.
// Debounced with hysteresis, with tap and hold detection.
#pragma once

#include <Arduino.h>

#include "config.h"

class Touch {
 public:
  // Starts the sensor. Uses a stored baseline if given (> 0), so a finger already
  // on the strip at power-on is detected. Otherwise calibrates once settled.
  // The sensor's filtered reading starts high and falls for a second or more:
  // until it settles, touch reads "not touched" (see settled()).
  void begin(uint8_t pin, uint32_t storedBaseline);
  bool settled() const { return !settling_; }
  void waitSettled(uint32_t timeoutMs);  // blocking; calls update()
  void calibrate();  // average 20 readings; finger must be off the strip
  // Re-measure the baseline in the background (about 200 ms), waiting while a
  // finger is on the strip. Call when something nearby changes the reading, such
  // as the mic clock starting or stopping. Touch reads "not touched" meanwhile.
  void rebaseline();
  uint32_t baseline() const { return baseline_; }
  uint32_t raw() const;

  // Call every loop. Samples every TOUCH_SAMPLE_MS.
  void update();

  bool isTouched() const { return touched_; }
  uint32_t heldMs() const { return touched_ ? millis() - pressStart_ : 0; }
  bool consumeTap();  // true once after a short press is released
  // True once per press, as soon as it has been held for ms (finger still down).
  // A press that fires this is never also a tap.
  bool consumeHold(uint32_t ms = TOUCH_HOLD_MS);

  // Raw-value check without the filter (used for the power-on gesture).
  bool rawTouched() const { return raw() > baseline_ * onFactor(); }
  bool rawReleased() const { return raw() < baseline_ * offFactor(); }

 private:
  static bool valid(uint32_t v) { return v > 0 && v < 0x3FFFFF; }  // 0x3FFFFF: not measured yet
  static float onFactor();
  static float offFactor();

  uint8_t pin_ = 0;
  uint32_t baseline_ = 0;
  bool touched_ = false;
  uint8_t streak_ = 0;
  uint32_t lastSample_ = 0, lastLog_ = 0, pressStart_ = 0;
  bool tapPending_ = false, holdFired_ = false;
  bool settling_ = false;
  uint32_t settleStart_ = 0, settleRefAt_ = 0, settleRef_ = 0;
  bool rebasing_ = false;
  uint32_t rebaseCount_ = 0;
  uint64_t rebaseSum_ = 0;
};

extern Touch touch;
