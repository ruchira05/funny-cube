#include "keybuddy.h"

#include "mic.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "server.h"
#include "settings.h"
#include "touch.h"

KeyBuddy keyBuddy;

namespace {
// ---- Background request (the upload can take several seconds) ----
ChatReply chatResult;
volatile bool chatBusy = false, chatDone = false;
bool chatAbandoned = false;  // mode left while the request was running

void chatTask(void* arg) {
  size_t samples = (size_t)(uintptr_t)arg;
  chatResult = postAudio(mic.data(), samples);
  chatDone = true;
  chatBusy = false;
  vTaskDelete(nullptr);
}

constexpr uint32_t SHOW_RECORDING_AFTER_MS = 150;  // a quick tap shouldn't flash "Listening"
constexpr uint32_t ERROR_SHOW_MS = 6000;
constexpr size_t MIN_UPLOAD_SAMPLES = MIC_SAMPLE_RATE / 5;  // 0.2 s
const uint8_t* FONT_TEXT = u8g2_font_5x8_tf;
const uint8_t* FONT_LABEL = u8g2_font_6x10_tf;
const uint8_t* FONT_TINY = u8g2_font_4x6_tf;

// Screen areas for the current rotation: face on top (portrait) or left (landscape).
struct Layout {
  int w, h;
  int fx, fy, fr;          // face centre and radius
  int tx, ty, tw;          // text area: left, top, width
  int lineH, lines;
};

Layout layout() {
  U8G2& g = display.gfx();
  Layout l;
  l.w = g.getDisplayWidth();
  l.h = g.getDisplayHeight();
  l.lineH = 9;
  if (l.h > l.w) {  // portrait 64 x 128
    l.fx = l.w / 2, l.fy = 28, l.fr = 24;
    l.tx = 0, l.ty = 60, l.tw = l.w;
  } else {  // landscape 128 x 64
    l.fx = 22, l.fy = 32, l.fr = 20;
    l.tx = 46, l.ty = 0, l.tw = l.w - 46;
  }
  l.lines = (l.h - l.ty) / l.lineH;
  return l;
}

// The OLED fonts cover ASCII and Latin-1 only: map common Unicode punctuation,
// drop everything else (emoji etc.).
String toAscii(const String& in) {
  String out;
  out.reserve(in.length());
  const char* s = in.c_str();
  while (*s) {
    uint8_t c = *s;
    if (c < 0x80) {
      out += (c == '\n' || c == '\t') ? ' ' : (char)c;
      s++;
      continue;
    }
    // Decode one UTF-8 sequence.
    uint32_t cp = 0;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    cp = c & (0x3F >> extra);
    s++;
    for (int i = 0; i < extra && (*s & 0xC0) == 0x80; i++, s++) cp = (cp << 6) | (*s & 0x3F);
    switch (cp) {
      case 0x2018: case 0x2019: out += '\''; break;
      case 0x201C: case 0x201D: out += '"'; break;
      case 0x2013: case 0x2014: out += '-'; break;
      case 0x2026: out += "..."; break;
      case 0x00A0: out += ' '; break;
      default: break;  // emoji and other symbols: dropped
    }
  }
  return out;
}

// Word-wrap into lines that fit width px in the current font. Long words are split.
std::vector<String> wrap(U8G2& g, const String& text, int width) {
  std::vector<String> lines;
  String line;
  int i = 0, n = text.length();
  while (i < n) {
    while (i < n && text[i] == ' ') i++;
    int j = i;
    while (j < n && text[j] != ' ') j++;
    String word = text.substring(i, j);
    i = j;
    if (word.isEmpty()) break;
    String candidate = line.isEmpty() ? word : line + " " + word;
    if (g.getStrWidth(candidate.c_str()) <= width) {
      line = candidate;
      continue;
    }
    if (!line.isEmpty()) lines.push_back(line);
    line = "";
    while (g.getStrWidth(word.c_str()) > width) {  // split a word longer than a line
      int k = word.length() - 1;
      while (k > 1 && g.getStrWidth(word.substring(0, k).c_str()) > width) k--;
      lines.push_back(word.substring(0, k));
      word = word.substring(k);
    }
    line = word;
  }
  if (!line.isEmpty()) lines.push_back(line);
  if (lines.empty()) lines.push_back("");
  return lines;
}
}  // namespace

void KeyBuddy::enter() {
  active_ = true;
  touchedLast_ = touch.isTouched();
  waitRelease_ = touchedLast_;  // a finger already on the strip doesn't start a recording
  ignorePress_ = false;
  recording_ = false;
  touch.consumeTap();
  netVersion_ = net.version();
  if (!mic.start()) {
    showError("Microphone error. Check the INMP441 wiring.", millis());
    return;
  }
  // The mic clock (GPIO 5, next to the touch pin) raises the untouched reading.
  touch.rebaseline();
  if (chatBusy) {
    chatAbandoned = false;  // came back in time: show the reply
    setState(St::Thinking, millis());
  } else {
    setState(St::Idle, millis());
  }
}

void KeyBuddy::exit() {
  if (recording_) mic.stopRecording();
  recording_ = false;
  mic.stop();
  touch.rebaseline();  // back to the reading without the mic clock
  if (chatBusy) chatAbandoned = true;
  active_ = false;
}

void KeyBuddy::setState(St s, uint32_t now) {
  st_ = s;
  stateAt_ = now;
  pageAt_ = now;
  frame_ = 0;
  dirty_ = true;
}

void KeyBuddy::showError(const String& msg, uint32_t now) {
  setText(msg);
  setState(St::Error, now);
}

void KeyBuddy::setText(const String& text) {
  U8G2& g = display.gfx();
  g.setFont(FONT_TEXT);
  Layout l = layout();
  lines_ = wrap(g, toAscii(text), l.tw);
  linesPerPage_ = l.lines;
  pages_ = (lines_.size() + linesPerPage_ - 1) / linesPerPage_;
  page_ = 0;
}

void KeyBuddy::loop() {
  uint32_t now = millis();

  // A finished request: show it, unless the mode was left meanwhile.
  if (chatDone) {
    chatDone = false;
    Serial.printf("[chat] free heap %u KB, free PSRAM %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(ESP.getFreePsram() / 1024));
    if (chatAbandoned) {
      chatAbandoned = false;
      setState(St::Idle, now);
    } else if (chatResult.ok) {
      emotion_ = emotionFromName(chatResult.emotion);
      setText(chatResult.reply);
      setState(St::Reply, now);
    } else {
      emotion_ = Emotion::Sad;
      showError(chatResult.error, now);
    }
  }

  // Touch: our own press/release edges (taps are handled here, not by Touch::consumeTap).
  touch.consumeTap();
  bool t = touch.isTouched();
  if (t && !touchedLast_) onPress(now);
  else if (!t && touchedLast_) onRelease(now);
  touchedLast_ = t;

  // Recording cap reached: send it now; the release that follows does nothing.
  if (st_ == St::Recording && recording_ &&
      (now - pressAt_ >= RECORD_MAX_MS || mic.full())) {
    mic.stopRecording();
    recording_ = false;
    waitRelease_ = true;
    startUpload(now);
  }

  // Timers.
  switch (st_) {
    case St::Reply:
      if (page_ < pages_ - 1 && now - pageAt_ >= REPLY_PAGE_MS) {
        page_++;
        pageAt_ = now;
        dirty_ = true;
      } else if (page_ == pages_ - 1 && now - pageAt_ >= REPLY_HOLD_MS) {
        setState(St::Idle, now);
      }
      break;
    case St::Error:
      if (now - stateAt_ >= ERROR_SHOW_MS) setState(St::Idle, now);
      break;
    case St::Status:
      if (now - stateAt_ >= STATUS_OVERLAY_MS) setState(St::Idle, now);
      break;
    default:
      break;
  }

  // Redraw: on change, for animations, and when the WiFi/server status changes.
  if (net.version() != netVersion_) {
    netVersion_ = net.version();
    if (st_ == St::Idle || st_ == St::Status) dirty_ = true;
  }
  uint32_t animMs = st_ == St::Recording ? 100 : st_ == St::Thinking ? 250 : 0;
  if (animMs && now - lastDraw_ >= animMs) dirty_ = true;
  if (st_ == St::Recording && now - pressAt_ < SHOW_RECORDING_AFTER_MS) dirty_ = false;
  if (dirty_) {
    dirty_ = false;
    lastDraw_ = now;
    frame_++;
    draw(now);
  }
}

void KeyBuddy::onPress(uint32_t now) {
  if (waitRelease_) return;
  if ((int32_t)(now - lockoutUntil_) < 0 || chatBusy) {
    ignorePress_ = true;
    return;
  }
  pressAt_ = now;
  beforePress_ = st_;
  // Record from the first moment: if the press turns out to be a tap, it is dropped.
  recording_ = mic.running() && net.connected();
  if (recording_) {
    mic.startRecording();
    st_ = St::Recording;
    stateAt_ = now;
  }
}

void KeyBuddy::onRelease(uint32_t now) {
  if (waitRelease_) {
    waitRelease_ = false;
    return;
  }
  if (ignorePress_) {
    ignorePress_ = false;
    return;
  }
  uint32_t held = now - pressAt_;
  if (recording_) {
    mic.stopRecording();
    recording_ = false;
  }

  if (held < RECORD_MIN_MS) {  // a tap
    st_ = beforePress_;
    tap(now);
    return;
  }
  if (st_ != St::Recording) {  // held long, but we couldn't record
    if (!mic.running()) showError("Microphone error. Check the INMP441 wiring.", now);
    else if (net.state() == Net::State::Connecting) showError("Still connecting to WiFi. Try again in a moment.", now);
    else showError("No WiFi. Tap for status.", now);
    return;
  }
  startUpload(now);
}

void KeyBuddy::tap(uint32_t now) {
  switch (st_) {
    case St::Reply:
      if (page_ < pages_ - 1) {
        page_++;
        pageAt_ = now;
        dirty_ = true;
      } else {
        setState(St::Idle, now);
      }
      break;
    case St::Error:
    case St::Status:
      setState(St::Idle, now);
      break;
    case St::Idle:
      net.requestHealthCheck();
      setState(St::Status, now);
      break;
    default:
      break;
  }
}

void KeyBuddy::startUpload(uint32_t now) {
  lockoutUntil_ = now + RECORD_LOCKOUT_MS;
  size_t n = mic.samples();
  if (n < MIN_UPLOAD_SAMPLES) {
    emotion_ = Emotion::Neutral;
    showError("Too short. Hold the strip while you talk.", now);
    return;
  }
  chatBusy = true;
  chatDone = false;
  chatAbandoned = false;
  // TLS needs a big stack. The recording buffer is not touched until this finishes
  // (new presses are ignored while chatBusy).
  if (xTaskCreate(chatTask, "chat", 12288, (void*)(uintptr_t)n, 1, nullptr) != pdPASS) {
    chatBusy = false;
    showError("Out of memory. Try again.", now);
    return;
  }
  setState(St::Thinking, now);
}

void KeyBuddy::draw(uint32_t now) {
  if (st_ == St::Status) {
    String server = net.healthPending() ? String("Checking server...")
                    : net.healthKnown() ? net.health().message
                                        : net.shortStatus();
    display.showStatus(net.ssid(), net.ip(), server, net.connected() && net.health().ok);
    return;
  }

  U8G2& g = display.gfx();
  Layout l = layout();
  bool portrait = l.h > l.w;
  g.clearBuffer();

  switch (st_) {
    case St::Idle: {
      drawEmotion(g, emotion_, l.fx, l.fy, l.fr);
      g.setFont(FONT_LABEL);
      display.drawWrapped("Hold to talk", l.tx, l.ty + 12, l.tw, 11, 2);
      g.setFont(FONT_TINY);
      g.drawStr(l.tx, l.h - 1, net.shortStatus().c_str());
      break;
    }

    case St::Recording: {
      drawEmotion(g, Emotion::Listening, l.fx, l.fy, l.fr, frame_);
      g.setFont(FONT_LABEL);
      g.drawStr(l.tx, l.ty + 12, "Listening");
      // Level bar and time left.
      int by = portrait ? l.ty + 22 : l.ty + 24;
      g.drawFrame(l.tx, by, l.tw, 8);
      g.drawBox(l.tx + 1, by + 1, (l.tw - 2) * mic.level() / 100, 6);
      uint32_t held = now - pressAt_;
      int left = held >= RECORD_MAX_MS ? 0 : (RECORD_MAX_MS - held + 999) / 1000;
      char buf[16];
      snprintf(buf, sizeof(buf), "%d s left", left);
      g.setFont(FONT_TEXT);
      g.drawStr(l.tx, by + 20, buf);
      display.drawWrapped("Release to send", l.tx, by + 30, l.tw, 9, 2);
      break;
    }

    case St::Thinking: {
      drawEmotion(g, Emotion::Thinking, l.fx, l.fy, l.fr, frame_);
      g.setFont(FONT_LABEL);
      g.drawStr(l.tx, l.ty + 12, "Thinking");
      static const char* dots[] = {"", ".", "..", "..."};
      g.drawStr(l.tx + g.getStrWidth("Thinking"), l.ty + 12, dots[frame_ % 4]);
      break;
    }

    case St::Reply:
    case St::Error: {
      drawEmotion(g, st_ == St::Error ? Emotion::Sad : emotion_, l.fx, l.fy, l.fr);
      g.setFont(FONT_TEXT);
      for (int i = 0; i < linesPerPage_; i++) {
        size_t idx = page_ * linesPerPage_ + i;
        if (idx >= lines_.size()) break;
        g.drawStr(l.tx, l.ty + 8 + i * l.lineH, lines_[idx].c_str());
      }
      if (pages_ > 1) {  // page number in a corner of the face area
        char buf[24];
        snprintf(buf, sizeof(buf), "%d/%d", page_ + 1, pages_);
        g.setFont(FONT_TINY);
        g.drawStr(portrait ? l.w - g.getStrWidth(buf) : 0, portrait ? 6 : l.h - 1, buf);
      }
      break;
    }

    default:
      break;
  }
  g.sendBuffer();
}
