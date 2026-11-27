#include "clockmodes.h"

#include "alarms.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "speaker.h"
#include "touch.h"

AlarmMode alarmMode;
HealthMode healthMode;

namespace {
U8G2& g() { return display.gfx(); }
const uint8_t* FONT_SMALL = u8g2_font_5x8_tf;
const uint8_t* FONT_BODY = u8g2_font_6x10_tf;
const uint8_t* FONT_TITLE = u8g2_font_helvB10_tr;
const uint8_t* FONT_TINY = u8g2_font_4x6_tf;

constexpr uint32_t LIST_SHOW_MS = 10000;  // alarm list, then back to the clock

String slotTime(const AlarmSlot& a) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", a.hour, a.minute);
  return buf;
}

// Redraw once a second (the clock), on a change, or while a finger is down
// (hold progress bar).
bool needDraw(bool& dirty, uint32_t& lastDraw) {
  uint32_t now = millis();
  uint32_t every = touch.isTouched() ? 100 : 1000;
  if (!dirty && now - lastDraw < every) return false;
  dirty = false;
  lastDraw = now;
  return true;
}

// Small text that falls back to the tiny font when it doesn't fit maxW.
// centre: x is the centre, otherwise the left edge.
void drawFit(const String& text, int x, int y, int maxW, bool centre) {
  U8G2& u = g();
  u.setFont(FONT_SMALL);
  if (u.getStrWidth(text.c_str()) > maxW) u.setFont(FONT_TINY);
  int tw = u.getStrWidth(text.c_str());
  u.drawStr(centre ? x - tw / 2 : x, y, text.c_str());
}

void drawHoldBar() {
  if (!touch.isTouched() || touch.heldMs() < 200) return;
  int w = g().getDisplayWidth();
  uint32_t held = touch.heldMs();
  g().drawBox(0, 0, held >= TOUCH_HOLD_MS ? w : held * w / TOUCH_HOLD_MS, 2);
}
}  // namespace

void showSettingsStatus() {
  String line = net.healthPending()  ? String("Checking server...")
                : net.healthKnown() ? net.health().message
                                    : net.shortStatus();
  if (net.connected()) line = "Settings: " MDNS_NAME ".local. " + line;
  display.showStatus(net.ssid(), net.ip(), line, net.connected() && net.health().ok);
}

// ------------------------------------------------------------------
// Mode B: alarm
// ------------------------------------------------------------------

void AlarmMode::enter() {
  page_ = Page::Main;
  dirty_ = true;
  netVersion_ = net.version();
  touch.consumeTap();
}

void AlarmMode::loop() {
  uint32_t now = millis();
  if (touch.consumeHold()) {
    if (schedule.snoozeUntil()) {
      schedule.cancelSnooze();
      Serial.println("[alarm] snooze cancelled");
    } else {
      settings.setAlarmsOn(!settings.alarmsOn());
      Serial.printf("[alarm] all alarms %s\n", settings.alarmsOn() ? "on" : "off");
    }
    speaker.play(Sound::Tap);
    page_ = Page::Main;
    dirty_ = true;
  }
  if (touch.consumeTap()) {
    page_ = page_ == Page::Main ? Page::List : page_ == Page::List ? Page::Status : Page::Main;
    if (page_ == Page::Status) net.requestHealthCheck();
    pageAt_ = now;
    dirty_ = true;
  }
  uint32_t showFor = page_ == Page::List ? LIST_SHOW_MS : STATUS_OVERLAY_MS;
  if (page_ != Page::Main && now - pageAt_ >= showFor) {
    page_ = Page::Main;
    dirty_ = true;
  }
  if (net.version() != netVersion_) {
    netVersion_ = net.version();
    dirty_ = true;
  }
  if (needDraw(dirty_, lastDraw_)) draw();
}

void AlarmMode::draw() {
  if (page_ == Page::Status) {
    showSettingsStatus();
    return;
  }
  U8G2& u = g();
  int w = u.getDisplayWidth(), h = u.getDisplayHeight();
  bool portrait = h > w;
  u.clearBuffer();

  if (page_ == Page::List) {
    u.setFont(FONT_BODY);
    u.drawStr(0, 10, "Alarms");
    u.setFont(FONT_SMALL);
    const char* master = settings.alarmsOn() ? "on" : "all off";
    u.drawStr(w - u.getStrWidth(master), 9, master);
    u.drawHLine(0, 13, w);
    for (int i = 0; i < MAX_ALARMS; i++) {
      const AlarmSlot& a = settings.alarm(i);
      String t = slotTime(a), d = a.on ? daysText(a.days) : String("off");
      if (portrait) {  // two lines per alarm
        int y = 26 + i * 25;
        u.setFont(FONT_BODY);
        u.drawStr(0, y, t.c_str());
        if (a.on) u.drawDisc(w - 4, y - 4, 3);
        else u.drawCircle(w - 4, y - 4, 3);
        u.setFont(FONT_SMALL);
        u.drawStr(0, y + 10, d.c_str());
      } else {
        int y = 24 + i * 11;
        if (a.on) u.drawDisc(3, y - 3, 3);
        else u.drawCircle(3, y - 3, 3);
        u.setFont(FONT_BODY);
        u.drawStr(10, y, t.c_str());
        u.setFont(FONT_SMALL);
        u.drawStr(46, y, d.c_str());
      }
    }
    u.sendBuffer();
    return;
  }

  // Main page: clock and next alarm.
  time_t now = nowEpoch();
  String clock = hhmm(now);
  int slot = -1;
  time_t next = schedule.nextAlarm(&slot);
  time_t snooze = schedule.snoozeUntil();

  String line1, line2, line3;  // big, small, small
  if (!timeValid()) {
    line1 = "--:--";
    line2 = "No time yet";
    line3 = net.connected() ? "Syncing..." : "Needs WiFi";
  } else if (snooze && next == snooze) {
    line1 = hhmm(snooze);
    line2 = "Snoozed";
    line3 = "Hold: cancel";
  } else if (!settings.alarmsOn()) {
    line1 = "Off";
    line2 = "Alarms off";
    line3 = "Hold: on";
  } else if (next) {
    line1 = hhmm(next);
    line2 = slot >= 0 ? daysText(settings.alarm(slot).days) : String("");
    line3 = untilText(next);
  } else {
    line1 = "None";
    line2 = "Set them at";
    line3 = MDNS_NAME ".local";
  }

  if (portrait) {
    u.setFont(FONT_BODY);
    display.drawCentered("Alarm", 10);
    display.drawBigText(clock.c_str(), w / 2, 32, w - 2, 24);
    u.drawHLine(0, 50, w);
    u.setFont(FONT_SMALL);
    display.drawCentered("Next alarm", 62);
    u.setFont(FONT_TITLE);
    display.drawCentered(line1.c_str(), 80);
    drawFit(line2, w / 2, 93, w, true);
    drawFit(line3, w / 2, 103, w, true);
    u.setFont(FONT_TINY);
    display.drawCentered("Tap: list", h - 1);
  } else {
    // Left: clock. Right column (63 px, 12 characters): the next alarm.
    display.drawBigText(clock.c_str(), 30, 28, 58, 30);
    u.drawVLine(61, 4, 46);
    u.setFont(FONT_SMALL);
    u.drawStr(65, 9, "Next alarm");
    u.setFont(FONT_TITLE);
    u.drawStr(65, 26, line1.c_str());
    drawFit(line2, 65, 37, w - 65, false);
    drawFit(line3, 65, 47, w - 65, false);
    u.setFont(FONT_TINY);
    u.drawStr(0, h - 1, "Tap: list");
    const char* holdHint = snooze ? "Hold: cancel snooze" : "Hold: on/off";
    u.drawStr(w - u.getStrWidth(holdHint), h - 1, holdHint);
  }
  drawHoldBar();
  u.sendBuffer();
}

// ------------------------------------------------------------------
// Mode C: health reminders
// ------------------------------------------------------------------

void HealthMode::enter() {
  page_ = Page::Main;
  dirty_ = true;
  netVersion_ = net.version();
  touch.consumeTap();
}

void HealthMode::loop() {
  uint32_t now = millis();
  if (touch.consumeHold()) {
    settings.setRemindersOn(!settings.remindersOn());
    schedule.remindersChanged();
    Serial.printf("[remind] reminders %s\n", settings.remindersOn() ? "on" : "off");
    speaker.play(Sound::Tap);
    page_ = Page::Main;
    dirty_ = true;
  }
  if (touch.consumeTap()) {
    page_ = page_ == Page::Main ? Page::Status : Page::Main;
    if (page_ == Page::Status) net.requestHealthCheck();
    pageAt_ = now;
    dirty_ = true;
  }
  if (page_ == Page::Status && now - pageAt_ >= STATUS_OVERLAY_MS) {
    page_ = Page::Main;
    dirty_ = true;
  }
  if (net.version() != netVersion_) {
    netVersion_ = net.version();
    dirty_ = true;
  }
  if (needDraw(dirty_, lastDraw_)) draw();
}

void HealthMode::draw() {
  if (page_ == Page::Status) {
    showSettingsStatus();
    return;
  }
  U8G2& u = g();
  int w = u.getDisplayWidth(), h = u.getDisplayHeight();
  bool portrait = h > w;
  time_t now = nowEpoch();
  u.clearBuffer();

  u.setFont(FONT_BODY);
  u.drawStr(0, 10, "Health");
  u.setFont(FONT_SMALL);
  String clock = hhmm(now);
  u.drawStr(w - u.getStrWidth(clock.c_str()), 9, clock.c_str());
  u.drawHLine(0, 13, w);

  // Footer: why reminders are quiet, or the active hours.
  char hours[24];
  snprintf(hours, sizeof(hours), "%02d:00-%02d:00", settings.remindFrom(), settings.remindTo());
  String footer;
  if (!settings.remindersOn()) footer = "Paused. Hold: on";
  else if (!timeValid()) footer = "No time yet";
  else if (!schedule.inActiveHours(now)) footer = String("Quiet. On ") + hours;
  else footer = String("On ") + hours;

  for (int k = 0; k < REMINDER_COUNT; k++) {
    const ReminderSlot& r = settings.reminder(k);
    String done = String(schedule.count(k)) + "x";
    time_t next = schedule.nextReminder(k);
    String when = !r.on || !settings.remindersOn() ? String("off")
                  : next && timeValid()           ? untilText(next)
                                                  : String("--");
    if (portrait) {  // icon left; name, done today, next on three lines
      int y = 20 + k * 30;
      drawReminderIcon(k, 8, y + 6);
      u.setFont(FONT_BODY);
      u.drawStr(19, y + 4, reminderName(k));
      u.setFont(FONT_SMALL);
      u.drawStr(19, y + 14, ("Today: " + String(schedule.count(k))).c_str());
      u.drawStr(19, y + 23, when.c_str());
    } else {  // one row each: icon, name, done today, next
      int y = 25 + k * 13;
      drawReminderIcon(k, 7, y - 4);
      u.setFont(FONT_BODY);
      u.drawStr(17, y, reminderName(k));
      u.setFont(FONT_SMALL);
      u.drawStr(64, y, done.c_str());
      u.drawStr(w - u.getStrWidth(when.c_str()), y, when.c_str());
    }
  }
  u.setFont(FONT_TINY);
  if (portrait) {
    u.drawHLine(0, 112, w);
    display.drawWrapped(footer, 0, 120, w, 7, 2);
  } else {
    u.drawStr(0, h - 1, footer.c_str());
  }
  drawHoldBar();
  u.sendBuffer();
}
