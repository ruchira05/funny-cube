// Stage 4: clock, alarms and health reminders.
//
// Time: SNTP after WiFi connects (net.cpp), re-synced every 6 hours, in the
// timezone from the setup page. The chip's RTC keeps counting through deep sleep;
// after a power cut the time is unknown ("--:--") until the next sync.
//
// Schedule: up to MAX_ALARMS alarms (once, or on chosen weekdays) and 3 health
// reminders (water, stretch, posture) that repeat every N minutes within active
// hours. Alarms ring in every mode and wake the cube from deep sleep; reminders
// fire in the 4 side modes only (the off face means off).
//
// Alerts: a full-screen overlay on top of any mode while an alarm rings or a
// reminder or notice shows. Modes (modes.cpp) pauses the mode underneath.
//   Alarm:    tap = snooze 5 min, hold = stop
//   Reminder: tap = done (counts for today), hold = skip
//   Notice:   tap = OK (focus timer messages shown in other modes)
#pragma once

#include <Arduino.h>
#include <time.h>

#include "settings.h"
#include "speaker.h"

// ---- Time helpers ----
void timeBegin();  // timezone from settings (also after a wake, before WiFi)
void timeOnConnected();  // SNTP start/resync, called by net.cpp
bool timeValid();  // false until the first sync after a power cut
time_t nowEpoch();
bool localTm(time_t t, struct tm& out);
uint32_t localDay(time_t t);  // changes at local midnight
String hhmm(time_t t);       // "07:30", "--:--" when the time is unknown
String untilText(time_t t);  // "in 2h 05m", "in 4m", "now"
String daysText(uint8_t days);  // "Mon-Fri", "Every day", "Once", "Sat Sun", ...
const char* reminderName(int k);    // "Water"
const char* reminderPrompt(int k);  // "Drink some water"
// Small icon for a reminder, centred at (cx, cy), about 16 x 16.
void drawReminderIcon(int k, int cx, int cy);

class Schedule {
 public:
  void begin();
  // Every loop while running. offFace: the cube is "off" (no reminders).
  // busy: a mode is in the middle of something (key buddy recording or thinking):
  // reminders wait. Starts alarm and reminder alerts.
  void loop(bool offFace, bool busy);

  // Next alarm (or snooze) after now; 0 if none or the time is unknown.
  time_t nextAlarm(int* slot = nullptr) const;
  time_t snoozeUntil() const;  // 0 if not snoozed
  void snooze(int slot);
  void cancelSnooze();
  bool alarmSoon(uint32_t withinS) const;
  // When to wake from deep sleep: ALARM_PREWAKE_S before the next alarm (0 = none).
  time_t sleepWakeAt() const;

  // Reminders.
  time_t nextReminder(int k) const;  // 0 if off or not scheduled yet
  bool inActiveHours(time_t t) const;
  uint8_t count(int k) const;
  void reminderDone(int k, bool acknowledged);

  // Call after reminder settings change: the intervals start again from now.
  void remindersChanged();

 private:
  time_t occurrenceAfter(const AlarmSlot& a, time_t t) const;
  void checkAlarms(time_t now);
  void checkReminders(time_t now, bool offFace, bool busy);

  uint32_t lastLoop_ = 0;
  bool wasValid_ = false;
};

class Alerts {
 public:
  enum class Kind { None, Alarm, Reminder, Notice };

  void startAlarm(int slot);  // slot -1: test from the settings page
  void startReminder(int k);
  void startNotice(const String& title, const String& body, Sound sound);
  void stop();  // ends any alert without acting (e.g. the off face before sleep)

  bool active() const { return kind_ != Kind::None; }
  Kind kind() const { return kind_; }
  void loop();      // touch, timeouts, drawing; call while active
  void redraw() { dirty_ = true; }  // after a rotation change
  bool consumeStarted();  // true once when an alert begins (modes pause)
  bool consumeEnded();    // true once when the last alert ends (modes resume)

 private:
  void begin(Kind k);
  void end();
  void draw(uint32_t now);

  Kind kind_ = Kind::None;
  int slot_ = 0;  // alarm slot or reminder kind
  String title_, body_;
  uint32_t startedAt_ = 0, lastDraw_ = 0, lastChime_ = 0;
  bool waitRelease_ = false, dirty_ = true, started_ = false, ended_ = false;
};

extern Schedule schedule;
extern Alerts alerts;
