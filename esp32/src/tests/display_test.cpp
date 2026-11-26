// OLED display test for Key Cube. Build and flash with:
//   pio run -e display_test -t upload -t monitor
//
// Cycles through test pages every few seconds. Serial commands (115200):
//   n / Enter  next page        p  previous page
//   space      pause / resume   1-9  jump to page
//
// What to look for:
//   - I2C scan finds the panel at 0x3C (or 0x3D).
//   - "Border": a 1 px frame on all four edges. If the left or right edge is
//     missing or the image wraps by 2 px, flip OLED_SH1106 in config.h.
//   - "All on" / "Checker": dead pixels, stuck lines, uneven brightness.
//   - "Rotation": the arrow points up on all four rotations.
//   - "Screens": every real firmware screen renders without clipping.

#include <Arduino.h>
#include <Wire.h>

#include "../config.h"
#include "../display.h"

namespace {

U8G2& g() { return display.gfx(); }

const uint32_t PAGE_MS = 4000;

void title(const char* t) {
  g().setFont(u8g2_font_5x8_tf);
  int w = g().getStrWidth(t);
  g().setDrawColor(0);
  g().drawBox(0, 0, w + 4, 10);
  g().setDrawColor(1);
  g().drawFrame(0, 0, w + 4, 10);
  g().drawStr(2, 8, t);
}

// ---- pages ----

void pageI2C() {
  g().clearBuffer();
  title("1 I2C scan");
  g().setFont(u8g2_font_5x8_tf);
  int y = 22;
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      char buf[24];
      const char* what = (a == 0x3C || a == 0x3D)   ? "OLED"
                         : (a == 0x1C || a == 0x1D) ? "MMA8452"
                                                    : "?";
      snprintf(buf, sizeof buf, "0x%02X %s", a, what);
      Serial.printf("[i2c] found %s\n", buf);
      if (y <= 64) g().drawStr(0, y, buf);
      y += 9;
      found++;
    }
  }
  if (!found) g().drawStr(0, 22, "No devices?!");
  g().drawStr(70, 22, OLED_SH1106 ? "SH1106" : "SSD1306");
  g().sendBuffer();
}

void pageBorder() {
  int w = g().getDisplayWidth(), h = g().getDisplayHeight();
  g().clearBuffer();
  g().drawFrame(0, 0, w, h);
  g().drawFrame(2, 2, w - 4, h - 4);
  // corner markers: a pixel exactly at each corner pixel inside the inner frame
  g().drawPixel(4, 4);
  g().drawPixel(w - 5, 4);
  g().drawPixel(4, h - 5);
  g().drawPixel(w - 5, h - 5);
  g().setFont(u8g2_font_6x10_tf);
  g().drawStr(30, 28, "2 Border");
  g().setFont(u8g2_font_5x8_tf);
  g().drawStr(14, 42, "All 4 edges visible?");
  char buf[20];
  snprintf(buf, sizeof buf, "%dx%d", w, h);
  g().drawStr(46, 54, buf);
  g().sendBuffer();
}

void pageAllOn() {
  g().clearBuffer();
  g().drawBox(0, 0, g().getDisplayWidth(), g().getDisplayHeight());
  g().sendBuffer();
}

void pageChecker() {
  g().clearBuffer();
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++)
      if ((x + y) & 1) g().drawPixel(x, y);
  g().sendBuffer();
}

void pageLines() {
  g().clearBuffer();
  title("5 Lines");
  for (int x = 0; x < 128; x += 8) g().drawLine(64, 63, x, 12);
  for (int y = 12; y < 64; y += 6) g().drawLine(64, 63, 0, y), g().drawLine(64, 63, 127, y);
  g().drawCircle(110, 30, 14);
  g().drawDisc(18, 30, 10);
  g().sendBuffer();
}

void pageFonts() {
  g().clearBuffer();
  g().setFont(u8g2_font_4x6_tf);
  g().drawStr(0, 6, "4x6 The quick brown fox jumps 0123");
  g().setFont(u8g2_font_5x8_tf);
  g().drawStr(0, 16, "5x8 Quick brown fox 0123");
  g().setFont(u8g2_font_6x10_tf);
  g().drawStr(0, 28, "6x10 Brown fox 0123");
  g().setFont(u8g2_font_helvB10_tr);
  g().drawStr(0, 44, "helvB10 Key Cube");
  g().setFont(u8g2_font_6x10_tf);
  g().drawUTF8(0, 60, "UTF8: \xC2\xB0 \xC2\xB1 \xC2\xB5 \xC3\xA9");
  g().sendBuffer();
}

void pageContrast() {
  // Sweep contrast 0..255 and back, then restore the default.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i <= 255; i += 5) {
      int c = pass == 0 ? i : 255 - i;
      g().setContrast(c);
      g().clearBuffer();
      title("7 Contrast");
      g().setFont(u8g2_font_helvB10_tr);
      char buf[8];
      snprintf(buf, sizeof buf, "%d", c);
      g().drawStr(50, 40, buf);
      g().drawFrame(0, 50, 128, 10);
      g().drawBox(2, 52, c * 124 / 255, 6);
      g().sendBuffer();
      delay(15);
    }
  }
  g().setContrast(255);
}

void drawArrowPage(uint8_t q) {
  int w = g().getDisplayWidth(), h = g().getDisplayHeight();
  g().clearBuffer();
  g().drawFrame(0, 0, w, h);
  int cx = w / 2;
  g().drawTriangle(cx - 10, 24, cx + 10, 24, cx, 6);
  g().drawBox(cx - 3, 24, 7, 14);
  g().setFont(u8g2_font_5x8_tf);
  char buf[8];
  snprintf(buf, sizeof buf, "R%u UP", q);
  g().drawStr((w - g().getStrWidth(buf)) / 2, h - 6, buf);
  g().sendBuffer();
}

void pageRotation() {
  for (uint8_t q = 0; q < 4; q++) {
    display.setRotation(q);
    drawArrowPage(q);
    delay(1200);
  }
  display.setRotation(DISPLAY_UPRIGHT_ROTATION);
}

void pageFps() {
  g().clearBuffer();
  title("9 FPS");
  g().sendBuffer();
  const int frames = 60;
  uint32_t t0 = millis();
  for (int i = 0; i < frames; i++) {
    g().clearBuffer();
    int x = (i * 4) % 120;
    g().drawBox(x, 30, 8, 8);
    g().sendBuffer();
  }
  uint32_t dt = millis() - t0;
  float fps = frames * 1000.0f / dt;
  Serial.printf("[fps] %d full frames in %lu ms = %.1f fps\n", frames, (unsigned long)dt, fps);
  g().clearBuffer();
  title("9 FPS");
  g().setFont(u8g2_font_helvB10_tr);
  char buf[16];
  snprintf(buf, sizeof buf, "%.1f fps", fps);
  g().drawStr(30, 40, buf);
  g().setFont(u8g2_font_5x8_tf);
  g().drawStr(18, 58, "400 kHz I2C, full buf");
  g().sendBuffer();
}

// The real firmware screens, one after another.
void pageScreens() {
  auto hold = [](uint32_t ms) { delay(ms); };
  display.showBoot("Display test");
  hold(1500);
  display.showHoldGesture(1500);
  hold(1000);
  display.showHoldGesture(5000);
  hold(1000);
  for (int i = 0; i < 8; i++) {
    display.showConnecting("MyHomeNetwork-5G");
    hold(150);
  }
  display.showSetupJoinQR("KeyCube-A1B2", "12345678");
  hold(2500);
  display.showSetupUrlQR(false);
  hold(2500);
  display.showSetupFailed("MyHomeNetwork-5G");
  hold(1500);
  display.showNoWifi();
  hold(1500);
  display.showMessage("Connected!", "Joined MyHomeNetwork-5G. This is a long body to test wrapping.");
  hold(2000);
  const Display::Icon icons[] = {Display::Icon::Buddy, Display::Icon::Alarm, Display::Icon::Health,
                                 Display::Icon::Focus};
  const char* names[] = {"Buddy", "Alarm", "Health", "Focus"};
  for (uint8_t q : {0, 1}) {
    display.setRotation(q);
    for (int i = 0; i < 4; i++) {
      display.showMode(names[i], "Hold to talk", icons[i], "Server OK");
      hold(1000);
    }
    display.showStatus("MyHomeNetwork", "192.168.100.123", "Server: reachable", true);
    hold(1500);
  }
  display.setRotation(DISPLAY_UPRIGHT_ROTATION);
}

struct Page {
  const char* name;
  void (*fn)();
};

const Page PAGES[] = {
    {"I2C scan", pageI2C},  {"Border", pageBorder},     {"All on", pageAllOn},
    {"Checker", pageChecker}, {"Lines", pageLines},     {"Fonts", pageFonts},
    {"Contrast", pageContrast}, {"Rotation", pageRotation}, {"FPS", pageFps},
    {"Screens", pageScreens},
};
const int PAGE_COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

int page = 0;
bool paused = false;
uint32_t shownAt = 0;

void show(int i) {
  page = (i % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT;
  Serial.printf("[page %d/%d] %s\n", page + 1, PAGE_COUNT, PAGES[page].name);
  PAGES[page].fn();
  shownAt = millis();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);
  Serial.println("\n=== Key Cube display test ===");
  Serial.printf("Driver: %s, SDA=%d SCL=%d\n", OLED_SH1106 ? "SH1106" : "SSD1306", PIN_I2C_SDA,
                PIN_I2C_SCL);
  Serial.println("Commands: n/Enter next, p prev, space pause, 1-9 / 0 jump");

  display.begin();
  display.on();
  display.setRotation(DISPLAY_UPRIGHT_ROTATION);
  show(0);
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'n' || c == '\n' || c == '\r') {
      if (c == '\r' && Serial.peek() == '\n') Serial.read();
      show(page + 1);
    } else if (c == 'p') {
      show(page - 1);
    } else if (c == ' ') {
      paused = !paused;
      Serial.println(paused ? "[paused]" : "[resumed]");
      shownAt = millis();
    } else if (c >= '1' && c <= '9') {
      show(c - '1');
    } else if (c == '0') {
      show(9);
    }
  }
  if (!paused && millis() - shownAt > PAGE_MS) show(page + 1);
}
