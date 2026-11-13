// INMP441 I2S microphone: 16 kHz mono, recorded as int16 PCM into a PSRAM buffer.
// A reader task runs while the mic is on, so recording starts instantly on touch
// and display updates in the main loop can't cause dropped audio.
#pragma once

#include <Arduino.h>

class Mic {
 public:
  bool start();  // allocate the buffer (first time), I2S on, reader task running
  void stop();   // reader task ends, I2S off
  bool running() const { return running_; }

  void startRecording();
  void stopRecording();
  bool recording() const { return recording_; }
  bool full() const { return samples_ >= capacity_; }

  // Valid after stopRecording(), until the next startRecording().
  const int16_t* data() const { return buf_; }
  size_t samples() const { return samples_; }
  uint16_t peak() const { return recPeak_; }  // loudest sample in the recording

  uint8_t level() const { return level_; }  // live input level 0..100

 private:
  static void taskEntry(void* arg);
  void taskLoop();

  int16_t* buf_ = nullptr;
  size_t capacity_ = 0;
  volatile size_t samples_ = 0;
  volatile bool running_ = false, recording_ = false, taskDone_ = true;
  volatile uint8_t level_ = 0;
  volatile uint16_t recPeak_ = 0;
  uint32_t startedAt_ = 0;
};

extern Mic mic;
