// Touch strip calibration for Key Cube. Build and flash with:
//   pio run -e touch_cal -t upload -t monitor
//
// A guided run, with instructions on the OLED and in the serial log. Five steps:
//   1. Hands off            (mic off)
//   2. Touch and hold strip (mic off)
//   3. Hold the cube, NOT the strip (checks for false triggers from your hand)
//   4. Hands off            (mic on: its clock raises the reading)
//   5. Touch and hold strip (mic on)
// At the end it prints a block of #defines to paste into config.h, then goes
// into live mode so you can try the suggested thresholds.
//
// Serial commands: c = run the calibration again, l = live log on/off.

#include <Arduino.h>
#include <algorithm>

#include "../audio.h"
#include "../config.h"
#include "../display.h"

namespace {

U8G2& g() { return display.gfx(); }

const uint32_t PREP_MS = 3000;     // time to get ready before each step
const uint32_t MEASURE_MS = 5000;  // samples taken over this long
const int MAX_SAMPLES = MEASURE_MS / TOUCH_SAMPLE_MS;

uint32_t samples[MAX_SAMPLES];

bool valid(uint32_t v) { return v > 0 && v < 0x3FFFFF; }

struct Stat {
  uint32_t mean = 0, min = 0, max = 0, p10 = 0, p50 = 0, p90 = 0;
  int n = 0;
};

Stat stats(int n) {
  Stat s;
  s.n = n;
  if (!n) return s;
  uint64_t sum = 0;
  for (int i = 0; i < n; i++) sum += samples[i];
  std::sort(samples, samples + n);
  s.mean = sum / n;
  s.min = samples[0];
  s.max = samples[n - 1];
  s.p10 = samples[n / 10];
  s.p50 = samples[n / 2];
  s.p90 = samples[n * 9 / 10];
  return s;
}

void screen(const char* step, const char* l1, const char* l2, const char* foot) {
  g().clearBuffer();
  g().setFont(u8g2_font_5x8_tf);
  g().drawStr(0, 8, step);
  g().setFont(u8g2_font_helvB10_tr);
  g().drawStr(0, 26, l1);
  if (l2) g().drawStr(0, 40, l2);
  g().setFont(u8g2_font_5x8_tf);
  if (foot) g().drawStr(0, 62, foot);
}

// One step: show the instruction, count down, then sample for MEASURE_MS.
Stat runStep(int num, const char* l1, const char* l2) {
  char head[24], foot[32];
  snprintf(head, sizeof head, "Step %d/5", num);
  Serial.printf("\n[step %d/5] %s %s\n", num, l1, l2 ? l2 : "");

  for (uint32_t t0 = millis(), el; (el = millis() - t0) < PREP_MS;) {
    snprintf(foot, sizeof foot, "Starts in %lu...", (unsigned long)((PREP_MS - el) / 1000 + 1));
    screen(head, l1, l2, foot);
    g().sendBuffer();
    delay(50);
  }

  int n = 0;
  uint32_t t0 = millis(), lastDraw = 0, lastLog = 0;
  while (millis() - t0 < MEASURE_MS && n < MAX_SAMPLES) {
    uint32_t v = touchRead(PIN_TOUCH);
    if (valid(v)) samples[n++] = v;
    uint32_t now = millis();
    if (now - lastDraw >= 100) {
      lastDraw = now;
      snprintf(foot, sizeof foot, "Measuring  raw %lu", (unsigned long)v);
      screen(head, l1, l2, foot);
      g().drawFrame(0, 46, 128, 6);
      g().drawBox(1, 47, (now - t0) * 126 / MEASURE_MS, 4);
      g().sendBuffer();
    }
    if (now - lastLog >= 500) {
      lastLog = now;
      Serial.printf("  raw %lu\n", (unsigned long)v);
    }
    delay(TOUCH_SAMPLE_MS);
  }
  Stat s = stats(n);
  Serial.printf("  -> n=%d mean %lu  min %lu  p10 %lu  p90 %lu  max %lu\n", s.n,
                (unsigned long)s.mean, (unsigned long)s.min, (unsigned long)s.p10,
                (unsigned long)s.p90, (unsigned long)s.max);
  return s;
}

// Wait until the reading changes by under 1% in 500 ms (as touch.cpp does).
void waitSettled() {
  Serial.println("[touch] waiting for the sensor to settle...");
  uint32_t t0 = millis(), refAt = millis(), ref = 0;
  while (!valid(ref = touchRead(PIN_TOUCH)) && millis() - t0 < 1000) delay(5);
  while (millis() - t0 < 5000) {
    screen("Starting", "Hands off", "settling...", nullptr);
    g().sendBuffer();
    delay(20);
    if (millis() - refAt >= 500) {
      uint32_t v = touchRead(PIN_TOUCH);
      uint32_t d = v > ref ? v - ref : ref - v;
      if (d * 100 < ref) break;
      ref = v;
      refAt = millis();
    }
  }
  Serial.printf("[touch] settled after %lu ms\n", (unsigned long)(millis() - t0));
}

// ---- results ----

float onF = TOUCH_ON_FACTOR, offF = TOUCH_OFF_FACTOR, rebaseF = TOUCH_REBASE_MAX_FACTOR;
uint32_t liveBase = 0;
bool good = false;

float ratio(uint32_t v, uint32_t base) { return base ? (float)v / base : 0; }

void calibrate() {
  waitSettled();
  if (mic.running()) mic.stop();
  Stat idle = runStep(1, "Hands off", "the cube");
  Stat touchS = runStep(2, "Touch and", "hold strip");
  Stat grip = runStep(3, "Hold cube,", "NOT strip");
  mic.start();
  delay(300);
  Stat idleMic = runStep(4, "Hands off", "(mic on)");
  Stat touchMic = runStep(5, "Touch and", "hold (mic on)");
  mic.stop();

  // Ratios against each condition's own baseline, as the firmware uses them
  // (the baseline is re-measured when the mic starts).
  float noise = std::max(ratio(idle.max, idle.mean), ratio(idleMic.max, idleMic.mean));
  float gripR = ratio(grip.p90, idle.mean);
  float notTouch = std::max(noise, gripR);  // highest reading without a finger
  // Median of the hold: ignores the moments before the finger landed (a late
  // touch can fill the bottom 10% with untouched readings).
  float fingOff = ratio(touchS.p50, idle.mean);
  float fingOn = ratio(touchMic.p50, idleMic.mean);
  float micShift = ratio(idleMic.mean, idle.mean);
  float fingMicOld = ratio(touchMic.p50, idle.mean);  // finger vs the pre-mic baseline
  // A finger step that reads like hands-off: either nobody touched, or the
  // sensor can't see a finger in that condition. Leave it out of the result.
  bool offSeen = fingOff > noise + 0.01f, onSeen = fingOn > noise + 0.01f;
  if (!offSeen)
    Serial.printf("\n!! Step 2: no finger seen (x%.3f). Touched the strip?\n", fingOff);
  if (!onSeen)
    Serial.printf("\n!! Step 5: no finger seen with the mic on (x%.3f). If you were touching,\n"
                  "   the mic's I2S clock is blinding the touch sensor (wiring/crosstalk).\n",
                  fingOn);
  float fing = offSeen && onSeen ? std::min(fingOff, fingOn) : offSeen ? fingOff : fingOn;
  Serial.printf("Finger mic off x%.3f, mic on x%.3f\n", fingOff, fingOn);

  Serial.println("\n================ RESULTS ================");
  Serial.printf("Baseline mic off %lu, mic on %lu (x%.3f)\n", (unsigned long)idle.mean,
                (unsigned long)idleMic.mean, micShift);
  Serial.printf("Idle noise up to        x%.3f\n", noise);
  Serial.printf("Hand on cube (no strip) x%.3f\n", gripR);
  Serial.printf("Finger on strip         x%.3f (weakest of mic off/on)\n", fing);
  Serial.printf("Current config: ON %.2f  OFF %.2f  REBASE %.2f\n", TOUCH_ON_FACTOR,
                TOUCH_OFF_FACTOR, TOUCH_REBASE_MAX_FACTOR);

  float gap = fing - notTouch;
  good = offSeen && onSeen && gap > 0.03f;
  if (!offSeen && !onSeen) {
    Serial.println("\n!! No finger seen at all: keeping the current values.");
    onF = TOUCH_ON_FACTOR;
    offF = TOUCH_OFF_FACTOR;
  } else {
    if (gap <= 0.03f) {
      Serial.println("\n!! Finger and no-finger readings are very close.");
      Serial.println("   Check the strip wiring, use a bigger strip or a thinner case wall,");
      Serial.println("   and keep the wire to GPIO4 short and away from the I2S mic wires.");
    }
    // ON 50% of the way from the highest no-finger reading to the finger,
    // OFF 20%: hysteresis so a steady finger doesn't flicker.
    onF = notTouch + gap * 0.50f;
    offF = notTouch + gap * 0.20f;
  }
  // Re-measure guard: above the mic's jump, below a finger on the old baseline.
  if (onSeen) {
    rebaseF = micShift + (fingMicOld - micShift) * 0.5f;
    if (rebaseF <= micShift * 1.03f) rebaseF = micShift * 1.03f;
  } else {
    rebaseF = TOUCH_REBASE_MAX_FACTOR;  // can't tell without a mic-on finger reading
  }

  Serial.println("\nPaste into config.h:");
  Serial.println("----------------------------------------");
  Serial.printf("#define TOUCH_ON_FACTOR %.2ff\n", onF);
  Serial.printf("#define TOUCH_OFF_FACTOR %.2ff\n", offF);
  Serial.printf("#define TOUCH_REBASE_MAX_FACTOR %.2ff\n", rebaseF);
  Serial.println("----------------------------------------");
  Serial.println(good ? "Margin OK." : "WARNING: weak margin, see above.");
  Serial.println("Live mode: touch the strip to try these values. c = run again.\n");

  char l1[24], l2[24];
  snprintf(l1, sizeof l1, "ON %.2f OFF %.2f", onF, offF);
  snprintf(l2, sizeof l2, "REBASE %.2f", rebaseF);
  screen(good ? "Done - margin OK" : "Done - WEAK MARGIN", l1, l2, "Values in serial log");
  g().sendBuffer();
  delay(5000);

  liveBase = idle.mean;
}

// ---- live mode: try the suggested values ----

bool touched = false;
uint8_t streak = 0;
bool liveLog = true;

void live() {
  static uint32_t lastSample = 0, lastDraw = 0, lastLog = 0;
  uint32_t now = millis();
  if (now - lastSample < TOUCH_SAMPLE_MS) return;
  lastSample = now;
  uint32_t v = touchRead(PIN_TOUCH);
  if (!valid(v) || !liveBase) return;

  bool want = touched ? v < liveBase * offF : v > liveBase * onF;
  streak = want ? streak + 1 : 0;
  if (streak >= TOUCH_FILTER_SAMPLES) {
    streak = 0;
    touched = !touched;
    Serial.printf("[live] %s\n", touched ? "TOUCH" : "release");
  }
  if (!touched && !streak) liveBase = (liveBase * 999 + v) / 1000;
  float r = ratio(v, liveBase);

  if (now - lastDraw >= 50) {
    lastDraw = now;
    // Bar from x1.0 to the larger of x2.0 and the reading.
    float top = std::max(2.0f, onF * 1.5f);
    auto px = [&](float f) { return (int)((std::min(f, top) - 1.0f) / (top - 1.0f) * 126); };
    g().clearBuffer();
    g().setFont(u8g2_font_6x10_tf);
    g().drawStr(0, 10, touched ? "TOUCHED" : "Live test");
    g().setFont(u8g2_font_5x8_tf);
    char buf[32];
    snprintf(buf, sizeof buf, "raw %lu  base %lu", (unsigned long)v, (unsigned long)liveBase);
    g().drawStr(0, 22, buf);
    snprintf(buf, sizeof buf, "x%.3f", r);
    g().drawStr(92, 10, buf);
    g().drawFrame(0, 28, 128, 12);
    if (r > 1.0f) g().drawBox(1, 29, px(r), 10);
    // threshold ticks
    g().drawVLine(1 + px(onF), 25, 18);
    g().drawVLine(1 + px(offF), 27, 14);
    snprintf(buf, sizeof buf, "on x%.2f  off x%.2f", onF, offF);
    g().drawStr(0, 52, buf);
    g().drawStr(0, 62, "c: calibrate again");
    if (touched) g().drawBox(118, 52, 10, 10);
    g().sendBuffer();
  }
  if (liveLog && now - lastLog >= 250) {
    lastLog = now;
    Serial.printf("[live] raw %lu base %lu x%.3f %s\n", (unsigned long)v, (unsigned long)liveBase,
                  r, touched ? "TOUCHED" : "");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  Serial.println("\n=== Key Cube touch calibration ===");
  Serial.printf("Touch pin GPIO%d, sample every %d ms, filter %d samples\n", PIN_TOUCH,
                TOUCH_SAMPLE_MS, TOUCH_FILTER_SAMPLES);
  Serial.println("Follow the steps on the screen. Commands: c calibrate again, l live log");

  display.begin();
  display.on();
  display.setRotation(DISPLAY_UPRIGHT_ROTATION);
  calibrate();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'c') {
      calibrate();
      touched = false;
      streak = 0;
    } else if (c == 'l') {
      liveLog = !liveLog;
    }
  }
  live();
}
