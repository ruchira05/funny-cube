// 1.3" 128x64 OLED (SH1106 or SSD1306) over I2C: status screens and QR codes.
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

class Display {
 public:
  void begin();
  U8G2& gfx();

  // Rotation in quarter turns: 0..3 = U8G2_R0..R3. R1/R3 are portrait (64 x 128).
  void setRotation(uint8_t quarterTurns);
  uint8_t rotation() const { return rotation_; }
  void off();  // panel sleep (screen blank, about 10 uA)
  void on();

  // ---- Stage 1 screens ----
  void showBoot(const char* hint = nullptr);  // hint: small line under the version
  void showHoldGesture(uint32_t heldMs);  // touch held at power-on
  void showFactoryReset();
  void showConnecting(const String& ssid);  // call repeatedly: animates a spinner
  void showSetupJoinQR(const String& apName, const String& apPass);
  void showSetupUrlQR(bool phoneConnected);
  void showSetupTesting(const String& ssid);
  void showSetupFailed(const String& ssid);
  void showNoWifi();
  void showMessage(const char* title, const String& body);

  // ---- Stage 2 screens (work in any rotation) ----
  enum class Icon { Buddy, Alarm, Health, Focus };
  void showMode(const char* name, const char* label, Icon icon, const String& status);
  void showStatus(const String& ssid, const String& ip, const String& serverLine, bool ok);

  // ---- Stage 4 ----
  // Digits and ':' in the largest font that fits maxW x maxH, centred on (cx, cy).
  // Returns the height used.
  int drawBigText(const char* text, int cx, int cy, int maxW, int maxH);
  // Text centred horizontally on the screen at baseline y, in the current font.
  void drawCentered(const char* text, int y);

  // Word-wraps text into width w using the current font.
  void drawWrapped(const String& text, int x, int y, int w, int lineH, int maxLines);

 private:
  bool drawQR(const char* text, int x, int y, int maxPx);
  void drawFace(int cx, int cy, int r, bool happy);
  void drawIcon(Icon icon, int cx, int cy);
  uint8_t spin_ = 0;
  uint8_t rotation_ = 0;
};

extern Display display;
