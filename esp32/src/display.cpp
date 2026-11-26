#include "display.h"

#include <Wire.h>
#include <qrcode.h>  // ESP-IDF QR encoder bundled with the ESP32 core

#include "config.h"

#if OLED_SH1106
static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#else
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
#endif

Display display;

namespace {
const uint8_t* FONT_SMALL = u8g2_font_5x8_tf;    // 5x8: about 25 chars across
const uint8_t* FONT_BODY = u8g2_font_6x10_tf;    // 6x10: about 21 chars across
const uint8_t* FONT_TITLE = u8g2_font_helvB10_tr;

// The QR encoder hands the result to a plain C callback, so pass the target here.
struct QrTarget {
  int x, y, maxPx;
  bool ok;
} g_qr;

void qrDraw(esp_qrcode_handle_t qr) {
  int n = esp_qrcode_get_size(qr);
  int scale = g_qr.maxPx / (n + 2);  // leave at least a 1-module quiet zone
  if (scale < 1) {
    g_qr.ok = false;
    return;
  }
  int quiet = (g_qr.maxPx - n * scale) / 2;
  int total = n * scale + 2 * quiet;
  // Scanners expect dark modules on a light background, so light the whole
  // square and switch off the dark modules.
  u8g2.setDrawColor(1);
  u8g2.drawBox(g_qr.x, g_qr.y, total, total);
  u8g2.setDrawColor(0);
  for (int my = 0; my < n; my++)
    for (int mx = 0; mx < n; mx++)
      if (esp_qrcode_get_module(qr, mx, my))
        u8g2.drawBox(g_qr.x + quiet + mx * scale, g_qr.y + quiet + my * scale, scale, scale);
  u8g2.setDrawColor(1);
  g_qr.ok = true;
}
}  // namespace

void Display::begin() {
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.enableUTF8Print();
}

U8G2& Display::gfx() { return u8g2; }

void Display::setRotation(uint8_t q) {
  static const u8g2_cb_t* rot[] = {U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3};
  q &= 3;
  if (q == rotation_) return;
  rotation_ = q;
  u8g2.setDisplayRotation(rot[q]);
}

void Display::off() {
  u8g2.clearBuffer();
  u8g2.sendBuffer();
  u8g2.setPowerSave(1);
}

void Display::on() { u8g2.setPowerSave(0); }

bool Display::drawQR(const char* text, int x, int y, int maxPx) {
  g_qr = {x, y, maxPx, false};
  esp_qrcode_config_t cfg = {};
  cfg.display_func = qrDraw;
  cfg.max_qrcode_version = 4;  // version 4 = 33 modules; larger won't fit 64 px at 1 px quiet zone
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  return esp_qrcode_generate(&cfg, text) == ESP_OK && g_qr.ok;
}

void Display::drawFace(int cx, int cy, int r, bool happy) {
  u8g2.drawCircle(cx, cy, r);
  int ex = r / 3, ey = r / 3;
  u8g2.drawDisc(cx - ex, cy - ey, 2);
  u8g2.drawDisc(cx + ex, cy - ey, 2);
  int mr = r / 2;
  if (happy) {
    u8g2.drawCircle(cx, cy, mr, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
  } else {
    u8g2.drawCircle(cx, cy + mr + 2, mr, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  }
}

void Display::drawWrapped(const String& text, int x, int y, int w, int lineH, int maxLines) {
  String line, word;
  int lines = 0;
  auto flush = [&]() {
    if (lines < maxLines) u8g2.drawStr(x, y + lines * lineH, line.c_str());
    lines++;
    line = "";
  };
  for (size_t i = 0; i <= text.length(); i++) {
    char c = i < text.length() ? text[i] : ' ';
    if (c == ' ' || c == '\n') {
      String candidate = line.isEmpty() ? word : line + " " + word;
      if (!word.isEmpty() && u8g2.getStrWidth(candidate.c_str()) > w && !line.isEmpty()) {
        flush();
        line = word;
      } else if (!word.isEmpty()) {
        line = candidate;
      }
      word = "";
      if (c == '\n') flush();
    } else {
      word += c;
    }
  }
  if (!line.isEmpty()) flush();
}

void Display::showBoot(const char* hint) {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_TITLE);
  u8g2.drawStr(26, 30, "Key Cube");
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(26, 46, "v" FW_VERSION);
  if (hint) u8g2.drawStr((128 - u8g2.getStrWidth(hint)) / 2, 62, hint);
  u8g2.sendBuffer();
}

void Display::showHoldGesture(uint32_t heldMs) {
  u8g2.clearBuffer();
  u8g2.setFont(FONT_BODY);
  const char* msg;
  uint32_t target;
  if (heldMs < SETUP_HOLD_MS) {
    msg = "Keep holding: setup";
    target = SETUP_HOLD_MS;
  } else if (heldMs < RESET_HOLD_MS) {
    msg = "Release: WiFi setup";
    target = RESET_HOLD_MS;
  } else {
    msg = "Release: RESET ALL";
    target = RESET_HOLD_MS;
  }
  u8g2.drawStr(0, 12, msg);
  u8g2.setFont(FONT_SMALL);
  if (heldMs >= SETUP_HOLD_MS && heldMs < RESET_HOLD_MS)
    u8g2.drawStr(0, 26, "Hold to 10 s to reset");
  // progress bar toward the next threshold
  u8g2.drawFrame(0, 40, 128, 10);
  uint32_t start = heldMs < SETUP_HOLD_MS ? 0 : SETUP_HOLD_MS;
  uint32_t w = heldMs >= RESET_HOLD_MS ? 124 : (heldMs - start) * 124 / (target - start);
  u8g2.drawBox(2, 42, w, 6);
  u8g2.sendBuffer();
}

void Display::showFactoryReset() {
  showMessage("Factory reset", "All WiFi networks and server settings were erased.");
}

void Display::showConnecting(const String& ssid) {
  static const char* frames[] = {"|", "/", "-", "\\"};
  u8g2.clearBuffer();
  u8g2.setFont(FONT_BODY);
  u8g2.drawStr(0, 12, "Connecting to");
  u8g2.setFont(FONT_SMALL);
  drawWrapped(ssid, 0, 26, 128, 9, 2);
  u8g2.setFont(FONT_TITLE);
  u8g2.drawStr(60, 58, frames[spin_++ % 4]);
  u8g2.sendBuffer();
}

void Display::showSetupJoinQR(const String& apName, const String& apPass) {
  // Standard WiFi QR format, understood by iOS and Android cameras.
  String payload = "WIFI:T:WPA;S:" + apName + ";P:" + apPass + ";;";
  u8g2.clearBuffer();
  if (!drawQR(payload.c_str(), 0, 0, 64)) {
    u8g2.setFont(FONT_SMALL);
    u8g2.drawStr(0, 30, "QR error");
  }
  u8g2.setFont(FONT_BODY);
  u8g2.drawStr(67, 10, "Scan to");
  u8g2.drawStr(67, 21, "set up");
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(67, 36, "WiFi:");
  u8g2.drawStr(67, 45, apName.c_str());
  u8g2.drawStr(67, 54, "Pass:");
  u8g2.drawStr(67, 63, apPass.c_str());
  u8g2.sendBuffer();
}

void Display::showSetupUrlQR(bool phoneConnected) {
  u8g2.clearBuffer();
  if (!drawQR("http://" AP_IP_STR "/", 0, 0, 64)) {
    u8g2.setFont(FONT_SMALL);
    u8g2.drawStr(0, 30, "QR error");
  }
  u8g2.setFont(FONT_BODY);
  if (phoneConnected) {
    u8g2.drawStr(67, 10, "Phone");
    u8g2.drawStr(67, 21, "joined");
  } else {
    u8g2.drawStr(67, 10, "Open");
    u8g2.drawStr(67, 21, "page");
  }
  // Right column is 61 px wide: at most 12 characters of the 5x8 font per line.
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(67, 36, "Scan, or go");
  u8g2.drawStr(67, 45, "to:");
  u8g2.drawStr(67, 54, AP_IP_STR);
  u8g2.drawStr(67, 63, "Tap: WiFi QR");
  u8g2.sendBuffer();
}

void Display::showSetupTesting(const String& ssid) { showConnecting(ssid); }

void Display::showSetupFailed(const String& ssid) {
  u8g2.clearBuffer();
  drawFace(18, 24, 16, false);
  u8g2.setFont(FONT_BODY);
  u8g2.drawStr(42, 12, "Can't join");
  u8g2.setFont(FONT_SMALL);
  drawWrapped(ssid, 42, 24, 86, 9, 2);
  u8g2.drawStr(0, 52, "Check the password");
  u8g2.drawStr(0, 62, "and try again on phone");
  u8g2.sendBuffer();
}

void Display::showNoWifi() {
  u8g2.clearBuffer();
  drawFace(18, 24, 16, false);
  u8g2.setFont(FONT_BODY);
  u8g2.drawStr(42, 12, "No WiFi");
  u8g2.setFont(FONT_SMALL);
  u8g2.drawStr(42, 26, "Saved networks");
  u8g2.drawStr(42, 35, "not found.");
  u8g2.drawStr(0, 56, "Starting setup...");
  u8g2.sendBuffer();
}

void Display::showMessage(const char* title, const String& body) {
  int w = u8g2.getDisplayWidth(), h = u8g2.getDisplayHeight();
  u8g2.clearBuffer();
  u8g2.setFont(FONT_BODY);
  drawWrapped(title, 0, 12, w, 11, 2);
  u8g2.setFont(FONT_SMALL);
  int y = u8g2.getStrWidth(title) > w ? 39 : 28;
  drawWrapped(body, 0, y, w, 10, (h - y) / 10 + 1);
  u8g2.sendBuffer();
}

// ---- Stage 2 ----

void Display::drawIcon(Icon icon, int cx, int cy) {
  switch (icon) {
    case Icon::Buddy:
      drawFace(cx, cy, 14, true);
      break;
    case Icon::Alarm:  // alarm clock: body, two bells, hands, feet
      u8g2.drawCircle(cx, cy + 1, 12);
      u8g2.drawDisc(cx - 10, cy - 11, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
      u8g2.drawDisc(cx + 10, cy - 11, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
      u8g2.drawLine(cx, cy + 1, cx, cy - 7);
      u8g2.drawLine(cx, cy + 1, cx + 5, cy + 4);
      u8g2.drawLine(cx - 8, cy + 11, cx - 11, cy + 15);
      u8g2.drawLine(cx + 8, cy + 11, cx + 11, cy + 15);
      break;
    case Icon::Health:  // heart
      u8g2.drawDisc(cx - 6, cy - 4, 7);
      u8g2.drawDisc(cx + 6, cy - 4, 7);
      u8g2.drawTriangle(cx - 13, cy - 2, cx + 13, cy - 2, cx, cy + 13);
      break;
    case Icon::Focus:  // hourglass
      u8g2.drawBox(cx - 10, cy - 14, 21, 2);
      u8g2.drawBox(cx - 10, cy + 13, 21, 2);
      u8g2.drawLine(cx - 8, cy - 12, cx + 8, cy + 12);
      u8g2.drawLine(cx + 8, cy - 12, cx - 8, cy + 12);
      u8g2.drawLine(cx - 8, cy - 12, cx + 8, cy - 12);
      u8g2.drawTriangle(cx - 6, cy + 12, cx + 7, cy + 12, cx, cy + 3);
      break;
  }
}

void Display::showMode(const char* name, const char* label, Icon icon, const String& status) {
  int w = u8g2.getDisplayWidth(), h = u8g2.getDisplayHeight();
  u8g2.clearBuffer();
  if (w > h) {  // landscape 128 x 64: icon left, text right
    drawIcon(icon, 20, 24);
    u8g2.setFont(FONT_TITLE);
    u8g2.drawStr(44, 18, name);
    u8g2.setFont(FONT_SMALL);
    u8g2.drawStr(44, 30, label);
    u8g2.drawHLine(0, 50, w);
    u8g2.drawStr(0, 61, status.c_str());
  } else {  // portrait 64 x 128: stacked
    drawIcon(icon, w / 2, 26);
    u8g2.setFont(FONT_TITLE);
    drawWrapped(name, (w - u8g2.getStrWidth(name)) / 2 > 0 ? (w - u8g2.getStrWidth(name)) / 2 : 0,
                64, w, 13, 2);
    u8g2.setFont(FONT_SMALL);
    u8g2.drawStr((w - u8g2.getStrWidth(label)) / 2, 90, label);
    u8g2.drawHLine(0, 110, w);
    u8g2.setFont(u8g2_font_4x6_tf);  // 16 characters across in portrait
    u8g2.drawStr(0, 121, status.c_str());
  }
  u8g2.sendBuffer();
}

void Display::showStatus(const String& ssid, const String& ip, const String& serverLine, bool ok) {
  int w = u8g2.getDisplayWidth(), h = u8g2.getDisplayHeight();
  bool portrait = h > w;
  u8g2.clearBuffer();
  if (portrait) {
    drawFace(w / 2, 18, 14, ok);
    u8g2.setFont(FONT_BODY);
    u8g2.drawStr(0, 48, "Status");
  } else {
    drawFace(18, 20, 16, ok);
    u8g2.setFont(FONT_BODY);
    u8g2.drawStr(42, 12, "Status");
  }
  // 4x6 in portrait: an IP address needs 15 characters, 64 px fits 16.
  u8g2.setFont(portrait ? u8g2_font_4x6_tf : FONT_SMALL);
  int x = portrait ? 0 : 42, y = portrait ? 60 : 24, lh = portrait ? 8 : 10;
  int tw = w - x;
  u8g2.drawStr(x, y, ssid.isEmpty() ? "No WiFi" : ssid.c_str());
  if (!ip.isEmpty()) u8g2.drawStr(x, y + lh, ip.c_str());
  int sy = portrait ? y + 2 * lh + 4 : 50;
  drawWrapped(serverLine, 0, sy, portrait ? w : tw + x, lh, portrait ? 6 : 2);
  u8g2.sendBuffer();
}

// ---- Stage 4 ----

int Display::drawBigText(const char* text, int cx, int cy, int maxW, int maxH) {
  // Largest first. The _tn variants hold only digits and a few symbols (":", "-").
  static const uint8_t* const fonts[] = {
      u8g2_font_logisoso42_tn, u8g2_font_logisoso38_tn, u8g2_font_logisoso32_tn,
      u8g2_font_logisoso28_tn, u8g2_font_logisoso24_tn, u8g2_font_logisoso20_tn,
      u8g2_font_logisoso16_tn, u8g2_font_10x20_tn,
  };
  for (const uint8_t* f : fonts) {
    u8g2.setFont(f);
    int tw = u8g2.getStrWidth(text), th = u8g2.getAscent();
    if (tw <= maxW && th <= maxH) {
      u8g2.drawStr(cx - tw / 2, cy + th / 2, text);
      return th;
    }
  }
  return 0;
}

void Display::drawCentered(const char* text, int y) {
  int w = u8g2.getDisplayWidth(), tw = u8g2.getStrWidth(text);
  u8g2.drawStr(tw >= w ? 0 : (w - tw) / 2, y, text);
}
