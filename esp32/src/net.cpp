#include "net.h"

#include <WiFi.h>

#include <algorithm>

#include "alarms.h"
#include "config.h"
#include "settings.h"

Net net;

namespace {
// Result hand-off from the health-check task. The task writes the result, then
// sets the flag; loop() reads it only after the flag is set.
ServerStatus taskResult{false, 0, ""};
volatile bool taskDone = false;

void healthTask(void*) {
  taskResult = checkServerHealth(settings.serverUrl());
  taskDone = true;
  vTaskDelete(nullptr);
}
}  // namespace

void Net::beginBackground() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  checkOnConnect_ = true;
  startCycle();
}

void Net::adoptConnection() {
  WiFi.setAutoReconnect(true);
  checkOnConnect_ = true;
  onConnected();
}

void Net::startCycle() {
  std::vector<SavedNetwork> nets = settings.networks();
  std::sort(nets.begin(), nets.end(),
            [](const SavedNetwork& a, const SavedNetwork& b) { return a.used > b.used; });
  order_.clear();
  for (auto& n : nets) order_.push_back(n.ssid);
  idx_ = 0;
  lostSince_ = 0;
  tryCurrent();
}

void Net::tryCurrent() {
  if (idx_ >= order_.size()) {
    Serial.printf("[net] no saved network connected, retrying in %d s\n", WIFI_RETRY_PAUSE_MS / 1000);
    WiFi.disconnect();
    state_ = State::Offline;
    offlineUntil_ = millis() + WIFI_RETRY_PAUSE_MS;
    version_++;
    return;
  }
  for (auto& n : settings.networks()) {
    if (n.ssid == order_[idx_]) {
      Serial.printf("[net] trying \"%s\"\n", n.ssid.c_str());
      WiFi.begin(n.ssid.c_str(), n.pass.c_str());
      break;
    }
  }
  tryStart_ = millis();
  if (state_ != State::Connecting) version_++;
  state_ = State::Connecting;
}

void Net::onConnected() {
  state_ = State::Connected;
  lostSince_ = 0;
  version_++;
  timeOnConnected();  // SNTP: clock for alarms, reminders and the focus face
  Serial.printf("[net] connected to \"%s\" %s, server %s, API key %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                settings.serverUrl().isEmpty() ? "(not set)" : settings.serverUrl().c_str(),
                settings.apiKey().isEmpty() ? "not set" : "set");  // never print the key
  if (checkOnConnect_) {
    checkOnConnect_ = false;
    requestHealthCheck();
  }
}

void Net::loop() {
  uint32_t now = millis();
  bool up = WiFi.status() == WL_CONNECTED;

  switch (state_) {
    case State::Off:
      break;

    case State::Connecting:
      if (up) {
        // Remember which network worked, but skip the flash write if it's already first.
        if (idx_ > 0) settings.markUsed(WiFi.SSID());
        onConnected();
      } else if (now - tryStart_ > WIFI_PER_NET_TIMEOUT_MS) {
        WiFi.disconnect();
        idx_++;
        tryCurrent();
      }
      break;

    case State::Connected:
      if (!up && !lostSince_) {
        lostSince_ = now;
        version_++;
        Serial.println("[net] connection lost, waiting for auto-reconnect");
      } else if (up && lostSince_) {
        lostSince_ = 0;
        version_++;
        Serial.println("[net] reconnected");
      } else if (!up && now - lostSince_ > WIFI_LOST_GRACE_MS) {
        Serial.println("[net] auto-reconnect failed, trying all saved networks");
        startCycle();
      }
      break;

    case State::Offline:
      if ((int32_t)(now - offlineUntil_) >= 0) startCycle();
      break;
  }

  if (taskDone) {
    taskDone = false;
    health_ = taskResult;
    healthPending_ = false;
    healthKnown_ = true;
    version_++;
  }
}

void Net::requestHealthCheck() {
  if (state_ == State::Offline) {
    offlineUntil_ = millis();  // retry WiFi now
    checkOnConnect_ = true;
    return;
  }
  if (!connected()) {
    checkOnConnect_ = true;
    return;
  }
  if (healthPending_) return;
  healthPending_ = true;
  version_++;
  // TLS needs a big stack. Runs next to loop(), so the screens stay responsive.
  if (xTaskCreate(healthTask, "health", 12288, nullptr, 1, nullptr) != pdPASS) {
    healthPending_ = false;
    Serial.println("[net] could not start the health check task");
  }
}

void Net::shutdown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state_ = State::Off;
}

String Net::ssid() const { return connected() ? WiFi.SSID() : String(""); }
String Net::ip() const { return connected() ? WiFi.localIP().toString() : String(""); }

String Net::shortStatus() const {
  switch (state_) {
    case State::Off: return "WiFi off";
    case State::Offline: return "No WiFi";
    case State::Connecting: return "Connecting...";
    case State::Connected:
      if (lostSince_) return "Reconnecting...";
      if (healthPending_ && !healthKnown_) return "Checking...";
      if (!healthKnown_) return "WiFi OK";
      return health_.ok ? "Online" : "Server down";
  }
  return "";
}
