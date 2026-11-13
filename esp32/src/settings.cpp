#include "settings.h"

#include <esp_random.h>

#include "config.h"

Settings settings;

namespace {
const char* NS = "keycube";

// Last 2 bytes of the factory MAC as 4 hex digits, e.g. "1A2B".
String macSuffix() {
  uint64_t mac = ESP.getEfuseMac();  // byte 0 in the lowest bits
  char buf[5];
  snprintf(buf, sizeof(buf), "%02X%02X", (unsigned)((mac >> 32) & 0xFF),
           (unsigned)((mac >> 40) & 0xFF));
  return String(buf);
}

// 8 characters without look-alikes (no 0/o, 1/l/i).
String randomPassword() {
  const char* alphabet = "abcdefghjkmnpqrstuvwxyz23456789";
  const size_t n = strlen(alphabet);
  String p;
  for (int i = 0; i < 8; i++) p += alphabet[esp_random() % n];
  return p;
}

String key(int i, const char* field) { return "n" + String(i) + "_" + field; }
}  // namespace

void Settings::begin() {
  prefs_.begin(NS, false);
  load();
}

void Settings::load() {
  serverUrl_ = prefs_.getString("server", "");
  apiKey_ = prefs_.getString("apikey", "");
  apName_ = AP_PREFIX + macSuffix();
  deviceId_ = prefs_.getString("devid", "");
  if (deviceId_.isEmpty()) deviceId_ = "keycube-" + macSuffix();
  tz_ = prefs_.getString("tz", DEFAULT_TZ);
  apPass_ = prefs_.getString("appass", "");
  if (apPass_.isEmpty()) {
    apPass_ = randomPassword();
    prefs_.putString("appass", apPass_);
  }
  touchBaseline_ = prefs_.getUInt("touchbase", 0);
  seq_ = prefs_.getUInt("seq", 0);
  loadNetworks();
}

void Settings::loadNetworks() {
  nets_.clear();
  int count = prefs_.getUChar("n_count", 0);
  for (int i = 0; i < count && i < MAX_NETWORKS; i++) {
    SavedNetwork n;
    n.ssid = prefs_.getString(key(i, "ssid").c_str(), "");
    n.pass = prefs_.getString(key(i, "pass").c_str(), "");
    n.used = prefs_.getUInt(key(i, "used").c_str(), 0);
    if (!n.ssid.isEmpty()) nets_.push_back(n);
  }
}

void Settings::saveNetworks() {
  for (size_t i = 0; i < nets_.size(); i++) {
    prefs_.putString(key(i, "ssid").c_str(), nets_[i].ssid);
    prefs_.putString(key(i, "pass").c_str(), nets_[i].pass);
    prefs_.putUInt(key(i, "used").c_str(), nets_[i].used);
  }
  for (int i = nets_.size(); i < MAX_NETWORKS; i++) {  // clear leftover slots
    prefs_.remove(key(i, "ssid").c_str());
    prefs_.remove(key(i, "pass").c_str());
    prefs_.remove(key(i, "used").c_str());
  }
  prefs_.putUChar("n_count", nets_.size());
  prefs_.putUInt("seq", seq_);
}

void Settings::addNetwork(const String& ssid, const String& pass) {
  seq_++;
  for (auto& n : nets_) {
    if (n.ssid == ssid) {
      n.pass = pass;
      n.used = seq_;
      saveNetworks();
      return;
    }
  }
  if (nets_.size() >= MAX_NETWORKS) {  // drop the least recently used
    size_t oldest = 0;
    for (size_t i = 1; i < nets_.size(); i++)
      if (nets_[i].used < nets_[oldest].used) oldest = i;
    nets_.erase(nets_.begin() + oldest);
  }
  nets_.push_back({ssid, pass, seq_});
  saveNetworks();
}

void Settings::markUsed(const String& ssid) {
  for (auto& n : nets_) {
    if (n.ssid == ssid) {
      n.used = ++seq_;
      saveNetworks();
      return;
    }
  }
}

bool Settings::forgetNetwork(size_t index) {
  if (index >= nets_.size()) return false;
  nets_.erase(nets_.begin() + index);
  saveNetworks();
  return true;
}

void Settings::setServerUrl(String url) {
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  serverUrl_ = url;
  prefs_.putString("server", serverUrl_);
}

void Settings::setApiKey(const String& k) {
  apiKey_ = k;
  prefs_.putString("apikey", apiKey_);
}

void Settings::setDeviceId(const String& id) {
  deviceId_ = id;
  prefs_.putString("devid", deviceId_);
}

void Settings::setTimezone(const String& tz) {
  tz_ = tz;
  prefs_.putString("tz", tz_);
}

void Settings::setTouchBaseline(uint32_t v) {
  // Skip tiny changes: this runs on every boot and wake, and flash has limited writes.
  uint32_t diff = v > touchBaseline_ ? v - touchBaseline_ : touchBaseline_ - v;
  if (touchBaseline_ && diff * 50 < touchBaseline_) return;  // under 2%
  touchBaseline_ = v;
  prefs_.putUInt("touchbase", v);
}

void Settings::factoryReset() {
  prefs_.clear();
  load();  // regenerates the hotspot password and defaults
}
