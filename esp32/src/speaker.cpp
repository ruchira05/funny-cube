#include "speaker.h"

#include <ESP_I2S.h>
#include <driver/rtc_io.h>
#include <math.h>

#include "config.h"
#include "settings.h"

Speaker speaker;

namespace {
I2SClass i2s;  // the mic uses its own I2SClass: the S3 has two I2S ports

struct Note {
  uint16_t hz;  // 0 = rest
  uint16_t ms;
};

// Small speakers are loudest around 1-3 kHz, so the tones sit there.
const Note TAP[] = {{1800, 25}};
const Note START[] = {{1175, 70}, {0, 30}, {1568, 110}};
const Note PAUSE[] = {{1568, 70}, {0, 30}, {1175, 110}};
const Note CHIME[] = {{1047, 160}, {1319, 160}, {1568, 320}};                // C E G
const Note FOCUS_DONE[] = {{1568, 140}, {0, 60}, {1568, 140}, {0, 60}, {2093, 420}};
const Note BREAK_DONE[] = {{2093, 140}, {0, 60}, {1568, 140}, {0, 60}, {1047, 420}};
const Note ALARM[] = {{2000, 110}, {0, 70}, {2000, 110}, {0, 70}, {2000, 110}, {0, 70},
                      {2000, 110}, {0, 600}};

struct Pattern {
  const Note* notes;
  size_t count;
};
#define PATTERN(a) {a, sizeof(a) / sizeof(a[0])}

Pattern patternFor(Sound s) {
  switch (s) {
    case Sound::Tap: return PATTERN(TAP);
    case Sound::Start: return PATTERN(START);
    case Sound::Pause: return PATTERN(PAUSE);
    case Sound::Chime: return PATTERN(CHIME);
    case Sound::FocusDone: return PATTERN(FOCUS_DONE);
    case Sound::BreakDone: return PATTERN(BREAK_DONE);
    case Sound::Alarm: return PATTERN(ALARM);
    default: return {nullptr, 0};
  }
}

constexpr int TABLE_BITS = 8;
constexpr int TABLE_SIZE = 1 << TABLE_BITS;
int16_t sineTable[TABLE_SIZE];

constexpr size_t FRAMES = 128;  // per write: 8 ms at 16 kHz
int16_t frameBuf[FRAMES * 2];   // stereo: the same sample on left and right

constexpr uint32_t ATTACK_MS = 4, RELEASE_MS = 12;  // envelope: no clicks
}  // namespace

bool Speaker::begin() {
  // The SD pin was held low in deep sleep: release it.
  gpio_num_t sd = (gpio_num_t)PIN_SPK_SD;
  rtc_gpio_hold_dis(sd);
  rtc_gpio_deinit(sd);
  pinMode(PIN_SPK_SD, OUTPUT);
  digitalWrite(PIN_SPK_SD, LOW);  // amp off until a sound plays

  for (int i = 0; i < TABLE_SIZE; i++)
    sineTable[i] = (int16_t)lrintf(32767.0f * sinf(2.0f * (float)M_PI * i / TABLE_SIZE));

  i2s.setPins(PIN_SPK_BCLK, PIN_SPK_LRC, PIN_SPK_DIN, -1);
  i2s.setTimeout(200);
  // 16-bit stereo, the same sample in both slots: the amp plays left, right or the
  // mix depending on its SD voltage, and all three are the same sound.
  if (!i2s.begin(I2S_MODE_STD, SPK_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                 I2S_STD_SLOT_BOTH)) {
    Serial.println("[spk] I2S start failed: no sound");
    return false;
  }
  if (xTaskCreatePinnedToCore(taskEntry, "spk", 4096, this, 2, &task_, 1) != pdPASS) {
    i2s.end();
    Serial.println("[spk] can't start the player task");
    return false;
  }
  ok_ = true;
  Serial.printf("[spk] ready, volume %u\n", settings.volume());
  return true;
}

void Speaker::play(Sound s, bool repeat) {
  if (!ok_) return;
  uint8_t vol = settings.volume();
  if (repeat && vol < SPK_ALARM_MIN_VOLUME) vol = SPK_ALARM_MIN_VOLUME;  // never a silent alarm
  if (vol == 0 || s == Sound::None) {
    stop();
    return;
  }
  float v = vol / 100.0f;
  portENTER_CRITICAL(&mux_);
  gain_ = SPK_MAX_AMPLITUDE * v * v;  // squared: closer to how loud it sounds
  req_ = s;
  repeat_ = repeat;
  seq_ = seq_ + 1;
  playing_ = true;
  portEXIT_CRITICAL(&mux_);
  xTaskNotifyGive(task_);
}

void Speaker::stop() {
  if (!ok_) return;
  portENTER_CRITICAL(&mux_);
  req_ = Sound::None;
  repeat_ = false;
  seq_ = seq_ + 1;
  portEXIT_CRITICAL(&mux_);
  xTaskNotifyGive(task_);
}

void Speaker::shutdown() {
  stop();
  for (int i = 0; i < 30 && playing_; i++) delay(10);
  gpio_num_t sd = (gpio_num_t)PIN_SPK_SD;
  digitalWrite(PIN_SPK_SD, LOW);
  // In deep sleep the pin would float, and the amp board's pull-up would turn the
  // amp on. Hold it low.
  rtc_gpio_init(sd);
  rtc_gpio_set_direction(sd, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level(sd, 0);
  rtc_gpio_hold_en(sd);
}

void Speaker::amp(bool on) {
  digitalWrite(PIN_SPK_SD, on ? HIGH : LOW);
  if (on) vTaskDelay(pdMS_TO_TICKS(SPK_AMP_WAKE_MS));
}

void Speaker::taskEntry(void* arg) { static_cast<Speaker*>(arg)->taskLoop(); }

void Speaker::taskLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool ampOn = false;
    for (;;) {
      portENTER_CRITICAL(&mux_);
      uint32_t seq = seq_;
      Sound s = req_;
      bool repeat = repeat_;
      float gain = gain_;
      if (s == Sound::None) playing_ = false;
      portEXIT_CRITICAL(&mux_);
      if (s == Sound::None) break;

      if (!ampOn) {
        amp(true);
        ampOn = true;
      }
      uint32_t start = millis();
      bool done;
      do {
        float g = gain;
        if (repeat) {  // the alarm starts softer and rises
          uint32_t t = millis() - start;
          float ramp = t >= ALARM_RAMP_MS ? 1.0f : 0.4f + 0.6f * t / ALARM_RAMP_MS;
          g *= ramp;
        }
        done = playPattern(s, seq, g);
      } while (done && repeat && seq == seq_);

      portENTER_CRITICAL(&mux_);
      bool newer = seq != seq_;
      if (!newer) {  // finished, and nothing new was asked for meanwhile
        req_ = Sound::None;
        playing_ = false;
      }
      portEXIT_CRITICAL(&mux_);
      if (!newer) break;
    }
    if (ampOn) {
      silence(40);  // the DMA buffers hold silence before the amp turns off
      amp(false);
    }
  }
}

bool Speaker::playPattern(Sound s, uint32_t seq, float gain) {
  Pattern p = patternFor(s);
  for (size_t i = 0; i < p.count; i++) {
    if (seq != seq_) return false;
    if (p.notes[i].hz == 0) silence(p.notes[i].ms);
    else if (!tone(p.notes[i].hz, p.notes[i].ms, seq, gain)) return false;
  }
  return seq == seq_;
}

bool Speaker::tone(uint16_t hz, uint16_t ms, uint32_t seq, float gain) {
  uint32_t total = (uint32_t)SPK_SAMPLE_RATE * ms / 1000;
  uint32_t attack = SPK_SAMPLE_RATE * ATTACK_MS / 1000;
  uint32_t release = SPK_SAMPLE_RATE * RELEASE_MS / 1000;
  if (attack + release > total) attack = release = total / 2;
  uint32_t step = (uint32_t)(((uint64_t)hz << 32) / SPK_SAMPLE_RATE);
  uint32_t phase = 0;
  uint32_t n = 0;
  while (n < total) {
    if (seq != seq_) {  // interrupted: fade out quickly instead of cutting
      silence(10);
      return false;
    }
    size_t frames = total - n < FRAMES ? total - n : FRAMES;
    for (size_t i = 0; i < frames; i++, n++) {
      float env = 1.0f;
      if (n < attack) env = (float)n / attack;
      else if (total - n < release) env = (float)(total - n) / release;
      int16_t v = (int16_t)(sineTable[phase >> (32 - TABLE_BITS)] * gain * env);
      phase += step;
      frameBuf[2 * i] = v;
      frameBuf[2 * i + 1] = v;
    }
    i2s.write((const uint8_t*)frameBuf, frames * 2 * sizeof(int16_t));
  }
  return true;
}

void Speaker::silence(uint16_t ms) {
  memset(frameBuf, 0, sizeof(frameBuf));
  uint32_t total = (uint32_t)SPK_SAMPLE_RATE * ms / 1000;
  while (total) {
    size_t frames = total < FRAMES ? total : FRAMES;
    i2s.write((const uint8_t*)frameBuf, frames * 2 * sizeof(int16_t));
    total -= frames;
  }
}
