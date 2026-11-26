#include "alarms.h"

#include <esp_sntp.h>
#include <sys/time.h>

#include "config.h"
#include "display.h"
#include "touch.h"

Schedule schedule;
Alerts alerts;

namespace {
// Kept in RTC memory: survives deep sleep (reset by a power cut).
RTC_DATA_ATTR time_t rtcLastCheck = 0;  // alarms between this and now are due
RTC_DATA_ATTR time_t rtcSnoozeAt = 0;
RTC_DATA_ATTR int8_t rtcSnoozeSlot = -1;
RTC_DATA_ATTR time_t rtcRemNext[REMINDER_COUNT] = {};

constexpr time_t VALID_AFTER = 1735689600;  // 2025-01-01: earlier means "never synced"

const char* const REM_NAMES[REMINDER_COUNT] = {"Water", "Stretch", "Posture"};
const char* const REM_PROMPTS[REMINDER_COUNT] = {"Drink some water", "Stand up and stretch",
                                                 "Sit up straight"};

void onTimeSync(struct timeval*) {
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  Serial.printf("[time] synced: %04d-%02d-%02d %02d:%02d:%02d\n", t.tm_year + 1900, t.tm_mon + 1,
                t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

U8G2& g() { return display.gfx(); }
const uint8_t* FONT_SMALL = u8g2_font_5x8_tf;
const uint8_t* FONT_BODY = u8g2_font_6x10_tf;
const uint8_t* FONT_TITLE = u8g2_font_helvB10_tr;
}  // namespace

// ------------------------------------------------------------------
// Time
// ------------------------------------------------------------------

void timeBegin() {
  setenv("TZ", settings.timezone().c_str(), 1);
  tzset();
  sntp_set_time_sync_notification_cb(onTimeSync);
}

void timeOnConnected() {
  sntp_set_sync_interval(NTP_RESYNC_MS);  // applies from the (re)start below
  configTzTime(settings.timezone().c_str(), NTP_SERVER_1, NTP_SERVER_2);
}

bool timeValid() { return time(nullptr) > VALID_AFTER; }
time_t nowEpoch() { return time(nullptr); }

bool localTm(time_t t, struct tm& out) {
  localtime_r(&t, &out);
  return t > VALID_AFTER;
}

uint32_t localDay(time_t t) {
  struct tm d;
  localTm(t, d);
  return (uint32_t)(d.tm_year * 400 + d.tm_yday);
}

String hhmm(time_t t) {
  struct tm d;
  if (!t || !localTm(t, d)) return "--:--";
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", d.tm_hour, d.tm_min);
  return buf;
}

String untilText(time_t t) {
  long diff = (long)(t - nowEpoch());
  if (diff <= 30) return "now";
  long mins = (diff + 59) / 60;
  char buf[24];
  if (mins < 60) snprintf(buf, sizeof(buf), "in %ldm", mins);
  else if (mins < 24 * 60) snprintf(buf, sizeof(buf), "in %ldh %02ldm", mins / 60, mins % 60);
  else snprintf(buf, sizeof(buf), "in %ldd %ldh", mins / 1440, (mins % 1440) / 60);
  return buf;
}

String daysText(uint8_t days) {
  days &= 0x7F;
  if (days == 0) return "Once";
  if (days == 0x7F) return "Every day";
  if (days == 0x3E) return "Mon-Fri";
  if (days == 0x41) return "Sat Sun";
  // Monday first, one letter per day, "-" for days off: "MTW-F--"
  const char* letters = "MTWTFSS";
  String s;
  for (int i = 0; i < 7; i++) {
    int wday = (i + 1) % 7;  // tm_wday: 0 = Sunday
    s += (days >> wday) & 1 ? letters[i] : '-';
  }
  return s;
}

const char* reminderName(int k) { return k >= 0 && k < REMINDER_COUNT ? REM_NAMES[k] : ""; }
const char* reminderPrompt(int k) { return k >= 0 && k < REMINDER_COUNT ? REM_PROMPTS[k] : ""; }

void drawReminderIcon(int k, int cx, int cy) {
  U8G2& u = g();
  switch (k) {
    case REM_WATER:  // drop
      u.drawDisc(cx, cy + 3, 5);
      u.drawTriangle(cx - 5, cy + 2, cx + 5, cy + 2, cx, cy - 8);
      break;
    case REM_STRETCH:  // figure with arms up
      u.drawDisc(cx, cy - 6, 2);
      u.drawLine(cx, cy - 3, cx, cy + 3);
      u.drawLine(cx, cy - 2, cx - 5, cy - 8);
      u.drawLine(cx, cy - 2, cx + 5, cy - 8);
      u.drawLine(cx, cy + 3, cx - 4, cy + 8);
      u.drawLine(cx, cy + 3, cx + 4, cy + 8);
      break;
    case REM_POSTURE:  // sitting up straight on a chair
      u.drawDisc(cx - 1, cy - 7, 2);
      u.drawLine(cx - 1, cy - 4, cx - 1, cy + 2);
      u.drawLine(cx - 1, cy + 2, cx + 5, cy + 2);
      u.drawLine(cx + 5, cy + 2, cx + 5, cy + 8);
      u.drawLine(cx - 5, cy - 4, cx - 5, cy + 8);  // chair back
      u.drawLine(cx - 5, cy + 4, cx + 3, cy + 4);  // seat
      break;
  }
}

// ------------------------------------------------------------------
// Schedule
// ------------------------------------------------------------------

void Schedule::begin() {
  timeBegin();
  if (timeValid()) {
    Serial.printf("[time] kept through sleep: %s\n", hhmm(nowEpoch()).c_str());
    time_t a = nextAlarm();
    if (a) Serial.printf("[alarm] next %s (%s)\n", hhmm(a).c_str(), untilText(a).c_str());
  } else {
    Serial.println("[time] unknown until the first sync (needs WiFi)");
  }
}

time_t Schedule::occurrenceAfter(const AlarmSlot& a, time_t t) const {
  struct tm d;
  localTm(t, d);
  for (int i = 0; i <= 7; i++) {
    struct tm c = d;
    c.tm_mday += i;
    c.tm_hour = a.hour;
    c.tm_min = a.minute;
    c.tm_sec = 0;
    c.tm_isdst = -1;
    time_t e = mktime(&c);  // normalizes the date and sets tm_wday
    if (e <= t) continue;
    if (a.days == 0 || ((a.days >> c.tm_wday) & 1)) return e;
  }
  return 0;
}

time_t Schedule::nextAlarm(int* slot) const {
  if (!timeValid()) return 0;
  time_t now = nowEpoch(), best = 0;
  int bestSlot = -1;
  if (rtcSnoozeAt > now) {
    best = rtcSnoozeAt;
    bestSlot = rtcSnoozeSlot;
  }
  if (settings.alarmsOn()) {
    for (int i = 0; i < MAX_ALARMS; i++) {
      const AlarmSlot& a = settings.alarm(i);
      if (!a.on) continue;
      time_t e = occurrenceAfter(a, now);
      if (e && (!best || e < best)) {
        best = e;
        bestSlot = i;
      }
    }
  }
  if (slot) *slot = bestSlot;
  return best;
}

time_t Schedule::snoozeUntil() const { return rtcSnoozeAt > nowEpoch() ? rtcSnoozeAt : 0; }

void Schedule::snooze(int slot) {
  rtcSnoozeAt = nowEpoch() + SNOOZE_MIN * 60;
  rtcSnoozeSlot = (int8_t)slot;
  Serial.printf("[alarm] snoozed until %s\n", hhmm(rtcSnoozeAt).c_str());
}

void Schedule::cancelSnooze() {
  rtcSnoozeAt = 0;
  rtcSnoozeSlot = -1;
}

bool Schedule::alarmSoon(uint32_t withinS) const {
  time_t a = nextAlarm();
  return a && a - nowEpoch() <= (time_t)withinS;
}

time_t Schedule::sleepWakeAt() const {
  time_t a = nextAlarm();
  return a ? a - ALARM_PREWAKE_S : 0;
}

void Schedule::loop(bool offFace, bool busy) {
  uint32_t ms = millis();
  if (ms - lastLoop_ < 250) return;
  lastLoop_ = ms;
  if (!timeValid()) return;
  time_t now = nowEpoch();
  if (!wasValid_) {
    wasValid_ = true;
    if (rtcLastCheck == 0) rtcLastCheck = now;  // first time known: nothing is overdue
  }
  checkAlarms(now);
  checkReminders(now, offFace, busy);
}

void Schedule::checkAlarms(time_t now) {
  time_t from = rtcLastCheck;
  // Clock set back (re-sync), or never checked: start from now.
  if (from == 0 || from > now) from = now;
  // Alarms missed by more than ALARM_LATE_MAX_S (cube unplugged) don't ring.
  if (now - from > ALARM_LATE_MAX_S) from = now - ALARM_LATE_MAX_S;
  rtcLastCheck = now;

  int due = -2;  // -2: nothing due
  if (rtcSnoozeAt && rtcSnoozeAt <= now) {
    if (rtcSnoozeAt > from) due = rtcSnoozeSlot;
    rtcSnoozeAt = 0;
    rtcSnoozeSlot = -1;
  }
  if (settings.alarmsOn()) {
    for (int i = 0; i < MAX_ALARMS; i++) {
      AlarmSlot a = settings.alarm(i);
      if (!a.on) continue;
      time_t e = occurrenceAfter(a, from);
      if (!e || e > now) continue;
      due = i;
      if (a.days == 0) {  // a one-time alarm switches itself off once it rings
        a.on = 0;
        settings.setAlarm(i, a);
      }
    }
  }
  if (due == -2) return;
  if (alerts.kind() == Alerts::Kind::Alarm) return;  // already ringing
  Serial.printf("[alarm] alarm %d at %s\n", due + 1, hhmm(now).c_str());
  alerts.startAlarm(due);
}

bool Schedule::inActiveHours(time_t t) const {
  struct tm d;
  localTm(t, d);
  int from = settings.remindFrom(), to = settings.remindTo(), h = d.tm_hour;
  if (from == to) return true;  // all day
  return from < to ? (h >= from && h < to) : (h >= from || h < to);
}

void Schedule::checkReminders(time_t now, bool offFace, bool busy) {
  for (int k = 0; k < REMINDER_COUNT; k++) {
    const ReminderSlot& r = settings.reminder(k);
    time_t every = (time_t)r.everyMin * 60;
    if (!settings.remindersOn() || !r.on) {
      rtcRemNext[k] = 0;
      continue;
    }
    if (rtcRemNext[k] == 0) {
      rtcRemNext[k] = now + every;
      continue;
    }
    if (now < rtcRemNext[k]) continue;
    // Due. Skip it (start the next interval) if it is old, or the cube is off,
    // or outside active hours. Wait while a mode is busy or another alert shows.
    bool stale = now - rtcRemNext[k] > REMINDER_STALE_S;
    if (stale || offFace || !inActiveHours(now)) {
      rtcRemNext[k] = now + every;
      continue;
    }
    if (busy || alerts.active()) continue;
    rtcRemNext[k] = now + every;
    Serial.printf("[remind] %s\n", reminderName(k));
    alerts.startReminder(k);
    return;  // one at a time; the others wait for this one to end
  }
}

time_t Schedule::nextReminder(int k) const {
  if (k < 0 || k >= REMINDER_COUNT) return 0;
  return rtcRemNext[k];
}

uint8_t Schedule::count(int k) const {
  if (!timeValid()) return 0;
  return settings.healthCount(k, localDay(nowEpoch()));
}

void Schedule::reminderDone(int k, bool acknowledged) {
  if (k < 0 || k >= REMINDER_COUNT) return;
  time_t now = nowEpoch();
  // The next interval counts from when it was answered.
  if (settings.reminder(k).on) rtcRemNext[k] = now + (time_t)settings.reminder(k).everyMin * 60;
  if (acknowledged && timeValid()) settings.addHealthCount(k, localDay(now));
  Serial.printf("[remind] %s %s\n", reminderName(k), acknowledged ? "done" : "skipped");
}

void Schedule::remindersChanged() {
  for (int k = 0; k < REMINDER_COUNT; k++) rtcRemNext[k] = 0;
}

// ------------------------------------------------------------------
// Alerts
// ------------------------------------------------------------------

void Alerts::begin(Kind k) {
  if (kind_ == Kind::None) started_ = true;
  kind_ = k;
  startedAt_ = lastChime_ = millis();
  // A finger already on the strip (e.g. talking to key buddy) must not snooze or
  // stop the alert: wait until it is lifted.
  waitRelease_ = touch.isTouched();
  touch.consumeTap();
  dirty_ = true;
  display.on();
}

void Alerts::startAlarm(int slot) {
  if (kind_ == Kind::Alarm) return;
  slot_ = slot;
  if (kind_ == Kind::Reminder) schedule.reminderDone(slot_, false);  // replaced
  begin(Kind::Alarm);
  speaker.play(Sound::Alarm, true);
}

void Alerts::startReminder(int k) {
  if (kind_ == Kind::Alarm) return;
  slot_ = k;
  begin(Kind::Reminder);
  speaker.play(Sound::Chime);
}

void Alerts::startNotice(const String& title, const String& body, Sound sound) {
  if (kind_ == Kind::Alarm || kind_ == Kind::Reminder) return;
  title_ = title;
  body_ = body;
  begin(Kind::Notice);
  speaker.play(sound);
}

void Alerts::end() {
  speaker.stop();
  kind_ = Kind::None;
  ended_ = true;
}

void Alerts::stop() {
  if (kind_ == Kind::None) return;
  if (kind_ == Kind::Reminder) schedule.reminderDone(slot_, false);
  end();
}

bool Alerts::consumeStarted() {
  bool s = started_;
  started_ = false;
  return s;
}

bool Alerts::consumeEnded() {
  bool e = ended_ && kind_ == Kind::None;
  if (e) ended_ = false;
  return e;
}

void Alerts::loop() {
  if (kind_ == Kind::None) return;
  uint32_t now = millis();

  bool tap = false, hold = false;
  if (waitRelease_) {
    if (!touch.isTouched()) waitRelease_ = false;
    touch.consumeTap();
    touch.consumeHold();
  } else {
    hold = touch.consumeHold();
    tap = touch.consumeTap();
  }

  switch (kind_) {
    case Kind::Alarm:
      if (tap) {
        schedule.snooze(slot_);
        end();
        return;
      }
      if (hold || now - startedAt_ >= ALARM_RING_MAX_MS) {
        Serial.println(hold ? "[alarm] stopped" : "[alarm] rang for 10 min, stopped");
        end();
        return;
      }
      break;

    case Kind::Reminder:
      if (tap || hold || now - startedAt_ >= REMINDER_SHOW_MS) {
        schedule.reminderDone(slot_, tap);
        end();
        if (tap) speaker.play(Sound::Tap);
        return;
      }
      if (now - lastChime_ >= REMINDER_REPEAT_MS) {
        lastChime_ = now;
        speaker.play(Sound::Chime);
      }
      break;

    case Kind::Notice:
      if (tap || hold || now - startedAt_ >= NOTICE_SHOW_MS) {
        end();
        return;
      }
      break;

    default:
      break;
  }

  // Alarm flashes every 500 ms; a finger on the strip shows hold progress.
  uint32_t every = kind_ == Kind::Alarm ? 250 : 1000;
  if (touch.isTouched() && !waitRelease_) every = 100;
  if (dirty_ || now - lastDraw_ >= every) {
    dirty_ = false;
    lastDraw_ = now;
    draw(now);
  }
}

void Alerts::draw(uint32_t now) {
  U8G2& u = g();
  int w = u.getDisplayWidth(), h = u.getDisplayHeight();
  bool portrait = h > w;
  bool holding = touch.isTouched() && !waitRelease_;
  uint32_t held = holding ? touch.heldMs() : 0;
  u.clearBuffer();
  u.setFontMode(1);  // transparent text, so it also draws on the inverted (flashing) screen

  bool invert = kind_ == Kind::Alarm && !holding && ((now - startedAt_) / 500) % 2 == 1;
  if (invert) {
    u.drawBox(0, 0, w, h);
    u.setDrawColor(0);
  }

  switch (kind_) {
    case Kind::Alarm: {
      u.setFont(FONT_TITLE);
      display.drawCentered("ALARM", 13);
      String t = hhmm(nowEpoch());
      if (portrait) {
        display.drawBigText(t.c_str(), w / 2, 44, w - 2, 28);
        u.setFont(FONT_SMALL);
        if (slot_ >= 0) {
          const AlarmSlot& a = settings.alarm(slot_);
          char buf[16];
          snprintf(buf, sizeof(buf), "%02d:%02d", a.hour, a.minute);
          display.drawCentered(buf, 72);
        } else {
          display.drawCentered("Test alarm", 72);
        }
        display.drawCentered("Tap:", 94);
        display.drawCentered("snooze 5 min", 103);
        display.drawCentered("Hold: stop", 116);
      } else {
        display.drawBigText(t.c_str(), w / 2, 33, w - 4, 24);
        u.setFont(FONT_SMALL);
        u.drawStr(0, 62, "Tap: snooze");
        u.drawStr(w - u.getStrWidth("Hold: stop"), 62, "Hold: stop");
      }
      break;
    }

    case Kind::Reminder: {
      const char* prompt = reminderPrompt(slot_);
      if (portrait) {
        drawReminderIcon(slot_, w / 2, 16);
        u.setFont(FONT_TITLE);
        display.drawWrapped(prompt, 0, 46, w, 14, 4);
        u.setFont(FONT_SMALL);
        display.drawCentered("Tap: done", 108);
        display.drawCentered("Hold: skip", 120);
      } else {
        drawReminderIcon(slot_, 12, 14);
        u.setFont(FONT_SMALL);
        String t = hhmm(nowEpoch());
        u.drawStr(w - u.getStrWidth(t.c_str()), 8, t.c_str());
        u.setFont(FONT_TITLE);
        display.drawWrapped(prompt, 28, 20, w - 28, 14, 2);
        u.setFont(FONT_SMALL);
        u.drawStr(0, 62, "Tap: done");
        u.drawStr(w - u.getStrWidth("Hold: skip"), 62, "Hold: skip");
      }
      break;
    }

    case Kind::Notice: {
      u.setFont(FONT_TITLE);
      display.drawWrapped(title_, 0, 14, w, 14, 2);
      u.setFont(FONT_BODY);
      display.drawWrapped(body_, 0, portrait ? 52 : 34, w, 11, portrait ? 5 : 2);
      u.setFont(FONT_SMALL);
      display.drawCentered("Tap: OK", h - 2);
      break;
    }

    default:
      break;
  }

  // Hold progress along the top edge.
  if (holding && held > 200) {
    uint32_t p = held >= TOUCH_HOLD_MS ? w : held * w / TOUCH_HOLD_MS;
    u.drawBox(0, 0, p, 2);
  }
  u.setDrawColor(1);
  u.setFontMode(0);
  u.sendBuffer();
}
