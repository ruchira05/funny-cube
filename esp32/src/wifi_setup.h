// WiFi: connect to saved networks, and the setup hotspot with a phone setup page
// (captive portal). Credentials are saved only after they connect successfully.
#pragma once

#include <Arduino.h>

// Scan, then try saved networks strongest first, for up to WIFI_TOTAL_TIMEOUT_MS.
// Shows progress on the display. Returns true when connected.
bool connectToSavedNetworks();

class SetupPortal {
 public:
  enum class Attempt { None, Testing, Connected, Failed };

  void start();  // hotspot + DNS + web server
  void stop();   // shuts the hotspot, keeps the station connection
  void loop();   // call every loop while active

  bool active() const { return active_; }
  bool phoneConnected() const;
  bool everHadPhone() const { return everHadPhone_; }
  uint32_t startedAt() const { return startedAt_; }

  Attempt attempt() const { return attempt_; }
  String attemptSsid() const { return trySsid_; }
  uint32_t attemptChangedAt() const { return attemptChangedAt_; }
  // True once each time a new network test starts or ends (for display refresh).
  bool consumeAttemptChange();
  void clearAttempt();  // back to the QR screens after a failed attempt


 private:
  void scan();
  void setupRoutes();
  void setAttempt(Attempt a);
  String pageHtml();

  bool active_ = false;
  bool everHadPhone_ = false;
  uint32_t startedAt_ = 0;

  Attempt attempt_ = Attempt::None;
  bool attemptChanged_ = false;
  uint32_t attemptChangedAt_ = 0;
  String trySsid_, tryPass_;
  uint32_t tryStart_ = 0;
};

extern SetupPortal portal;
