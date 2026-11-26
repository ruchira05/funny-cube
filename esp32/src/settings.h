// Persistent settings in NVS flash: saved WiFi networks, server settings,
// hotspot password, touch calibration, and (Stage 4) alarms, reminders, focus
// timer and volume.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <vector>

#include "config.h"

struct SavedNetwork {
  String ssid;
  String pass;
  uint32_t used;  // higher = used more recently (sequence number, not a clock)
};

// One alarm. days: bit 0 = Sunday .. bit 6 = Saturday (as tm_wday); 0 = once
// (rings the next time that clock time comes round, then switches itself off).
struct AlarmSlot {
  uint8_t hour, minute, days, on;
};

// Health reminders (Mode C).
constexpr int REMINDER_COUNT = 3;
enum ReminderKind : uint8_t { REM_WATER = 0, REM_STRETCH = 1, REM_POSTURE = 2 };
struct ReminderSlot {
  uint8_t on;
  uint16_t everyMin;
};

class Settings {
 public:
  void begin();

  // ---- WiFi networks (up to MAX_NETWORKS, least recently used is dropped) ----
  const std::vector<SavedNetwork>& networks() const { return nets_; }
  void addNetwork(const String& ssid, const String& pass);  // insert or update, marks as used
  void markUsed(const String& ssid);
  bool forgetNetwork(size_t index);

  // ---- Server and device ----
  String serverUrl() const { return serverUrl_; }  // base URL, no trailing slash
  String apiKey() const { return apiKey_; }
  String deviceId() const { return deviceId_; }
  String timezone() const { return tz_; }
  void setServerUrl(String url);
  void setApiKey(const String& key);
  void setDeviceId(const String& id);
  void setTimezone(const String& tz);

  // ---- Device identity ----
  String apName() const { return apName_; }  // KeyCube-XXXX (from MAC)
  String apPass() const { return apPass_; }  // generated once, 8 chars

  // ---- Touch calibration ----
  uint32_t touchBaseline() const { return touchBaseline_; }
  void setTouchBaseline(uint32_t v);

  // ---- Alarms (Stage 4) ----
  const AlarmSlot& alarm(int i) const { return alarms_[i]; }
  void setAlarm(int i, const AlarmSlot& a);
  bool alarmsOn() const { return alarmsOn_; }  // master switch (hold in Alarm mode)
  void setAlarmsOn(bool on);

  // ---- Health reminders (Stage 4) ----
  const ReminderSlot& reminder(int k) const { return rem_[k]; }
  void setReminder(int k, const ReminderSlot& r);
  bool remindersOn() const { return remOn_; }  // master switch (hold in Health mode)
  void setRemindersOn(bool on);
  uint8_t remindFrom() const { return remFrom_; }  // active hours, local time
  uint8_t remindTo() const { return remTo_; }
  void setRemindHours(uint8_t from, uint8_t to);
  // Done-today counts. day = any number that changes at local midnight.
  uint8_t healthCount(int k, uint32_t day) const { return day == healthDay_ ? healthCount_[k] : 0; }
  void addHealthCount(int k, uint32_t day);

  // ---- Focus timer and sound (Stage 4) ----
  uint8_t focusWorkMin() const { return focusWork_; }
  uint8_t focusBreakMin() const { return focusBreak_; }
  uint8_t focusLongMin() const { return focusLong_; }
  void setFocus(uint8_t work, uint8_t brk, uint8_t longBrk);
  uint8_t volume() const { return volume_; }  // 0..100
  void setVolume(uint8_t v);

  // Wipe everything (networks, server settings, hotspot password, touch baseline).
  void factoryReset();

 private:
  void load();
  void loadNetworks();
  void saveNetworks();

  Preferences prefs_;
  std::vector<SavedNetwork> nets_;
  uint32_t seq_ = 0;
  String serverUrl_, apiKey_, deviceId_, tz_, apName_, apPass_;
  uint32_t touchBaseline_ = 0;

  AlarmSlot alarms_[MAX_ALARMS] = {};
  bool alarmsOn_ = true, remOn_ = true;
  ReminderSlot rem_[REMINDER_COUNT] = {};
  uint8_t remFrom_ = DEFAULT_REMIND_FROM, remTo_ = DEFAULT_REMIND_TO;
  uint32_t healthDay_ = 0;
  uint8_t healthCount_[REMINDER_COUNT] = {};
  uint8_t focusWork_ = DEFAULT_FOCUS_WORK_MIN, focusBreak_ = DEFAULT_FOCUS_BREAK_MIN,
          focusLong_ = DEFAULT_FOCUS_LONG_MIN;
  uint8_t volume_ = SPK_DEFAULT_VOLUME;
};

extern Settings settings;
