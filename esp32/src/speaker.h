// MAX98357A I2S amplifier and 4 ohm 40 mm speaker: tones and beeps made in firmware.
// Sounds play in a background task, so the screens keep updating. The amp is
// switched on (SD high) only while a sound plays, and held off in deep sleep.
#pragma once

#include <Arduino.h>

enum class Sound : uint8_t {
  None,
  Tap,        // short tick: a touch was taken
  Start,      // focus timer started or resumed (rising)
  Pause,      // focus timer paused or reset (falling)
  Chime,      // health reminder, test sound
  FocusDone,  // focus period over: break time
  BreakDone,  // break over
  Alarm,      // alarm pattern; play with repeat = true
};

class Speaker {
 public:
  bool begin();  // amp off, I2S clock running (silence), player task
  // Replaces whatever is playing. repeat: loop until stop() (the alarm, with a
  // volume ramp). Silent at volume 0.
  void play(Sound s, bool repeat = false);
  void stop();
  bool playing() const { return playing_; }
  // Before deep sleep: stop, amp off and held off while asleep.
  void shutdown();

  // Hand the I2S pins to the radio: stop, wait for the player task, close I2S.
  // While released, play() keeps only the latest request, and reclaim() plays it
  // if it is recent (an alarm that starts the moment the radio stops).
  void release();
  void reclaim();  // reopen I2S at 16 kHz (the clock runs again)
  bool released() const { return released_; }

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool playPattern(Sound s, uint32_t seq, float gain);  // false if interrupted
  bool tone(uint16_t hz, uint16_t ms, uint32_t seq, float gain);
  void silence(uint16_t ms);
  void amp(bool on);

  bool openI2S();
  bool ok_ = false;
  volatile bool released_ = false, busy_ = false;
  volatile uint32_t reqAt_ = 0;
  TaskHandle_t task_ = nullptr;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile Sound req_ = Sound::None;
  volatile bool repeat_ = false, playing_ = false;
  volatile uint32_t seq_ = 0;
  volatile float gain_ = 0;
};

extern Speaker speaker;
