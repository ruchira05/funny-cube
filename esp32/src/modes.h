// Mode state machine: the face the cube stands on selects the mode.
// Stage 2: modes A-D show a placeholder (name and icon). Stages 3-4 fill them in.
#pragma once

#include <Arduino.h>

#include "orientation.h"

enum class Mode { None, Off, KeyBuddy, Alarm, Health, Focus };

class Modes {
 public:
  void begin();    // enter the mode for the current face
  void loop();     // face changes, taps, status overlay, sleep on the off face
  Mode current() const { return mode_; }

 private:
  void apply(Face f);
  void enter(Mode m, Face f);
  void exit(Mode m);
  void draw();

  Mode mode_ = Mode::None;
  Face face_ = Face::Unknown;
  bool dirty_ = true;
  uint32_t offSince_ = 0, overlayUntil_ = 0, netVersion_ = 0;
  bool usbNoSleepLogged_ = false;
};

extern Modes modes;
