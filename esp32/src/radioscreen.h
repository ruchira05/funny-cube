// Radio screens on the 128 x 64 OLED (back face: screen up, upright rotation).
//
//   Tuning:   antenna with pulsing waves, station name, "Tuning in..."
//   Playing:  station name and song title (scrolling when too long), level
//             bars from the decoder's spectrum with falling peak dots
//   Paused:   pause sign, bars sink to the floor
//   Failed / NoWifi / NoStations: icon, reason, what to do
//   Asleep:   sleep timer ran out: big clock
// Station changes slide the old name out and the new one in. A volume change on
// the settings page shows a volume bar for 2 s.
#pragma once

#include <Arduino.h>

#include "radio.h"

struct RadioView {
  enum class Kind { Tuning, Playing, Paused, Failed, NoWifi, NoStations, Asleep };
  Kind kind = Kind::Tuning;
  String name, title, info, clock;
  int index = 0, count = 0;
  int wifiBars = 0;  // 0 = no WiFi, 1..4
  int sleepMin = 0;  // minutes left on the sleep timer, 0 = off
  bool buffering = false;  // the stream arrives slower than it plays
  uint8_t bars[RADIO_BANDS] = {}, peaks[RADIO_BANDS] = {};
};

class RadioScreen {
 public:
  void reset();
  void slide(const String& from, const String& to, uint32_t now);
  void showVolume(uint8_t v, uint32_t now);
  void draw(const RadioView& v, uint32_t now);

 private:
  struct Marquee {  // scrolling text: waits, scrolls one full turn, repeats
    String text;
    uint32_t since = 0;
  };
  void header(const RadioView& v, uint32_t now);
  void stationName(const RadioView& v, int y, uint32_t now);
  void marquee(Marquee& m, const String& text, int x, int y, int w, uint32_t now);
  void levelBars(const RadioView& v, uint32_t now);
  void antenna(int cx, int cy, uint32_t now, bool waves, bool cross);
  void volumeOverlay(uint32_t now);
  void message(const RadioView& v, const char* l1, const char* l2, const char* foot, bool wifiIcon,
               uint32_t now);

  float shown_[RADIO_BANDS] = {}, peak_[RADIO_BANDS] = {};
  Marquee nameM_, titleM_;
  String slideFrom_, slideTo_;
  uint32_t slideAt_ = 0, volumeAt_ = 0;
  bool sliding_ = false, volumeShown_ = false;
  uint8_t volume_ = 0;
};

extern RadioScreen radioScreen;
