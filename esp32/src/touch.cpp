#include "touch.h"

#include "config.h"

Touch touch;

float Touch::onFactor() { return TOUCH_ON_FACTOR; }
float Touch::offFactor() { return TOUCH_OFF_FACTOR; }

uint32_t Touch::raw() const { return touchRead(pin_); }

void Touch::begin(uint8_t pin, uint32_t storedBaseline) {
  pin_ = pin;
  // The first readings after power-on are 0x3FFFFF (no measurement yet). Wait for
  // a real one, or the boot gesture and calibration see a "finger".
  int skipped = 0;
  uint32_t t0 = millis();
  while (!valid(touchRead(pin_)) && millis() - t0 < 500) {
    skipped++;
    delay(5);
  }
  if (skipped) Serial.printf("[touch] skipped %d start-up readings\n", skipped);
  baseline_ = storedBaseline;
  settling_ = true;
  settleStart_ = settleRefAt_ = millis();
  settleRef_ = touchRead(pin_);
}

void Touch::waitSettled(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (settling_ && millis() - t0 < timeoutMs) {
    update();
    delay(TOUCH_SAMPLE_MS);
  }
}

void Touch::calibrate() {
  uint64_t sum = 0;
  int n = 0;
  for (int i = 0; i < 40 && n < 20; i++) {
    uint32_t v = touchRead(pin_);
    if (valid(v)) {
      sum += v;
      n++;
    }
    delay(10);
  }
  if (n) baseline_ = sum / n;
  touched_ = false;
  streak_ = 0;
  Serial.printf("[touch] baseline %u (on > %u, off < %u)\n", (unsigned)baseline_,
                (unsigned)(baseline_ * onFactor()), (unsigned)(baseline_ * offFactor()));
}

void Touch::update() {
  uint32_t now = millis();
  if (now - lastSample_ < TOUCH_SAMPLE_MS) return;
  lastSample_ = now;

  uint32_t v = raw();
  if (!valid(v)) return;

  // Start-up: wait until the reading changes by under 1% in 500 ms (5 s at most).
  if (settling_) {
#if TOUCH_DEBUG  // timeline of the first seconds, to tell a finger from a start-up quirk
    static uint32_t lastTl = 0;
    if (now - lastTl >= 250) {
      lastTl = now;
      Serial.printf("[touch] t=%u ms raw %u (settling)\n", (unsigned)(now - settleStart_), (unsigned)v);
    }
#endif
    if (now - settleRefAt_ >= 500) {
      uint32_t diff = v > settleRef_ ? v - settleRef_ : settleRef_ - v;
      if (diff * 100 < settleRef_ || now - settleStart_ > 5000) {
        settling_ = false;
        if (!baseline_) calibrate();  // first boot: nothing stored yet
        Serial.printf("[touch] settled after %u ms at %u\n", (unsigned)(now - settleStart_),
                      (unsigned)v);
      } else {
        settleRef_ = v;
        settleRefAt_ = now;
      }
    }
    return;
  }

  if (rebasing_) {
    if (v < baseline_ * TOUCH_REBASE_MAX_FACTOR) {
      rebaseSum_ += v;
      if (++rebaseCount_ >= 20) {
        uint32_t old = baseline_;
        baseline_ = rebaseSum_ / rebaseCount_;
        rebasing_ = false;
        Serial.printf("[touch] re-measured baseline %u -> %u\n", (unsigned)old,
                      (unsigned)baseline_);
      }
    } else {  // finger on the strip: start over once it is lifted
      rebaseSum_ = 0;
      rebaseCount_ = 0;
    }
    return;
  }

  // Filter: the state flips only after N consecutive samples past the threshold.
  bool wantChange = touched_ ? (v < baseline_ * offFactor()) : (v > baseline_ * onFactor());
  streak_ = wantChange ? streak_ + 1 : 0;
  if (streak_ >= TOUCH_FILTER_SAMPLES) {
    streak_ = 0;
    touched_ = !touched_;
    if (touched_) {
      pressStart_ = now;
    } else if (now - pressStart_ < TOUCH_TAP_MAX_MS) {
      tapPending_ = true;
    }
  }

  // Slow drift tracking (temperature, humidity), only while untouched.
  if (!touched_ && streak_ == 0) baseline_ = (baseline_ * 999 + v) / 1000;

#if TOUCH_DEBUG
  if (now - lastLog_ >= 500) {
    lastLog_ = now;
    Serial.printf("[touch] raw %u base %u %s\n", (unsigned)v, (unsigned)baseline_,
                  touched_ ? "TOUCHED" : "");
  }
#endif
}

void Touch::rebaseline() {
  rebasing_ = true;
  rebaseSum_ = 0;
  rebaseCount_ = 0;
  touched_ = false;
  streak_ = 0;
  tapPending_ = false;
}

bool Touch::consumeTap() {
  bool t = tapPending_;
  tapPending_ = false;
  return t;
}
