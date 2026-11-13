// Persistent settings in NVS flash: saved WiFi networks, server settings,
// hotspot password and touch calibration.
#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <vector>

struct SavedNetwork {
  String ssid;
  String pass;
  uint32_t used;  // higher = used more recently (sequence number, not a clock)
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
};

extern Settings settings;
