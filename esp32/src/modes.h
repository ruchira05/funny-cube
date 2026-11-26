// Mode state machine: the face the cube stands on selects the mode.
// A: key buddy (keybuddy.cpp), B: alarm and C: health (clockmodes.cpp),
// D: focus timer and clock (focus.cpp). Alarm, reminder and notice alerts
// (alarms.cpp) show on top of any mode, which pauses underneath.
#pragma once

#include <Arduino.h>

#include "orientation.h"

enum class Mode { None, Off, KeyBuddy, Alarm, Health, Focus };

class Modes {
 public:
  void begin();    // enter the mode for the current face
  void loop();     // face changes, schedule, alerts, the current mode, sleep on the off face
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
  bool usbNoSleepLogged_ = false, awakeForAlarmLogged_ = false;
  bool inAlert_ = false;
};

extern Modes modes;
