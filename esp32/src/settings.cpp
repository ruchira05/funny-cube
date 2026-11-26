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

// The stored touch baseline is only valid for the same electrical conditions.
// Version 2: measured with the speaker's I2S clock running (Stage 4), which raises
// the reading. Version 3: new touch strip (2026-11-26). An older baseline is
// dropped and measured again at the next boot.
constexpr uint8_t TOUCH_BASELINE_VERSION = 3;

const ReminderSlot DEFAULT_REMINDERS[REMINDER_COUNT] = {
    {1, 60},  // water every hour
    {1, 50},  // stretch
    {1, 40},  // posture
};

uint8_t clampU8(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : v > hi ? hi : v); }
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
  if (prefs_.getUChar("touchver", 0) != TOUCH_BASELINE_VERSION) touchBaseline_ = 0;

  // Stage 4: alarms, reminders, focus timer, volume.
  for (int i = 0; i < MAX_ALARMS; i++) alarms_[i] = {7, 0, 0x3E, 0};  // 07:00 Mon-Fri, off
  if (prefs_.getBytesLength("alarms") == sizeof(alarms_)) prefs_.getBytes("alarms", alarms_, sizeof(alarms_));
  alarmsOn_ = prefs_.getBool("alarmson", true);
  memcpy(rem_, DEFAULT_REMINDERS, sizeof(rem_));
  if (prefs_.getBytesLength("rem") == sizeof(rem_)) prefs_.getBytes("rem", rem_, sizeof(rem_));
  remOn_ = prefs_.getBool("remon", true);
  remFrom_ = prefs_.getUChar("remfrom", DEFAULT_REMIND_FROM);
  remTo_ = prefs_.getUChar("remto", DEFAULT_REMIND_TO);
  healthDay_ = prefs_.getUInt("hday", 0);
  memset(healthCount_, 0, sizeof(healthCount_));
  if (prefs_.getBytesLength("hcount") == sizeof(healthCount_))
    prefs_.getBytes("hcount", healthCount_, sizeof(healthCount_));
  focusWork_ = prefs_.getUChar("fwork", DEFAULT_FOCUS_WORK_MIN);
  focusBreak_ = prefs_.getUChar("fbreak", DEFAULT_FOCUS_BREAK_MIN);
  focusLong_ = prefs_.getUChar("flong", DEFAULT_FOCUS_LONG_MIN);
  volume_ = prefs_.getUChar("vol", SPK_DEFAULT_VOLUME);
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
  prefs_.putUChar("touchver", TOUCH_BASELINE_VERSION);
}

void Settings::setAlarm(int i, const AlarmSlot& a) {
  if (i < 0 || i >= MAX_ALARMS) return;
  AlarmSlot v = {clampU8(a.hour, 0, 23), clampU8(a.minute, 0, 59), (uint8_t)(a.days & 0x7F),
                 (uint8_t)(a.on ? 1 : 0)};
  if (memcmp(&v, &alarms_[i], sizeof(v)) == 0) return;
  alarms_[i] = v;
  prefs_.putBytes("alarms", alarms_, sizeof(alarms_));
}

void Settings::setAlarmsOn(bool on) {
  if (on == alarmsOn_) return;
  alarmsOn_ = on;
  prefs_.putBool("alarmson", on);
}

void Settings::setReminder(int k, const ReminderSlot& r) {
  if (k < 0 || k >= REMINDER_COUNT) return;
  int every = r.everyMin < 5 ? 5 : r.everyMin > 480 ? 480 : r.everyMin;
  ReminderSlot v = {(uint8_t)(r.on ? 1 : 0), (uint16_t)every};
  if (v.on == rem_[k].on && v.everyMin == rem_[k].everyMin) return;
  rem_[k] = v;
  prefs_.putBytes("rem", rem_, sizeof(rem_));
}

void Settings::setRemindersOn(bool on) {
  if (on == remOn_) return;
  remOn_ = on;
  prefs_.putBool("remon", on);
}

void Settings::setRemindHours(uint8_t from, uint8_t to) {
  remFrom_ = clampU8(from, 0, 23);
  remTo_ = clampU8(to, 0, 23);
  prefs_.putUChar("remfrom", remFrom_);
  prefs_.putUChar("remto", remTo_);
}

void Settings::addHealthCount(int k, uint32_t day) {
  if (k < 0 || k >= REMINDER_COUNT) return;
  if (day != healthDay_) {  // a new day: start counting from zero
    healthDay_ = day;
    memset(healthCount_, 0, sizeof(healthCount_));
    prefs_.putUInt("hday", day);
  }
  if (healthCount_[k] < 255) healthCount_[k]++;
  prefs_.putBytes("hcount", healthCount_, sizeof(healthCount_));
}

void Settings::setFocus(uint8_t work, uint8_t brk, uint8_t longBrk) {
  focusWork_ = clampU8(work, 1, 120);
  focusBreak_ = clampU8(brk, 1, 60);
  focusLong_ = clampU8(longBrk, 1, 60);
  prefs_.putUChar("fwork", focusWork_);
  prefs_.putUChar("fbreak", focusBreak_);
  prefs_.putUChar("flong", focusLong_);
}

void Settings::setVolume(uint8_t v) {
  if (v > 100) v = 100;
  if (v == volume_) return;
  volume_ = v;
  prefs_.putUChar("vol", v);
}

void Settings::factoryReset() {
  prefs_.clear();
  load();  // regenerates the hotspot password and defaults
}
