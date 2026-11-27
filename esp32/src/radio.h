// Internet radio on the back face (speaker down, screen up).
//
// RadioPlayer: the stream decoder (ESP32-audioI2S) in its own task, so screen
// updates can't cause gaps. The decoder exists only while a station plays: it
// owns the speaker's I2S pins then, and speaker.cpp gets them back afterwards.
//
// RadioMode: the mode on the back face.
//   Tap:  next station (wraps)
//   Hold: pause / play
// Stations, radio volume and the sleep timer are set on the settings page.
#pragma once

#include <Arduino.h>

constexpr int RADIO_BANDS = 15;  // spectrum bands from the decoder

class RadioPlayer {
 public:
  enum class State : uint8_t { Off, Connecting, Playing, Failed };

  bool begin();  // starts the (idle) player task once
  // Start a station (async). Creates the decoder and takes the I2S pins: call
  // speaker.release() first.
  void play(const String& url);
  // Stop and delete the decoder: the I2S pins are free when this returns.
  // Can wait a few seconds while a connection attempt times out.
  void stop();
  void setVolume(uint8_t v);  // 0..100

  State state() const { return state_; }
  bool hasDecoder() const { return hasDecoder_; }
  String title();  // song title from the stream (ICY StreamTitle), "" if none
  String info();   // "MP3 128 kbps"
  // Latest spectrum, 0..255 per band, and the falling peaks.
  void spectrum(uint8_t bars[RADIO_BANDS], uint8_t peaks[RADIO_BANDS]);
  // The stream is arriving slower than it plays (the decoder ran low recently).
  bool buffering() const { return millis() - slowAt_ < 4000; }
  // Changes when the I2S clock starts or stops (touch needs a new baseline).
  uint32_t clockVersion() const { return clockVersion_; }

  // Called from the decoder's message callback (player task).
  void onTitle(const char* t);
  void onSpectrum(const uint32_t* bars, const uint32_t* peaks, size_t n);
  void onSlowStream();

 private:
  enum class Cmd : uint8_t { None, Play, Stop };
  static void taskEntry(void* arg);
  void taskLoop();
  void createDecoder();
  void deleteDecoder();
  void connect();
  void setStateAndLog(State s);

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;     // url_, title_, info_
  SemaphoreHandle_t stopped_ = nullptr;  // given when a Stop command is done
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;  // spectrum and command

  volatile Cmd cmd_ = Cmd::None;
  String cmdUrl_, url_, title_, info_;
  volatile State state_ = State::Off;
  volatile bool hasDecoder_ = false;
  volatile uint8_t volume_ = 50;
  volatile bool volumeDirty_ = true;
  volatile uint32_t clockVersion_ = 0;
  uint8_t bars_[RADIO_BANDS] = {}, peaks_[RADIO_BANDS] = {};
  uint32_t spectrumAt_ = 0;
  volatile uint32_t slowAt_ = 0xF0000000;  // far in the past
  uint32_t slowLogAt_ = 0;

  // Player task only.
  uint32_t connectAt_ = 0, retryAt_ = 0, infoAt_ = 0;
  uint8_t attempts_ = 0;
};

class RadioMode {
 public:
  void enter();  // takes the I2S pins, plays the last station once WiFi is up
  void exit();   // stops the stream, gives the I2S pins back to the speaker
  void loop();
  bool active() const { return active_; }

  // Settings page.
  void playIndex(int i);    // play this station now (or next time radio starts)
  void stationsChanged();   // the list was edited: keep playing the same station

 private:
  void startCurrent();
  void stopStream();
  void next();
  void draw(uint32_t now);

  bool active_ = false, paused_ = false, asleep_ = false, started_ = false, wifiDown_ = false;
  int index_ = 0;
  String playingUrl_;
  uint8_t volume_ = 0;
  uint32_t sleepAt_ = 0, lastDraw_ = 0, clockVersion_ = 0;
};

extern RadioPlayer radioPlayer;
extern RadioMode radioMode;
