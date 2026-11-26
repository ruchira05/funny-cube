// INMP441 microphone test for Key Cube. Build and flash with:
//   pio run -e mic_test -t upload -t monitor
//
// The OLED shows a live level meter. Serial commands (115200):
//   r  record 3 s          R  record 10 s (the maximum)
//   s  stats of the last recording
//   d  send the last recording as a WAV file (use tools/mic_capture.py)
//   l  live level on serial on/off
//
// What to look for:
//   - Quiet room: level about 0-20. Talking at 30 cm: 50-80. Clap: near 100.
//   - Peak 0 / "NO SIGNAL": SD, WS or SCK not connected, or the mic has no 3.3 V.
//   - Level stuck high in silence: noise from a loose GND or a long wire.
//   - Voice too quiet: lower MIC_SHIFT in config.h (14 -> 13 doubles the gain).
//   - Many clipped samples: raise MIC_SHIFT.

#include <Arduino.h>
#include <math.h>

#include "../audio.h"
#include "../config.h"
#include "../display.h"

namespace {

U8G2& g() { return display.gfx(); }

enum class State { Live, Recording };
State state = State::Live;
uint32_t recStart = 0, recLen = 0;
bool liveSerial = false;
uint8_t peakHold = 0;
uint32_t peakHoldAt = 0;

struct Stats {
  bool valid = false;
  float seconds = 0, rmsDb = -100, peakDb = -100, dc = 0;
  uint16_t peak = 0;
  uint32_t clipped = 0, zeros = 0;
} last;

float toDb(float v) { return v > 0 ? 20.0f * log10f(v / 32767.0f) : -100.0f; }

void computeStats() {
  const int16_t* d = mic.data();
  size_t n = mic.samples();
  last = Stats();
  if (n == 0) return;
  double sum = 0, sumSq = 0;
  uint16_t peak = 0;
  for (size_t i = 0; i < n; i++) {
    int32_t s = d[i];
    sum += s;
    sumSq += (double)s * s;
    uint16_t a = s < 0 ? -s : s;
    if (a > peak) peak = a;
    if (a >= 32767) last.clipped++;
    if (s == 0) last.zeros++;
  }
  last.valid = true;
  last.seconds = n / (float)MIC_SAMPLE_RATE;
  last.dc = sum / n;
  last.rmsDb = toDb(sqrt(sumSq / n));
  last.peak = peak;
  last.peakDb = toDb(peak);
}

void printStats() {
  if (!last.valid) {
    Serial.println("[stats] no recording yet: press r");
    return;
  }
  size_t n = mic.samples();
  Serial.printf("[stats] %.2f s, %u samples\n", last.seconds, (unsigned)n);
  Serial.printf("[stats] peak %u (%.1f dBFS), RMS %.1f dBFS, DC %.1f\n", last.peak, last.peakDb,
                last.rmsDb, last.dc);
  Serial.printf("[stats] clipped %lu (%.2f%%), zero samples %.1f%%\n", (unsigned long)last.clipped,
                last.clipped * 100.0f / n, last.zeros * 100.0f / n);
  if (last.peak == 0)
    Serial.println("[stats] NO SIGNAL: check SD/WS/SCK wiring, 3.3 V, and L/R tied to GND");
  else if (last.clipped > n / 1000)
    Serial.println("[stats] clipping: raise MIC_SHIFT in config.h");
  else if (last.peakDb < -30)
    Serial.println("[stats] very quiet: speak closer, or lower MIC_SHIFT in config.h");
  else
    Serial.println("[stats] looks OK");
  Serial.println("DONE");  // tools/mic_capture.py waits for this
}

// ---- screens ----

void drawLive() {
  uint8_t lvl = mic.level();
  if (lvl >= peakHold || millis() - peakHoldAt > 1500) {
    peakHold = lvl;
    peakHoldAt = millis();
  }
  g().clearBuffer();
  g().setFont(u8g2_font_6x10_tf);
  g().drawStr(0, 10, "Mic test");
  g().setFont(u8g2_font_5x8_tf);
  char buf[24];
  snprintf(buf, sizeof buf, "lvl %3u", lvl);
  g().drawStr(92, 10, buf);
  // level bar with a peak-hold tick
  g().drawFrame(0, 16, 128, 14);
  g().drawBox(2, 18, lvl * 124 / 100, 10);
  int px = 2 + peakHold * 124 / 100;
  g().drawVLine(px > 125 ? 125 : px, 16, 14);
  if (state == State::Recording) {
    uint32_t el = millis() - recStart;
    snprintf(buf, sizeof buf, "REC %.1f / %.0f s", el / 1000.0f, recLen / 1000.0f);
    g().drawStr(0, 42, buf);
    g().drawFrame(0, 46, 128, 6);
    g().drawBox(1, 47, (el > recLen ? recLen : el) * 126 / recLen, 4);
  } else if (last.valid) {
    snprintf(buf, sizeof buf, "Last: pk %.0f rms %.0f dB", last.peakDb, last.rmsDb);
    g().drawStr(0, 42, buf);
    g().drawStr(0, 52, last.peak == 0 ? "NO SIGNAL - check wiring" : "d: send WAV  s: stats");
  } else {
    g().drawStr(0, 42, "Serial: r = record 3 s");
    g().drawStr(0, 52, "R = 10 s, l = live log");
  }
  g().drawStr(0, 63, "SCK5 WS6 SD7");
  g().sendBuffer();
}

// Min/max envelope of the recording, one column per 1/128 of it.
void drawWaveform() {
  const int16_t* d = mic.data();
  size_t n = mic.samples();
  g().clearBuffer();
  g().setFont(u8g2_font_5x8_tf);
  char buf[28];
  snprintf(buf, sizeof buf, "%.1fs pk %.0fdB rms %.0fdB", last.seconds, last.peakDb, last.rmsDb);
  g().drawStr(0, 7, buf);
  const int top = 10, h = 54, mid = top + h / 2;
  g().drawHLine(0, mid, 128);
  if (n >= 128) {
    for (int x = 0; x < 128; x++) {
      size_t a = n * x / 128, b = n * (x + 1) / 128;
      int16_t lo = 32767, hi = -32768;
      for (size_t i = a; i < b; i++) {
        if (d[i] < lo) lo = d[i];
        if (d[i] > hi) hi = d[i];
      }
      int y1 = mid - (int32_t)hi * (h / 2) / 32768;
      int y2 = mid - (int32_t)lo * (h / 2) / 32768;
      g().drawVLine(x, y1, y2 - y1 + 1);
    }
  }
  g().sendBuffer();
}

// ---- recording ----

void startRecording(uint32_t ms) {
  if (state == State::Recording) return;
  if (!mic.running() && !mic.start()) return;
  recLen = ms > RECORD_MAX_MS ? RECORD_MAX_MS : ms;
  Serial.printf("[rec] recording %.0f s...\n", recLen / 1000.0f);
  mic.startRecording();
  recStart = millis();
  state = State::Recording;
}

void finishRecording() {
  mic.stopRecording();
  state = State::Live;
  computeStats();
  drawWaveform();
  printStats();
  delay(2500);  // leave the waveform up for a moment
}

// ---- WAV dump over serial ----

bool writeAll(const uint8_t* p, size_t len) {
  uint32_t lastProgress = millis();
  while (len) {
    size_t w = Serial.write(p, len > 512 ? 512 : len);
    if (w) {
      p += w;
      len -= w;
      lastProgress = millis();
    } else if (millis() - lastProgress > 2000) {
      return false;  // nobody reading
    } else {
      delay(1);
    }
  }
  return true;
}

void sendWav() {
  if (!last.valid) {
    Serial.println("[wav] no recording yet: press r");
    return;
  }
  // Stop the mic so its reader task can't print into the middle of the binary data.
  mic.stop();
  uint32_t dataBytes = mic.samples() * sizeof(int16_t);
  struct __attribute__((packed)) {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t size;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t format = 1;  // PCM
    uint16_t channels = 1;
    uint32_t rate = MIC_SAMPLE_RATE;
    uint32_t byteRate = MIC_SAMPLE_RATE * 2;
    uint16_t align = 2;
    uint16_t bits = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
  } hdr;
  hdr.size = 36 + dataBytes;
  hdr.dataSize = dataBytes;

  g().clearBuffer();
  g().setFont(u8g2_font_6x10_tf);
  g().drawStr(0, 12, "Sending WAV...");
  g().sendBuffer();

  Serial.flush();
  Serial.printf("WAV %lu\n", (unsigned long)(sizeof(hdr) + dataBytes));
  bool ok = writeAll((const uint8_t*)&hdr, sizeof(hdr)) &&
            writeAll((const uint8_t*)mic.data(), dataBytes);
  Serial.flush();
  Serial.println(ok ? "\nEND" : "\n[wav] send timed out");
  mic.start();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  Serial.println("\n=== Key Cube mic test ===");
  Serial.printf("INMP441 SCK=%d WS=%d SD=%d, %d Hz, MIC_SHIFT=%d\n", PIN_MIC_SCK, PIN_MIC_WS,
                PIN_MIC_SD, MIC_SAMPLE_RATE, MIC_SHIFT);
  Serial.println("Commands: r record 3 s, R record 10 s, s stats, d send WAV, l live log");
  Serial.printf("PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());

  display.begin();
  display.on();
  display.setRotation(DISPLAY_UPRIGHT_ROTATION);
  if (!mic.start()) {
    display.showMessage("Mic failed", "I2S or PSRAM error. See the serial log.");
    Serial.println("[mic] start failed");
  }
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'r': startRecording(3000); break;
      case 'R': startRecording(RECORD_MAX_MS); break;
      case 's': printStats(); break;
      case 'd': sendWav(); break;
      case 'l':
        liveSerial = !liveSerial;
        Serial.println(liveSerial ? "[live] on" : "[live] off");
        break;
      default: break;
    }
  }

  if (state == State::Recording && (millis() - recStart >= recLen || mic.full())) finishRecording();

  static uint32_t lastDraw = 0, lastLog = 0;
  if (millis() - lastDraw >= 50) {  // 20 fps
    lastDraw = millis();
    drawLive();
  }
  if (liveSerial && state == State::Live && millis() - lastLog >= 100) {
    lastLog = millis();
    uint8_t lvl = mic.level();
    char bar[51];
    int n = lvl / 2;
    memset(bar, '#', n);
    memset(bar + n, ' ', 50 - n);
    bar[50] = 0;
    Serial.printf("[live] %3u |%s|\n", lvl, bar);
  }
}
