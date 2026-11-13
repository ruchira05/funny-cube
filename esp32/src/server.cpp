#include "server.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "certs.h"
#include "config.h"
#include "settings.h"

namespace {
// Both clients live in the caller's stack frame; begin() picks one by URL scheme.
struct Conn {
  NetworkClientSecure secure;
  NetworkClient plain;
  HTTPClient http;
  bool https = false;

  bool begin(const String& url, uint16_t timeoutMs) {
    https = url.startsWith("https://");
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(timeoutMs);
    if (https) {
      secure.setCACert(SERVER_ROOT_CA);  // pinned roots, never setInsecure()
      return http.begin(secure, url);
    }
    return http.begin(plain, url);  // plain HTTP: local dev server only
  }

  // Message for a failed connection (negative HTTPClient code).
  String connectError(int code) {
    String msg = "Server unreachable: " + HTTPClient::errorToString(code);
    if (https) {
      // Say why TLS failed (untrusted certificate, wrong clock, DNS...).
      char err[100];
      int tlsErr = secure.lastError(err, sizeof(err));
      if (tlsErr) {
        Serial.printf("[server] TLS error %d: %s\n", tlsErr, err);
        if (strstr(err, "X509") || strstr(err, "verify"))
          msg = "Server certificate not trusted. Update the firmware CA list.";
      }
    }
    return msg;
  }
};

const char* checkUrl(const String& baseUrl) {
  if (baseUrl.isEmpty()) return "No server URL set. Re-run setup.";
  if (!baseUrl.startsWith("https://") && !baseUrl.startsWith("http://"))
    return "Server URL must start with https://";
  return nullptr;
}
}  // namespace

ServerStatus checkServerHealth(const String& baseUrl) {
  if (const char* e = checkUrl(baseUrl)) return {false, 0, e};
  if (WiFi.status() != WL_CONNECTED) return {false, 0, "No WiFi"};

  Conn c;
  String url = baseUrl + "/health";
  if (!c.begin(url, HTTP_TIMEOUT_MS)) return {false, 0, "Bad server URL"};

  int code = c.http.GET();
  ServerStatus st{false, code, ""};
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, c.http.getString()) == DeserializationError::Ok && doc["ok"] == true) {
      st.ok = true;
      st.message = c.https ? "Server OK" : "Server OK (not secure: http)";
    } else {
      st.message = "Server replied, but not key-ai";
    }
  } else if (code > 0) {
    st.message = "Server error " + String(code);
  } else {
    st.message = c.connectError(code);
  }
  c.http.end();
  Serial.printf("[server] GET %s -> %d %s\n", url.c_str(), code, st.message.c_str());
  return st;
}

ChatReply postAudio(const int16_t* pcm, size_t samples) {
  ChatReply r;
  String base = settings.serverUrl();
  if (const char* e = checkUrl(base)) {
    r.error = e;
    return r;
  }
  if (settings.apiKey().isEmpty()) {
    r.error = "No API key. Re-run setup.";
    return r;
  }
  if (WiFi.status() != WL_CONNECTED) {
    r.error = "No WiFi";
    return r;
  }

  Conn c;
  String url = base + "/audio?sample_rate=" + String(MIC_SAMPLE_RATE);
  if (!c.begin(url, CHAT_TIMEOUT_MS)) {
    r.error = "Bad server URL";
    return r;
  }
  c.http.addHeader("Content-Type", "application/octet-stream");
  c.http.addHeader("X-API-Key", settings.apiKey());  // never logged
  c.http.addHeader("X-Device-Id", settings.deviceId());

  uint32_t t0 = millis();
  r.httpCode = c.http.POST((uint8_t*)pcm, samples * sizeof(int16_t));
  uint32_t ms = millis() - t0;

  if (r.httpCode == 200) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, c.http.getString());
    if (err == DeserializationError::Ok) {
      r.text = doc["text"] | "";
      r.reply = doc["reply"] | "";
      r.emotion = doc["emotion"] | "neutral";
      r.ok = !r.reply.isEmpty();
      if (!r.ok) r.error = "Empty reply from the server";
    } else {
      r.error = "Bad reply from the server";
    }
  } else if (r.httpCode > 0) {
    switch (r.httpCode) {
      case 400: r.error = "Too short. Hold the strip while you talk."; break;
      case 401: r.error = "Wrong API key. Re-run setup."; break;
      case 413: r.error = "Recording too long."; break;
      case 502: r.error = "The AI is offline (LLM down). Try later."; break;
      case 503:
      case 504: r.error = "Server busy. Try again."; break;
      default: r.error = "Server error " + String(r.httpCode);
    }
  } else if (r.httpCode == HTTPC_ERROR_READ_TIMEOUT) {
    r.error = "The server took too long. Try again.";
  } else {
    r.error = c.connectError(r.httpCode);
  }
  c.http.end();

  Serial.printf("[chat] POST /audio %.1f s audio -> %d in %u ms\n",
                samples / (float)MIC_SAMPLE_RATE, r.httpCode, (unsigned)ms);
  if (r.ok)
    Serial.printf("[chat] heard: \"%s\"\n[chat] reply (%s): \"%s\"\n", r.text.c_str(),
                  r.emotion.c_str(), r.reply.c_str());
  else
    Serial.printf("[chat] error: %s\n", r.error.c_str());
  return r;
}
