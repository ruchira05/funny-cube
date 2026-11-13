// Calls to the key-ai server: GET /health (Stage 1), POST /audio (Stage 3).
#pragma once

#include <Arduino.h>

struct ServerStatus {
  bool ok;
  int httpCode;    // HTTP status, or a negative HTTPClient error
  String message;  // short text for the display
};

// baseUrl like "https://your.domain.com" (no trailing slash).
ServerStatus checkServerHealth(const String& baseUrl);

struct ChatReply {
  bool ok = false;
  int httpCode = 0;
  String text;     // what the server heard
  String reply;    // the answer
  String emotion;  // happy, sad, angry, surprised, neutral, thinking, love
  String error;    // short text for the display when !ok
};

// POST raw 16-bit mono PCM to /audio. Uses the server URL, API key and device ID
// from settings. Blocking (up to CHAT_TIMEOUT_MS): call from a background task.
ChatReply postAudio(const int16_t* pcm, size_t samples);
