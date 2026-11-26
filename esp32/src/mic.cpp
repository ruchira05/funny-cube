#include "mic.h"

#include <ESP_I2S.h>
#include <math.h>

#include "config.h"

Mic mic;

namespace {
I2SClass i2s;
constexpr size_t CHUNK = 256;  // samples per read: 16 ms
}  // namespace

bool Mic::start() {
  if (running_) return true;
  if (!buf_) {
    capacity_ = (size_t)MIC_SAMPLE_RATE * RECORD_MAX_MS / 1000;
    buf_ = (int16_t*)ps_malloc(capacity_ * sizeof(int16_t));  // PSRAM: keeps internal RAM for WiFi/TLS
    if (!buf_) {
      capacity_ = 0;
      Serial.println("[mic] can't allocate the PSRAM buffer");
      return false;
    }
  }
  i2s.setPins(PIN_MIC_SCK, PIN_MIC_WS, -1, PIN_MIC_SD);
  i2s.setTimeout(100);
  // INMP441: 24-bit samples left-aligned in 32-bit slots, L/R tied to GND = left slot.
  if (!i2s.begin(I2S_MODE_STD, MIC_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO,
                 I2S_STD_SLOT_LEFT)) {
    Serial.println("[mic] I2S start failed");
    return false;
  }
  startedAt_ = millis();
  recording_ = false;
  running_ = true;
  taskDone_ = false;
  if (xTaskCreatePinnedToCore(taskEntry, "mic", 6144, this, 3, nullptr, 1) != pdPASS) {
    running_ = false;
    taskDone_ = true;
    i2s.end();
    Serial.println("[mic] can't start the reader task");
    return false;
  }
  Serial.println("[mic] on");
  return true;
}

void Mic::stop() {
  if (!running_) return;
  recording_ = false;
  running_ = false;
  for (int i = 0; i < 30 && !taskDone_; i++) delay(10);  // a read takes at most 100 ms
  i2s.end();
  level_ = 0;
  Serial.println("[mic] off");
}

void Mic::startRecording() {
  samples_ = 0;
  recPeak_ = 0;
  recording_ = true;
}

void Mic::stopRecording() {
  recording_ = false;
#if AUDIO_DEBUG
  Serial.printf("[mic] recorded %.2f s, peak %u (%.0f%% of full scale)\n",
                samples_ / (float)MIC_SAMPLE_RATE, recPeak_, recPeak_ * 100.0f / 32767);
#endif
}

void Mic::taskEntry(void* arg) {
  static_cast<Mic*>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void Mic::taskLoop() {
  int32_t raw[CHUNK];
  float prevIn = 0, prevOut = 0;  // DC blocker state
#if AUDIO_DEBUG
  uint32_t lastLog = 0;
#endif
  while (running_) {
    size_t got = i2s.readBytes((char*)raw, sizeof(raw)) / sizeof(int32_t);
    if (got == 0) continue;
    bool settled = millis() - startedAt_ >= MIC_SETTLE_MS;
    uint16_t peak = 0;
    for (size_t i = 0; i < got; i++) {
      // DC blocker (high-pass at about 13 Hz): the INMP441 output has an offset.
      float x = (float)(raw[i] >> MIC_SHIFT);
      float y = x - prevIn + 0.995f * prevOut;
      prevIn = x;
      prevOut = y;
      int32_t s = (int32_t)lrintf(y);
      if (s > 32767) s = 32767;
      if (s < -32768) s = -32768;
      uint16_t a = (uint16_t)(s < 0 ? -s : s);
      if (a > peak) peak = a;
      if (recording_ && settled && samples_ < capacity_) {
        size_t n = samples_;
        buf_[n] = (int16_t)s;
        samples_ = n + 1;  // published after the sample is written
      }
    }
    if (recording_ && peak > recPeak_) recPeak_ = peak;
    // Level for the screen: 0..100 over a 50 dB range.
    float db = peak > 0 ? 20.0f * log10f(peak / 32767.0f) : -100.0f;
    int lvl = (int)((db + 50.0f) * 2.0f);
    level_ = (uint8_t)constrain(lvl, 0, 100);
#if AUDIO_DEBUG
    if (!recording_ && millis() - lastLog > 2000) {
      lastLog = millis();
      Serial.printf("[mic] idle level %u, peak %u\n", level_, peak);
    }
#endif
  }
  taskDone_ = true;
}
