#include "radio.h"

#include <Audio.h>  // ESP32-audioI2S
#include <WiFi.h>
#include <math.h>

#include "alarms.h"
#include "config.h"
#include "net.h"
#include "radioscreen.h"
#include "settings.h"
#include "speaker.h"
#include "touch.h"

RadioPlayer radioPlayer;
RadioMode radioMode;

namespace {
Audio* audio = nullptr;  // player task only; exists while a station plays

constexpr uint32_t TASK_STACK = 16384;  // TLS handshakes run in this task
constexpr uint16_t CONNECT_TIMEOUT_MS = 4000, CONNECT_TIMEOUT_SSL_MS = 6000;
constexpr uint32_t STOP_WAIT_MS = 9000;  // a connection attempt can take this long to give up

const char* stateName(RadioPlayer::State s) {
  switch (s) {
    case RadioPlayer::State::Off: return "off";
    case RadioPlayer::State::Connecting: return "connecting";
    case RadioPlayer::State::Playing: return "playing";
    case RadioPlayer::State::Failed: return "unavailable";
  }
  return "?";
}

void onAudioInfo(Audio::msg_t m) {
  switch (m.e) {
    case Audio::evt_streamtitle:
      radioPlayer.onTitle(m.msg ? m.msg : "");
      break;
    case Audio::evt_spectrum:
      if (m.vec1.size() >= RADIO_BANDS && m.vec2.size() >= RADIO_BANDS)
        radioPlayer.onSpectrum(m.vec1.data(), m.vec2.data(), RADIO_BANDS);
      break;
    default:
      if (m.e == Audio::evt_info && m.msg && strcmp(m.msg, "slow stream") == 0) radioPlayer.onSlowStream();
#if RADIO_DEBUG
      if (m.msg) Serial.printf("[radio] %s: %s\n", m.s ? m.s : "audio", m.msg);
#endif
      break;
  }
}
}  // namespace

// ================================================================
// RadioPlayer
// ================================================================

bool RadioPlayer::begin() {
  if (task_) return true;
  lock_ = xSemaphoreCreateMutex();
  stopped_ = xSemaphoreCreateBinary();
  Audio::audio_info_callback = onAudioInfo;
  // Core 1 next to the Arduino loop (the decoder's own task runs on core 0), one
  // priority above it: network reads come before screen drawing.
  if (xTaskCreatePinnedToCore(taskEntry, "radio", TASK_STACK, this, 2, &task_, 1) != pdPASS) {
    Serial.println("[radio] can't start the player task");
    task_ = nullptr;
    return false;
  }
  return true;
}

void RadioPlayer::play(const String& url) {
  if (!task_ && !begin()) return;
  xSemaphoreTake(lock_, portMAX_DELAY);
  cmdUrl_ = url;
  xSemaphoreGive(lock_);
  portENTER_CRITICAL(&mux_);
  cmd_ = Cmd::Play;
  portEXIT_CRITICAL(&mux_);
  state_ = State::Connecting;  // shown at once; the task confirms it
  xTaskNotifyGive(task_);
}

void RadioPlayer::stop() {
  if (!task_) return;
  if (!hasDecoder_ && state_ == State::Off) return;
  xSemaphoreTake(stopped_, 0);  // drop a stale signal
  portENTER_CRITICAL(&mux_);
  cmd_ = Cmd::Stop;
  portEXIT_CRITICAL(&mux_);
  xTaskNotifyGive(task_);
  if (xSemaphoreTake(stopped_, pdMS_TO_TICKS(STOP_WAIT_MS)) != pdTRUE)
    Serial.println("[radio] WARNING: the player did not stop in time");
}

void RadioPlayer::setVolume(uint8_t v) {
  volume_ = v > 100 ? 100 : v;
  volumeDirty_ = true;
}

String RadioPlayer::title() {
  xSemaphoreTake(lock_, portMAX_DELAY);
  String t = title_;
  xSemaphoreGive(lock_);
  return t;
}

String RadioPlayer::info() {
  xSemaphoreTake(lock_, portMAX_DELAY);
  String t = info_;
  xSemaphoreGive(lock_);
  return t;
}

void RadioPlayer::spectrum(uint8_t bars[RADIO_BANDS], uint8_t peaks[RADIO_BANDS]) {
  portENTER_CRITICAL(&mux_);
  bool fresh = millis() - spectrumAt_ < 500;  // nothing new: the stream is silent or gone
  for (int i = 0; i < RADIO_BANDS; i++) {
    bars[i] = fresh ? bars_[i] : 0;
    peaks[i] = fresh ? peaks_[i] : 0;
  }
  portEXIT_CRITICAL(&mux_);
}

void RadioPlayer::onTitle(const char* t) {
  String s(t);
  s.trim();
  xSemaphoreTake(lock_, portMAX_DELAY);
  title_ = s;
  xSemaphoreGive(lock_);
#if RADIO_DEBUG
  Serial.printf("[radio] title: %s\n", s.c_str());
#endif
}

void RadioPlayer::onSpectrum(const uint32_t* bars, const uint32_t* peaks, size_t n) {
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < n && i < RADIO_BANDS; i++) {
    bars_[i] = bars[i] > 255 ? 255 : bars[i];
    peaks_[i] = peaks[i] > 255 ? 255 : peaks[i];
  }
  spectrumAt_ = millis();
  portEXIT_CRITICAL(&mux_);
}

// The library reports this when its input buffer stays low for 2 s: the network
// delivers less than the station's bitrate, so playback stutters.
void RadioPlayer::onSlowStream() {
  slowAt_ = millis();
  if (millis() - slowLogAt_ > 30000) {  // once per 30 s at most
    slowLogAt_ = millis();
    xSemaphoreTake(lock_, portMAX_DELAY);
    String url = url_;
    xSemaphoreGive(lock_);
    Serial.printf("[radio] slow stream (network slower than the bitrate, RSSI %d dBm): %s\n",
                  WiFi.RSSI(), url.c_str());
  }
}

void RadioPlayer::taskEntry(void* arg) { static_cast<RadioPlayer*>(arg)->taskLoop(); }

void RadioPlayer::setStateAndLog(State s) {
  if (s == state_) return;
  state_ = s;
  if (s == State::Playing || s == State::Failed) {
    xSemaphoreTake(lock_, portMAX_DELAY);
    String url = url_;
    xSemaphoreGive(lock_);
    Serial.printf("[radio] %s: %s\n", stateName(s), url.c_str());
  }
}

void RadioPlayer::createDecoder() {
  audio = new Audio();
  audio->settings.SPECTRUM = true;  // level bars on the screen
  if (!audio->setPinout(PIN_SPK_BCLK, PIN_SPK_LRC, PIN_SPK_DIN)) {
    Serial.println("[radio] I2S setup failed");
  }
  audio->forceMono(true);  // one speaker: the amp plays the left slot only
  audio->setConnectionTimeout(CONNECT_TIMEOUT_MS, CONNECT_TIMEOUT_SSL_MS);
  // 0..100, squared like the tones and capped the same way.
  audio->setVolumeSteps(100);
  audio->setVolumeCurve([](float t) {
    float a = SPK_MAX_AMPLITUDE * t * t;
    return a < 0.001f ? -60.0f : 20.0f * log10f(a);
  });
  audio->setVolume(volume_);
  volumeDirty_ = false;
  audio->setTone(RADIO_LOW_SHELF_DB, 0, 0);
  pinMode(PIN_SPK_SD, OUTPUT);
  digitalWrite(PIN_SPK_SD, HIGH);  // amp on: the decoder outputs silence until audio arrives
  hasDecoder_ = true;
  clockVersion_ = clockVersion_ + 1;
}

void RadioPlayer::deleteDecoder() {
  if (!audio) return;
  digitalWrite(PIN_SPK_SD, LOW);  // amp off first: no click as I2S stops
  delete audio;
  audio = nullptr;
  hasDecoder_ = false;
  clockVersion_ = clockVersion_ + 1;
  portENTER_CRITICAL(&mux_);
  memset(bars_, 0, sizeof(bars_));
  memset(peaks_, 0, sizeof(peaks_));
  spectrumAt_ = 0;
  portEXIT_CRITICAL(&mux_);
}

void RadioPlayer::connect() {
  xSemaphoreTake(lock_, portMAX_DELAY);
  String url = url_;
  title_ = "";
  info_ = "";
  xSemaphoreGive(lock_);
  state_ = State::Connecting;
  retryAt_ = 0;
  digitalWrite(PIN_SPK_SD, HIGH);
  audio->connecttohost(url.c_str());  // false: caught below as "not running"
  connectAt_ = millis();
}

void RadioPlayer::taskLoop() {
  for (;;) {
    portENTER_CRITICAL(&mux_);
    Cmd c = cmd_;
    cmd_ = Cmd::None;
    portEXIT_CRITICAL(&mux_);

    if (c == Cmd::Play) {
      xSemaphoreTake(lock_, portMAX_DELAY);
      url_ = cmdUrl_;
      xSemaphoreGive(lock_);
      attempts_ = 0;
      if (!audio) createDecoder();
      else audio->stopSong();
      connect();
    } else if (c == Cmd::Stop) {
      deleteDecoder();
      state_ = State::Off;
      xSemaphoreGive(stopped_);
    }

    if (!audio) {  // idle until the next command
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }

    if (volumeDirty_) {
      volumeDirty_ = false;
      audio->setVolume(volume_);
    }
    audio->loop();

    uint32_t now = millis();
    switch (state_) {
      case State::Connecting:
        if (retryAt_) {
          if ((int32_t)(now - retryAt_) >= 0) connect();
        } else if (audio->isRunning() && audio->getSampleRate() > 0 && audio->inBufferFilled() > 0) {
          setStateAndLog(State::Playing);
          infoAt_ = 0;
        } else if (!audio->isRunning() || now - connectAt_ > RADIO_CONNECT_TIMEOUT_MS) {
          audio->stopSong();
          if (attempts_ < RADIO_RETRIES) {
            attempts_++;
            retryAt_ = now + RADIO_RETRY_PAUSE_MS;
          } else {
            digitalWrite(PIN_SPK_SD, LOW);
            setStateAndLog(State::Failed);
          }
        }
        break;
      case State::Playing:
        if (!audio->isRunning()) {  // the stream dropped: reconnect
          Serial.println("[radio] stream ended, reconnecting");
          attempts_ = 0;
          state_ = State::Connecting;
          retryAt_ = now + RADIO_RETRY_PAUSE_MS;
        } else if (!infoAt_ || now - infoAt_ > 3000) {
#if RADIO_STATS
          static uint32_t statsAt = 0;
          if (now - statsAt >= 5000) {
            statsAt = now;
            Serial.printf("[radio] buffer %u KB, %u Hz, %u kbps, RSSI %d dBm, WiFi sleep %s%s\n",
                          (unsigned)(audio->inBufferFilled() / 1024),
                          (unsigned)audio->getSampleRate(),
                          (unsigned)((audio->getBitRate() + 500) / 1000), WiFi.RSSI(),
                          WiFi.getSleep() ? "ON" : "off", buffering() ? ", SLOW" : "");
          }
#endif
          infoAt_ = now;
          uint32_t br = audio->getBitRate();
          String inf = String(audio->getCodecname());
          if (br) inf += " " + String((br + 500) / 1000) + " kbps";
          xSemaphoreTake(lock_, portMAX_DELAY);
          info_ = inf;
          xSemaphoreGive(lock_);
        }
        break;
      default:
        break;
    }
    // Faster while data flows; Failed only waits for a command.
    if (state_ == State::Failed) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
    else vTaskDelay(1);
  }
}

// ================================================================
// RadioMode
// ================================================================

void RadioMode::enter() {
  active_ = true;
  paused_ = asleep_ = started_ = false;
  index_ = settings.radioStation();
  playingUrl_ = "";
  volume_ = settings.radioVolume();
  radioPlayer.begin();
  radioPlayer.setVolume(volume_);
  sleepAt_ = 0;  // set when a station starts
  wifiDown_ = false;
  speaker.release();  // the decoder takes the I2S pins when a station starts
  // WiFi power save makes the radio doze between beacons and answer packets late.
  // Over a long route (US servers) that cuts TCP throughput below 128 kbps and
  // the stream stutters. Off while the radio plays; applied when WiFi starts too.
  WiFi.setSleep(false);
  touch.rebaseline();
  clockVersion_ = radioPlayer.clockVersion();
  radioScreen.reset();
  lastDraw_ = 0;
  Serial.println("[radio] on");
}

void RadioMode::exit() {
  if (!active_) return;
  active_ = false;
  stopStream();
  WiFi.setSleep(true);  // back to the default (saves power in the other modes)
  speaker.reclaim();
  touch.rebaseline();
  Serial.println("[radio] off");
}

void RadioMode::startCurrent() {
  const auto& list = settings.stations();
  if (list.empty()) return;
  if (index_ < 0 || index_ >= (int)list.size()) index_ = 0;
  playingUrl_ = list[index_].url;
  Serial.printf("[radio] tuning %d/%u %s\n", index_ + 1, (unsigned)list.size(),
                list[index_].name.c_str());
  radioPlayer.play(playingUrl_);
  started_ = true;
}

void RadioMode::stopStream() {
  radioPlayer.stop();
  started_ = false;
}

void RadioMode::next() {
  const auto& list = settings.stations();
  if (list.empty()) return;
  String from = index_ < (int)list.size() ? list[index_].name : String("");
  index_ = (index_ + 1) % list.size();
  settings.setRadioStation(index_);
  radioScreen.slide(from, list[index_].name, millis());
  if (net.connected()) startCurrent();
  else started_ = false;  // starts when WiFi is back
}

void RadioMode::playIndex(int i) {
  const auto& list = settings.stations();
  if (i < 0 || i >= (int)list.size()) return;
  settings.setRadioStation(i);
  if (!active_) return;
  String from = index_ < (int)list.size() ? list[index_].name : String("");
  index_ = i;
  paused_ = asleep_ = false;
  radioScreen.slide(from, list[i].name, millis());
  if (net.connected()) startCurrent();
  else started_ = false;
}

void RadioMode::stationsChanged() {
  const auto& list = settings.stations();
  if (!active_) return;
  int found = -1;
  for (size_t i = 0; i < list.size(); i++)
    if (list[i].url == playingUrl_) found = i;
  if (found >= 0) {  // still in the list (maybe moved): keep playing
    index_ = found;
    settings.setRadioStation(index_);
    return;
  }
  if (list.empty()) {
    stopStream();
    index_ = 0;
    return;
  }
  if (index_ >= (int)list.size()) index_ = list.size() - 1;
  settings.setRadioStation(index_);
  if (started_) startCurrent();  // the station that played is gone
}

void RadioMode::loop() {
  uint32_t now = millis();
  const auto& list = settings.stations();

  // The I2S clock started or stopped: the touch reading moves with it.
  if (radioPlayer.clockVersion() != clockVersion_) {
    clockVersion_ = radioPlayer.clockVersion();
    touch.rebaseline();
  }
  if (settings.radioVolume() != volume_) {  // changed on the settings page
    volume_ = settings.radioVolume();
    radioPlayer.setVolume(volume_);
    radioScreen.showVolume(volume_, now);
  }

  // Sleep timer: stop playing and show the clock; a touch plays again. It
  // counts from when a station starts, and restarts after a pause.
  uint32_t sleepMs = settings.radioSleepMin() * 60000UL;
  if (!sleepMs) sleepAt_ = 0;  // switched off on the settings page
  if (sleepAt_ && (int32_t)(now - sleepAt_) >= 0) {
    Serial.println("[radio] sleep timer: off");
    asleep_ = true;
    stopStream();
  }

  if (touch.consumeHold()) {
    if (paused_ || asleep_) {
      paused_ = asleep_ = false;
    } else if (!list.empty()) {
      Serial.println("[radio] paused");
      paused_ = true;
      stopStream();
    }
  }
  if (touch.consumeTap()) {
    if (paused_ || asleep_) paused_ = asleep_ = false;  // any touch plays again
    else next();
  }
  if (paused_ || asleep_) sleepAt_ = 0;

  // Play when WiFi is up. After a WiFi drop, retry a station that gave up meanwhile.
  bool wantPlay = !paused_ && !asleep_ && !list.empty();
  if (!net.connected()) {
    wifiDown_ = true;
  } else {
    if (wifiDown_ && started_ && radioPlayer.state() != RadioPlayer::State::Playing) startCurrent();
    wifiDown_ = false;
    if (wantPlay && !started_) startCurrent();
  }
  if (wantPlay && started_ && sleepMs && !sleepAt_) sleepAt_ = now + sleepMs;

  if (now - lastDraw_ >= RADIO_FRAME_MS) {
    lastDraw_ = now;
    draw(now);
  }
}

void RadioMode::draw(uint32_t now) {
  const auto& list = settings.stations();
  RadioView v;
  v.count = list.size();
  v.index = index_;
  v.name = index_ < (int)list.size() ? list[index_].name : String("");
  RadioPlayer::State s = radioPlayer.state();
  if (list.empty()) v.kind = RadioView::Kind::NoStations;
  else if (asleep_) v.kind = RadioView::Kind::Asleep;
  else if (paused_) v.kind = RadioView::Kind::Paused;
  else if (!net.connected() && s != RadioPlayer::State::Playing) v.kind = RadioView::Kind::NoWifi;
  else if (s == RadioPlayer::State::Failed) v.kind = RadioView::Kind::Failed;
  else if (s == RadioPlayer::State::Playing) v.kind = RadioView::Kind::Playing;
  else v.kind = RadioView::Kind::Tuning;
  if (v.kind == RadioView::Kind::Playing) {
    v.buffering = radioPlayer.buffering();
    v.title = radioPlayer.title();
    v.info = radioPlayer.info();
    radioPlayer.spectrum(v.bars, v.peaks);
  }
  int rssi = net.connected() ? WiFi.RSSI() : 0;
  v.wifiBars = !net.connected() ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  v.sleepMin = sleepAt_ ? (int)((sleepAt_ - now + 59999) / 60000) : 0;
  v.clock = hhmm(nowEpoch());
  radioScreen.draw(v, now);
}
