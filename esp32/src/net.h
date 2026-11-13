// WiFi while the cube is running: non-blocking (re)connect to saved networks and a
// background /health check, so the mode screens never freeze waiting for the network.
#pragma once

#include <Arduino.h>

#include <vector>

#include "server.h"

class Net {
 public:
  enum class State { Off, Connecting, Connected, Offline };

  void beginBackground();  // after wake: start connecting, most recently used first
  void adoptConnection();  // already connected (cold boot or setup portal)
  void loop();
  void shutdown();  // WiFi off before deep sleep

  State state() const { return state_; }
  bool connected() const { return state_ == State::Connected && !lostSince_; }
  String ssid() const;
  String ip() const;

  void requestHealthCheck();  // runs in a background task; also retries WiFi if offline
  bool healthPending() const { return healthPending_; }
  bool healthKnown() const { return healthKnown_; }
  const ServerStatus& health() const { return health_; }

  // Short text for the bottom of the mode screens.
  String shortStatus() const;
  // Changes whenever something shown on screen changes.
  uint32_t version() const { return version_; }

 private:
  void startCycle();
  void tryCurrent();
  void onConnected();

  State state_ = State::Off;
  std::vector<String> order_;  // SSIDs, most recently used first
  size_t idx_ = 0;
  uint32_t tryStart_ = 0, offlineUntil_ = 0, lostSince_ = 0;
  bool checkOnConnect_ = true;

  bool healthPending_ = false, healthKnown_ = false;
  ServerStatus health_{false, 0, ""};
  uint32_t version_ = 0;
};

extern Net net;
