#include "orientation.h"

#include <Wire.h>
#include <driver/rtc_io.h>
#include <math.h>

#include "config.h"

Orientation orientation;

namespace {
// MMA8452Q registers (datasheet section 6)
constexpr uint8_t REG_OUT_X_MSB = 0x01;
constexpr uint8_t REG_WHO_AM_I = 0x0D;
constexpr uint8_t REG_XYZ_DATA_CFG = 0x0E;
constexpr uint8_t REG_FF_MT_CFG = 0x15;
constexpr uint8_t REG_FF_MT_SRC = 0x16;
constexpr uint8_t REG_FF_MT_THS = 0x17;
constexpr uint8_t REG_FF_MT_COUNT = 0x18;
constexpr uint8_t REG_CTRL1 = 0x2A;
constexpr uint8_t REG_CTRL3 = 0x2C;
constexpr uint8_t REG_CTRL4 = 0x2D;
constexpr uint8_t REG_CTRL5 = 0x2E;

constexpr uint8_t WHO_AM_I_MMA8452 = 0x2A;
constexpr uint8_t CTRL1_ACTIVE = 0x01;
constexpr uint8_t CTRL1_LNOISE = 0x04;
constexpr uint8_t CTRL1_ODR_50HZ = 0x20;    // DR = 100
constexpr uint8_t CTRL1_ODR_12_5HZ = 0x28;  // DR = 101 (motion wake while asleep)
constexpr float COUNTS_PER_G = 1024.0f;     // 12-bit, +-2 g
constexpr float MOTION_G_PER_COUNT = 0.063f;

// Same order as Face, then screen down.
const float FACE_DIRS[REF_COUNT][3] = {FACE_DIR_BACK, FACE_DIR_R0, FACE_DIR_R1, FACE_DIR_R2,
                                       FACE_DIR_R3,   FACE_DIR_SCREEN_DOWN};
const char* const REF_NAMES[REF_COUNT] = {"back (off)", "R0", "R1", "R2", "R3",
                                          "screen down (off)"};

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
float norm3(const float a[3]) { return sqrtf(dot3(a, a)); }
}  // namespace

const char* faceName(Face f) {
  switch (f) {
    case Face::Back: return "back (off)";
    case Face::R0: return "R0";
    case Face::R1: return "R1";
    case Face::R2: return "R2";
    case Face::R3: return "R3";
    default: return "unknown";
  }
}

bool Orientation::writeReg(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool Orientation::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;  // repeated start (required)
  if (Wire.requestFrom(addr_, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

uint8_t Orientation::readReg(uint8_t reg) {
  uint8_t v = 0;
  readRegs(reg, &v, 1);
  return v;
}

bool Orientation::begin() {
  // After a motion wake the INT pin is still an RTC pin: hand it back to GPIO.
  rtc_gpio_deinit((gpio_num_t)PIN_ACCEL_INT);
  pinMode(PIN_ACCEL_INT, INPUT_PULLDOWN);

  // The display driver has already started I2C on GPIO 8/9.
  addr_ = 0;
  for (uint8_t a : {(uint8_t)0x1C, (uint8_t)0x1D}) {
    addr_ = a;
    uint8_t who = readReg(REG_WHO_AM_I);
    if (who == WHO_AM_I_MMA8452) break;
    if (who != 0) Serial.printf("[accel] 0x%02X answered WHO_AM_I 0x%02X (not an MMA8452)\n", a, who);
    addr_ = 0;
  }
  if (!addr_) {
    Serial.println("[accel] MMA8452 not found at 0x1C/0x1D: check wiring. Modes disabled.");
    return false;
  }
  configureActive();
  loadRefs();
  Serial.printf("[accel] MMA8452 at 0x%02X\n", addr_);
  return true;
}

// Normal running: 50 Hz, +-2 g, low noise, interrupts off.
void Orientation::configureActive() {
  writeReg(REG_CTRL1, 0);  // standby: registers can only change in standby
  writeReg(REG_XYZ_DATA_CFG, 0x00);
  writeReg(REG_CTRL4, 0x00);
  writeReg(REG_FF_MT_CFG, 0x00);
  readReg(REG_FF_MT_SRC);  // clear any latched wake event (releases INT1)
  writeReg(REG_CTRL1, CTRL1_ODR_50HZ | CTRL1_LNOISE | CTRL1_ACTIVE);
  filtInit_ = false;
}

void Orientation::loadRefs() {
  for (int i = 0; i < REF_COUNT; i++) {
    float n = norm3(FACE_DIRS[i]);
    for (int k = 0; k < 3; k++) refs_[i][k] = n > 0 ? FACE_DIRS[i][k] / n : 0;
    if (n == 0) Serial.printf("[accel] WARNING: FACE_DIR for %s is {0,0,0}\n", REF_NAMES[i]);
  }
  // Catch copy-paste mistakes in config.h: two faces with (nearly) the same direction.
  for (int i = 0; i < REF_COUNT; i++)
    for (int j = i + 1; j < REF_COUNT; j++)
      if (dot3(refs_[i], refs_[j]) > 0.5f)
        Serial.printf("[accel] WARNING: FACE_DIR for %s and %s are less than 60 deg apart\n",
                      REF_NAMES[i], REF_NAMES[j]);
}

bool Orientation::readG(float g[3]) {
  if (!addr_) return false;
  uint8_t b[6];
  if (!readRegs(REG_OUT_X_MSB, b, 6)) return false;
  for (int i = 0; i < 3; i++) {
    int16_t raw = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]) >> 4;  // 12-bit, left-justified
    g[i] = raw / COUNTS_PER_G;
  }
  return true;
}

// Index of the nearest direction, or -1 when moving or between faces.
int Orientation::nearestRef(const float v[3]) const {
  float mag = norm3(v);
  if (mag < FACE_G_MIN || mag > FACE_G_MAX) return -1;
  int best = -1;
  float bestDot = FACE_MIN_DOT;
  for (int i = 0; i < REF_COUNT; i++) {
    float d = dot3(v, refs_[i]) / mag;
    if (d > bestDot) {
      bestDot = d;
      best = i;
    }
  }
  return best;
}

Face Orientation::classify(const float v[3]) const {
  int r = nearestRef(v);
  if (r < 0) return Face::Unknown;
  return r == REF_SCREEN_DOWN ? Face::Back : (Face)r;  // screen down = off too
}

void Orientation::update() {
  if (!addr_) return;
  uint32_t now = millis();
  if (now - lastSample_ < ACCEL_SAMPLE_MS) return;
  lastSample_ = now;

  float g[3];
  if (!readG(g)) return;
  if (!filtInit_) {
    for (int k = 0; k < 3; k++) filt_[k] = g[k];
    filtInit_ = true;
  }
  for (int k = 0; k < 3; k++) filt_[k] += ACCEL_FILTER_ALPHA * (g[k] - filt_[k]);

  // Both the raw and the filtered reading must agree, so a shake can't pass.
  Face c = classify(filt_);
  if (c != classify(g)) c = Face::Unknown;

  if (c != candidate_) {
    candidate_ = c;
    candidateSince_ = now;
  } else if (c != Face::Unknown && c != face_ && now - candidateSince_ >= FACE_STABLE_MS) {
    face_ = c;
    changed_ = true;
    Serial.printf("[accel] face: %s\n", faceName(face_));
  }

#if ACCEL_DEBUG
  if (now - lastLog_ >= 500) {
    lastLog_ = now;
    int r = nearestRef(filt_);
    Serial.printf("[accel] raw %+.2f %+.2f %+.2f  filt %+.2f %+.2f %+.2f  |g| %.2f  %s\n", g[0], g[1],
                  g[2], filt_[0], filt_[1], filt_[2], norm3(filt_), r < 0 ? "unknown" : REF_NAMES[r]);
  }
#endif
}

bool Orientation::consumeFaceChange() {
  bool c = changed_;
  changed_ = false;
  return c;
}

Face Orientation::waitForFace(uint32_t timeoutMs) {
  if (!addr_) return Face::Unknown;
  uint32_t t0 = millis();
  while (face_ == Face::Unknown && millis() - t0 < timeoutMs) {
    update();
    delay(5);
  }
  return face_;
}

// Motion detection on the axes that are near 0 g in the position the cube is
// resting in (back or screen down): tilting or picking it up pushes one of them
// past the threshold.
void Orientation::armMotionWake() {
  if (!addr_) return;
  float g[3];
  int r = readG(g) ? nearestRef(g) : -1;
  if (r != REF_SCREEN_DOWN) r = (int)Face::Back;
  const float* back = refs_[r];
  uint8_t axes = 0;
  float worst = 0;
  for (int k = 0; k < 3; k++) {
    if (fabsf(back[k]) < 0.6f) {
      axes |= 0x08 << k;  // XEFE, YEFE, ZEFE
      worst = fmaxf(worst, fabsf(back[k]));
    }
  }
  int ths = (int)ceilf((worst + MOTION_WAKE_MARGIN_G) / MOTION_G_PER_COUNT);
  ths = constrain(ths, 1, 127);

  writeReg(REG_CTRL1, 0);                          // standby
  writeReg(REG_FF_MT_CFG, 0x80 | 0x40 | axes);     // ELE latch, OAE motion, axes
  writeReg(REG_FF_MT_THS, 0x80 | ths);             // DBCNTM: reset count when calm
  writeReg(REG_FF_MT_COUNT, 2);                    // 2 samples at 12.5 Hz = 160 ms
  writeReg(REG_CTRL3, 0x02);                       // INT active high, push-pull
  writeReg(REG_CTRL4, 0x04);                       // enable free-fall/motion interrupt
  writeReg(REG_CTRL5, 0x04);                       // route it to INT1
  readReg(REG_FF_MT_SRC);                          // clear old events
  writeReg(REG_CTRL1, CTRL1_ODR_12_5HZ | CTRL1_ACTIVE);
  Serial.printf("[accel] motion wake armed (%s): axes 0x%02X, threshold %.2f g\n", REF_NAMES[r],
                axes, ths * MOTION_G_PER_COUNT);
}
