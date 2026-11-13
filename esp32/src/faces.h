// Emotion faces for the mono OLED. Faces differ by shape only (eyes, brows, mouth).
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

enum class Emotion { Neutral, Happy, Sad, Angry, Surprised, Thinking, Love, Listening };

// Server emotion name -> Emotion (unknown names -> Neutral).
Emotion emotionFromName(const String& name);

// Face centred at (cx, cy) with radius r (r >= 14 looks right). frame animates
// Thinking (dots) and Listening (eyes); pass a counter that increases.
void drawEmotion(U8G2& g, Emotion e, int cx, int cy, int r, uint8_t frame = 0);
