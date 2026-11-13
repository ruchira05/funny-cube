#include "faces.h"

Emotion emotionFromName(const String& name) {
  String n = name;
  n.toLowerCase();
  if (n == "happy") return Emotion::Happy;
  if (n == "sad") return Emotion::Sad;
  if (n == "angry") return Emotion::Angry;
  if (n == "surprised") return Emotion::Surprised;
  if (n == "thinking") return Emotion::Thinking;
  if (n == "love") return Emotion::Love;
  return Emotion::Neutral;
}

namespace {
void heart(U8G2& g, int x, int y, int s) {  // centred at (x, y), about 2s wide
  int h = s / 2 + 1;
  g.drawDisc(x - h + 1, y - 1, h);
  g.drawDisc(x + h - 1, y - 1, h);
  g.drawTriangle(x - s - 1, y, x + s + 1, y, x, y + s + 1);
}
}  // namespace

void drawEmotion(U8G2& g, Emotion e, int cx, int cy, int r, uint8_t frame) {
  g.drawCircle(cx, cy, r);
  int ex = r * 2 / 5;          // eye offset from centre, x
  int ey = cy - r / 4;         // eye line
  int er = r >= 20 ? 3 : 2;    // eye radius
  int my = cy + r * 2 / 5;     // mouth line
  int mw = r / 2;              // half mouth width

  switch (e) {
    case Emotion::Happy:
      g.drawDisc(cx - ex, ey, er);
      g.drawDisc(cx + ex, ey, er);
      g.drawCircle(cx, my - mw / 2, mw, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      break;

    case Emotion::Sad:
      g.drawDisc(cx - ex, ey + 1, er);
      g.drawDisc(cx + ex, ey + 1, er);
      g.drawLine(cx - ex - er, ey - er - 2, cx - ex + er, ey - er - 4);  // brows tilt up inward
      g.drawLine(cx + ex + er, ey - er - 2, cx + ex - er, ey - er - 4);
      g.drawCircle(cx, my + mw / 2, mw, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
      break;

    case Emotion::Angry:
      g.drawDisc(cx - ex, ey + 1, er);
      g.drawDisc(cx + ex, ey + 1, er);
      // Brows slant down toward the middle (two lines each for weight).
      for (int t = 0; t < 2; t++) {
        g.drawLine(cx - ex - er - 1, ey - er - 4 + t, cx - ex + er + 1, ey - er - 1 + t);
        g.drawLine(cx + ex + er + 1, ey - er - 4 + t, cx + ex - er - 1, ey - er - 1 + t);
      }
      g.drawHLine(cx - mw + 2, my + 1, 2 * mw - 3);
      g.drawLine(cx - mw + 2, my + 1, cx - mw, my + 3);
      g.drawLine(cx + mw - 2, my + 1, cx + mw, my + 3);
      break;

    case Emotion::Surprised:
      g.drawCircle(cx - ex, ey, er + 1);
      g.drawCircle(cx + ex, ey, er + 1);
      g.drawDisc(cx - ex, ey, 1);
      g.drawDisc(cx + ex, ey, 1);
      g.drawCircle(cx, my, r / 6 + 1);
      break;

    case Emotion::Thinking:
      g.drawHLine(cx - ex - er, ey, 2 * er + 1);  // one eye squinting
      g.drawDisc(cx + ex, ey - 1, er);
      g.drawLine(cx + ex - er, ey - er - 4, cx + ex + er, ey - er - 3);  // raised brow
      g.drawHLine(cx - mw / 2, my, mw);
      g.drawPixel(cx + mw / 2, my - 1);
      // Animated thought dots, top right (inside the screen for any rotation).
      for (int i = 0; i <= (frame % 4) && i < 3; i++)
        g.drawDisc(cx + r * 7 / 10 + i * 4, cy - r * 7 / 10 - i * 3, 1 + (i == 2));
      break;

    case Emotion::Love:
      heart(g, cx - ex, ey, er + 1);
      heart(g, cx + ex, ey, er + 1);
      g.drawCircle(cx, my - mw / 2, mw, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
      break;

    case Emotion::Listening: {
      int look = (frame / 4) % 2 ? 1 : -1;  // eyes glance side to side
      g.drawDisc(cx - ex + look, ey, er);
      g.drawDisc(cx + ex + look, ey, er);
      g.drawEllipse(cx, my, mw / 2 + 1, 2);
      break;
    }

    case Emotion::Neutral:
    default:
      g.drawDisc(cx - ex, ey, er);
      g.drawDisc(cx + ex, ey, er);
      g.drawHLine(cx - mw / 2, my, mw);
      break;
  }
}
