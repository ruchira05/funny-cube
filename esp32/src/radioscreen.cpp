#include "radioscreen.h"

#include <math.h>

#include "display.h"

RadioScreen radioScreen;

namespace {
const uint8_t* F_SMALL = u8g2_font_5x8_tf;
const uint8_t* F_BODY = u8g2_font_6x10_tf;
const uint8_t* F_NAME = u8g2_font_helvB10_tf;  // _tf: accented letters in station names

constexpr int W = 128, H = 64;
constexpr uint32_t SLIDE_MS = 320, VOLUME_MS = 2000;
constexpr uint32_t MARQUEE_WAIT_MS = 1500, MARQUEE_PX_MS = 30;  // pause, then 33 px/s
constexpr int MARQUEE_GAP = 28;

// Level bars: 15 bands across the bottom.
constexpr int BAR_W = 7, BAR_STEP = 8, BAR_X0 = (W - RADIO_BANDS * BAR_STEP + 1) / 2;
constexpr int BAR_BOTTOM = 63, BAR_MAX_H = 22;

U8G2& g() { return display.gfx(); }

float easeOut(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }

void centered(const char* s, int y) {
  int w = g().getUTF8Width(s);
  g().drawUTF8((W - w) / 2, y, s);
}
}  // namespace

void RadioScreen::reset() {
  memset(shown_, 0, sizeof(shown_));
  memset(peak_, 0, sizeof(peak_));
  nameM_ = Marquee();
  titleM_ = Marquee();
  sliding_ = volumeShown_ = false;
}

void RadioScreen::slide(const String& from, const String& to, uint32_t now) {
  if (from == to) return;
  slideFrom_ = from;
  slideTo_ = to;
  slideAt_ = now;
  sliding_ = true;
}

void RadioScreen::showVolume(uint8_t v, uint32_t now) {
  volume_ = v;
  volumeAt_ = now;
  volumeShown_ = true;
}

// ---- pieces ----

void RadioScreen::header(const RadioView& v, uint32_t now) {
  U8G2& u = g();
  u.setFont(F_SMALL);
  // Little radio: body, speaker grille, antenna.
  u.drawRFrame(0, 2, 11, 6, 1);
  u.drawDisc(3, 4, 1);
  u.drawVLine(6, 3, 3);
  u.drawVLine(8, 3, 3);
  u.drawLine(2, 2, 7, 0);
  if (v.count > 0) {
    char idx[24];
    snprintf(idx, sizeof(idx), "%d/%d", v.index + 1, v.count);
    u.drawStr(14, 7, idx);
  }
  // WiFi signal: 4 bars, or a cross when there is no WiFi.
  int x0 = 117;
  if (v.wifiBars == 0) {
    u.drawLine(x0 + 2, 1, x0 + 8, 7);
    u.drawLine(x0 + 8, 1, x0 + 2, 7);
  } else {
    for (int i = 0; i < 4; i++) {
      int h = 2 + 2 * i;
      if (i < v.wifiBars) u.drawBox(x0 + i * 3, 8 - h, 2, h);
      else u.drawPixel(x0 + i * 3, 7);
    }
  }
  if (v.sleepMin > 0) {  // sleep timer: "z 25m"
    char z[16];
    snprintf(z, sizeof(z), "z%dm", v.sleepMin);
    u.drawStr(x0 - 3 - u.getStrWidth(z), 7, z);
  }
}

void RadioScreen::marquee(Marquee& m, const String& text, int x, int y, int w, uint32_t now) {
  U8G2& u = g();
  if (m.text != text) {
    m.text = text;
    m.since = now;
  }
  const char* s = text.c_str();
  int tw = u.getUTF8Width(s);
  if (tw <= w) {
    u.drawUTF8(x + (w - tw) / 2, y, s);
    return;
  }
  // Wait, scroll one full turn, wait again.
  uint32_t turn = (tw + MARQUEE_GAP) * MARQUEE_PX_MS;
  uint32_t ph = (now - m.since) % (MARQUEE_WAIT_MS + turn);
  int off = ph < MARQUEE_WAIT_MS ? 0 : (ph - MARQUEE_WAIT_MS) / MARQUEE_PX_MS;
  u.setClipWindow(x, 0, x + w, H);
  u.drawUTF8(x - off, y, s);  // u8g2 handles negative x (wrap-around coordinates)
  u.drawUTF8(x - off + tw + MARQUEE_GAP, y, s);
  u.setMaxClipWindow();
}

// Station name on baseline y; slides when the station changes.
void RadioScreen::stationName(const RadioView& v, int y, uint32_t now) {
  U8G2& u = g();
  u.setFont(F_NAME);
  if (sliding_) {
    float t = (float)(now - slideAt_) / SLIDE_MS;
    if (t >= 1.0f) {
      sliding_ = false;
      nameM_ = Marquee();  // the new name starts its own scroll pause
    } else {
      int d = (int)(easeOut(t) * W);
      int wf = u.getUTF8Width(slideFrom_.c_str()), wt = u.getUTF8Width(slideTo_.c_str());
      int xf = wf < W ? (W - wf) / 2 : 0, xt = wt < W ? (W - wt) / 2 : 0;
      u.drawUTF8(xf - d, y, slideFrom_.c_str());
      u.drawUTF8(xt + W - d, y, slideTo_.c_str());
      return;
    }
  }
  marquee(nameM_, v.name, 0, y, W, now);
}

void RadioScreen::levelBars(const RadioView& v, uint32_t now) {
  U8G2& u = g();
  bool live = v.kind == RadioView::Kind::Playing;
  for (int i = 0; i < RADIO_BANDS; i++) {
    if (live) {
      float in = v.bars[i];
      shown_[i] = in >= shown_[i] ? in : shown_[i] * 0.6f + in * 0.4f;  // quick up, soft down
      peak_[i] = v.peaks[i] > shown_[i] ? v.peaks[i] : shown_[i];
    } else {  // paused or gone: everything sinks to the floor
      shown_[i] *= 0.8f;
      peak_[i] = peak_[i] > 4 ? peak_[i] - 6 : 0;
    }
    int x = BAR_X0 + i * BAR_STEP;
    int h = (int)(shown_[i] * BAR_MAX_H / 255.0f + 0.5f);
    if (h > 0) u.drawBox(x, BAR_BOTTOM - h + 1, BAR_W, h);
    int ph = (int)(peak_[i] * BAR_MAX_H / 255.0f + 0.5f);
    if (ph > h + 1) u.drawHLine(x, BAR_BOTTOM - ph + 1, BAR_W);
  }
  u.drawHLine(BAR_X0, BAR_BOTTOM, RADIO_BANDS * BAR_STEP - 1);  // floor
}

// Antenna with its tip at (cx, cy). waves: 3 arcs pulse outwards. cross: off air.
void RadioScreen::antenna(int cx, int cy, uint32_t now, bool waves, bool cross) {
  U8G2& u = g();
  if (waves) {
    int phase = (now / 220) % 5;  // 0: none, 1..3: one more arc each step, 4: all
    for (int k = 0; k < 3 && k < phase; k++) {
      int r = 6 + k * 5;
      u.drawCircle(cx, cy, r, U8G2_DRAW_ALL);
      // Open the top and bottom of each ring, so it reads as waves: (( · ))
      u.setDrawColor(0);
      u.drawBox(cx - r / 2, cy - r - 1, r + 1, 3);
      u.drawBox(cx - r / 2, cy + r - 1, r + 1, 3);
      u.setDrawColor(1);
    }
  }
  // The antenna itself, drawn last so the gaps never cut it.
  u.setDrawColor(0);
  u.drawBox(cx - 6, cy + 3, 13, 11);  // clear behind the legs
  u.setDrawColor(1);
  u.drawDisc(cx, cy, 2);
  u.drawVLine(cx, cy, 13);
  u.drawLine(cx, cy + 6, cx - 5, cy + 13);
  u.drawLine(cx, cy + 6, cx + 5, cy + 13);
  u.drawHLine(cx - 6, cy + 13, 13);
  if (cross) {
    u.drawLine(cx + 6, cy - 4, cx + 14, cy + 4);
    u.drawLine(cx + 14, cy - 4, cx + 6, cy + 4);
  }
}

void RadioScreen::volumeOverlay(uint32_t now) {
  if (!volumeShown_) return;
  if (now - volumeAt_ > VOLUME_MS) {
    volumeShown_ = false;
    return;
  }
  U8G2& u = g();
  u.setDrawColor(0);
  u.drawBox(0, 42, W, H - 42);
  u.setDrawColor(1);
  u.drawRFrame(0, 42, W, H - 42, 3);
  u.setFont(F_SMALL);
  u.drawStr(5, 56, "Vol");
  u.drawFrame(24, 49, 76, 8);
  u.drawBox(26, 51, (int)(72 * volume_ / 100), 4);
  char n[6];
  snprintf(n, sizeof(n), "%u", volume_);
  u.drawStr(122 - u.getStrWidth(n), 56, n);
}

void RadioScreen::message(const RadioView& v, const char* l1, const char* l2, const char* foot,
                          bool wifiIcon, uint32_t now) {
  U8G2& u = g();
  header(v, now);
  if (wifiIcon) {  // WiFi arcs with a cross
    int cx = 17, cy = 42;
    u.drawDisc(cx, cy, 2);
    for (int r = 6; r <= 14; r += 4) u.drawCircle(cx, cy, r, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    // Blink the cross: it reads as "waiting", not "broken".
    if ((now / 600) % 2 == 0) {
      u.drawLine(cx + 9, cy - 6, cx + 15, cy);
      u.drawLine(cx + 15, cy - 6, cx + 9, cy);
    }
  } else {
    antenna(14, 24, now, false, true);
  }
  u.setFont(F_BODY);
  u.drawUTF8(38, 29, l1);
  u.drawUTF8(38, 41, l2);
  u.setFont(F_SMALL);
  centered(foot, 61);
}

// ---- screens ----

void RadioScreen::draw(const RadioView& v, uint32_t now) {
  U8G2& u = g();
  u.clearBuffer();
  u.setDrawColor(1);
  switch (v.kind) {
    case RadioView::Kind::Tuning: {
      header(v, now);
      antenna(64, 15, now, true, false);
      stationName(v, 47, now);
      u.setFont(F_SMALL);
      static const char* const DOTS[] = {"", ".", "..", "..."};
      String t = String("Tuning in") + DOTS[(now / 350) % 4];
      u.drawStr((W - u.getStrWidth("Tuning in...")) / 2, 61, t.c_str());
      break;
    }
    case RadioView::Kind::Playing:
    case RadioView::Kind::Paused: {
      header(v, now);
      stationName(v, 24, now);
      u.setFont(F_SMALL);
      if (v.kind == RadioView::Kind::Paused) {
        titleM_ = Marquee();
        centered("Paused - touch to play", 35);
      } else if (v.buffering) {
        static const char* const DOTS[] = {"", ".", "..", "..."};
        String t = String("Buffering") + DOTS[(now / 350) % 4];
        u.drawStr((W - u.getStrWidth("Buffering...")) / 2, 35, t.c_str());
      } else {
        marquee(titleM_, v.title.isEmpty() ? v.info : v.title, 0, 35, W, now);
      }
      levelBars(v, now);
      if (v.kind == RadioView::Kind::Paused) {  // pause sign over the sinking bars
        u.setDrawColor(0);
        u.drawBox(55, 41, 18, 20);
        u.setDrawColor(1);
        u.drawBox(58, 44, 4, 14);
        u.drawBox(66, 44, 4, 14);
      }
      break;
    }
    case RadioView::Kind::Failed:
      message(v, "Station", "unavailable", "Tap: next station", false, now);
      break;
    case RadioView::Kind::NoWifi:
      message(v, "No WiFi", "Waiting...", "Plays when WiFi is back", true, now);
      break;
    case RadioView::Kind::NoStations:
      message(v, "No stations", "Add some at", "http://keycube.local", false, now);
      break;
    case RadioView::Kind::Asleep: {
      header(v, now);
      display.drawBigText(v.clock.c_str(), W / 2, 30, 116, 34);
      u.setFont(F_SMALL);
      centered("Radio off - touch to play", 61);
      break;
    }
  }
  volumeOverlay(now);
  u.sendBuffer();
}
