// Mode A: Key buddy. Hold the touch strip and talk, release to send. The cube
// shows a thinking face, then the reply with an emotion face.
//
//   IDLE --hold--> RECORDING --release / 10 s--> THINKING --> REPLY --> IDLE
//                                                    \--> ERROR --> IDLE
// A short tap (< RECORD_MIN_MS): next page of a reply, dismiss an error, or (when
// idle) show WiFi and server status.
#pragma once

#include <Arduino.h>

#include <vector>

#include "faces.h"

class KeyBuddy {
 public:
  void enter();  // mic on (after the display rotation is set)
  void exit();   // mic off; a request in flight is finished but its reply dropped
  void loop();

 private:
  enum class St { Idle, Recording, Thinking, Reply, Error, Status };

  void onPress(uint32_t now);
  void onRelease(uint32_t now);
  void tap(uint32_t now);
  void startUpload(uint32_t now);
  void setState(St s, uint32_t now);
  void showError(const String& msg, uint32_t now);
  void setText(const String& text);  // wrap into lines_ for the current layout
  void draw(uint32_t now);

  St st_ = St::Idle, beforePress_ = St::Idle;
  bool active_ = false;
  bool touchedLast_ = false, waitRelease_ = false, ignorePress_ = false, recording_ = false;
  bool dirty_ = true;
  uint32_t pressAt_ = 0, stateAt_ = 0, pageAt_ = 0, lockoutUntil_ = 0, lastDraw_ = 0;
  uint32_t netVersion_ = 0;
  uint8_t frame_ = 0;
  Emotion emotion_ = Emotion::Neutral;
  std::vector<String> lines_;
  int page_ = 0, pages_ = 1, linesPerPage_ = 1;
};

extern KeyBuddy keyBuddy;
