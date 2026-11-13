// MMA8452 accelerometer: which face the cube rests on, and motion wake from deep sleep.
#pragma once

#include <Arduino.h>

// The resting positions. Rn = the U8g2 display rotation (R0..R3) that reads
// upright when the cube stands on that face, so a face also tells the screen
// how to rotate. Back = off: lying on the back (screen up) or on the screen.
enum class Face : uint8_t { Back = 0, R0, R1, R2, R3, Unknown = 255 };
constexpr int FACE_COUNT = 5;
// Directions checked: the 5 faces plus "screen down", which also counts as Back (off).
// Set in config.h (FACE_DIR_*).
constexpr int REF_COUNT = FACE_COUNT + 1;
constexpr int REF_SCREEN_DOWN = FACE_COUNT;
const char* faceName(Face f);

class Orientation {
 public:
  bool begin();  // probe the sensor, 50 Hz, +-2 g. False if not found.
  bool present() const { return addr_ != 0; }

  void update();  // call every loop; samples every ACCEL_SAMPLE_MS
  Face face() const { return face_; }  // debounced; Unknown until the first steady face
  bool consumeFaceChange();
  // Sample until a steady face is found (or timeout). Used right after boot or wake.
  Face waitForFace(uint32_t timeoutMs);

  // Before deep sleep: interrupt on INT1 when the cube tilts away from the back face.
  void armMotionWake();

 private:
  bool writeReg(uint8_t reg, uint8_t v);
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
  uint8_t readReg(uint8_t reg);
  void configureActive();
  void loadRefs();  // FACE_DIR_* from config.h, normalized and checked
  bool readG(float g[3]);  // one raw reading in g (sensor axes)
  int nearestRef(const float v[3]) const;  // index into refs_, or -1
  Face classify(const float v[3]) const;

  uint8_t addr_ = 0;
  float refs_[REF_COUNT][3] = {};  // face directions, unit vectors
  float filt_[3] = {0, 0, 0};
  bool filtInit_ = false;
  Face face_ = Face::Unknown, candidate_ = Face::Unknown;
  uint32_t candidateSince_ = 0, lastSample_ = 0, lastLog_ = 0;
  bool changed_ = false;
};

extern Orientation orientation;
