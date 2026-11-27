// Stage 4: settings page on the local network, http://keycube.local (mDNS) or the
// cube's IP address (tap in Alarm or Health mode for the status page).
// Alarms, health reminders, focus timer lengths and volume, plus test buttons.
// WiFi and server settings stay on the setup hotspot page (hold the strip at power-on).
//
// No login: anyone on the same WiFi can change alarms. The page never shows the
// WiFi passwords or the API key.
#pragma once

#include <Arduino.h>

class WebConfig {
 public:
  void loop();  // in the running state: starts with WiFi, serves the page

 private:
  void begin();
  bool started_ = false, wasConnected_ = false;
};

extern WebConfig webConfig;
