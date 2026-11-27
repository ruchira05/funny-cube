// Stage 4 screens for Mode B (alarm) and Mode C (health reminders).
// Alarms and reminders are set on the settings page (http://keycube.local).
//
// Alarm mode:  clock and the next alarm. Tap: alarm list, then status.
//              Hold: all alarms on/off (or cancel a snooze).
// Health mode: water, stretch and posture: done today and when next.
//              Tap: status. Hold: reminders on/off.
// While an alarm rings or a reminder shows, the alert screen (alarms.h) is on top.
#pragma once

#include <Arduino.h>

class AlarmMode {
 public:
  void enter();
  void loop();

 private:
  enum class Page { Main, List, Status };
  void draw();
  Page page_ = Page::Main;
  uint32_t pageAt_ = 0, lastDraw_ = 0, netVersion_ = 0;
  bool dirty_ = true;
};

class HealthMode {
 public:
  void enter();
  void loop();

 private:
  enum class Page { Main, Status };
  void draw();
  Page page_ = Page::Main;
  uint32_t pageAt_ = 0, lastDraw_ = 0, netVersion_ = 0;
  bool dirty_ = true;
};

// The status page of these modes: WiFi, server and the settings page address.
void showSettingsStatus();

extern AlarmMode alarmMode;
extern HealthMode healthMode;
