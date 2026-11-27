#include "webconfig.h"

#include <ESPmDNS.h>
#include <WebServer.h>

#include "alarms.h"
#include "config.h"
#include "net.h"
#include "radio.h"
#include "settings.h"
#include "speaker.h"
#include "wifi_setup.h"

WebConfig webConfig;

namespace {
WebServer web(80);  // the setup portal's server is stopped by the time this starts

const char* const DAY_SHORT[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
const int DAY_ORDER[7] = {1, 2, 3, 4, 5, 6, 0};  // Monday first on the page

const char* EXTRA_STYLE = R"HTML(<style>
.days{display:flex;gap:4px;flex-wrap:wrap;margin-top:6px}
.days label,.chk{display:inline-flex;align-items:center;gap:4px;margin:0;font-size:14px;color:inherit}
.days input,.chk input{width:auto}
.alarm{border-top:1px solid #8883;padding-top:10px;margin-top:10px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
button.alt{background:#52525b}
.st{border-top:1px solid #8883;padding-top:8px;margin-top:8px}
.st input{margin-top:4px}
.sb{display:flex;gap:6px;margin-top:6px}
.sb button{width:auto;margin:0;padding:6px 12px;font-size:14px;background:#52525b}
.sb button.play{background:#16a34a;margin-left:auto}
</style>
<script>
function mv(b,d){var r=b.closest('.st'),p=r.parentNode;
if(d<0&&r.previousElementSibling)p.insertBefore(r,r.previousElementSibling);
if(d>0&&r.nextElementSibling)p.insertBefore(r.nextElementSibling,r);}
function del(b){b.closest('.st').remove();}
function add(){var l=document.getElementById('st');
if(l.children.length>=%MAX%){alert('Up to %MAX% stations');return;}
l.appendChild(document.getElementById('tpl').content.cloneNode(true));}
</script>)HTML";

// One station row. index >= 0: a saved station (gets a Play button).
String stationRow(const String& name, const String& url, int index) {
  String h = "<div class='st'><input name='sn' maxlength='" + String(RADIO_NAME_MAX) +
             "' placeholder='Name' value='" + name + "'>";
  h += "<input name='su' type='url' maxlength='" + String(RADIO_URL_MAX) +
       "' placeholder='http://... stream URL' value='" + url + "'>";
  h += "<div class='sb'><button type='button' onclick='mv(this,-1)'>&uarr;</button>"
       "<button type='button' onclick='mv(this,1)'>&darr;</button>"
       "<button type='button' onclick='del(this)'>Delete</button>";
  if (index >= 0)
    h += "<button class='play' formaction='/radio/play?i=" + String(index) + "'>Play</button>";
  return h + "</div></div>";
}

String esc(const String& in) {
  String out;
  for (char c : in) {
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

String checked(bool on) { return on ? " checked" : ""; }

int argInt(const char* name, int def, int lo, int hi) {
  if (!web.hasArg(name)) return def;
  int v = web.arg(name).toInt();
  return v < lo ? lo : v > hi ? hi : v;
}

void redirectHome(const char* msg) {
  web.sendHeader("Location", String("/?m=") + msg);
  web.send(303, "text/plain", "");
}

String pageHtml() {
  String h;
  h.reserve(16000);
  h += pageHead("Key Cube settings");
  String style = EXTRA_STYLE;
  style.replace("%MAX%", String(RADIO_MAX_STATIONS));
  h += style;
  h += "<h1>Key Cube</h1>";
  String m = web.arg("m");
  if (m == "saved") h += "<div class='card'>Saved.</div>";
  else if (m == "test") h += "<div class='card'>Playing a test sound.</div>";
  else if (m == "ring") h += "<div class='card'>Test alarm ringing. Tap the cube to snooze, hold to stop.</div>";
  else if (m == "radio") h += "<div class='card'>Radio saved.</div>";
  else if (m == "rreset") h += "<div class='card'>Radio stations reset to the default list.</div>";
  else if (m == "rplay") h += "<div class='card'>" + String(radioMode.active() ? "Playing on the cube." : "Selected. It plays when the cube lies on its back.") + "</div>";
  else if (m == "busy") h += "<div class='card'>The radio is playing: no test sound. Turn the cube to another side first.</div>";

  // Clock.
  h += "<div class='card'><h2>Clock</h2>";
  if (timeValid()) {
    struct tm d;
    localTm(nowEpoch(), d);
    char buf[40];
    strftime(buf, sizeof(buf), "%A %d %B, %H:%M", &d);
    h += String("<div>") + buf + "</div>";
    int slot;
    time_t next = schedule.nextAlarm(&slot);
    h += "<div class='small'>Next alarm: " +
         (next ? hhmm(next) + " (" + untilText(next) + ")" : String("none")) + "</div>";
  } else {
    h += "<div>Time not synced yet.</div>";
  }
  h += "<div class='small'>Timezone " + esc(settings.timezone()) + " (change it on the setup page)</div></div>";

  h += "<form method='post' action='/save'>";

  // Alarms.
  h += "<div class='card'><h2>Alarms</h2>";
  h += "<label class='chk'><input type='checkbox' name='aon'" + checked(settings.alarmsOn()) +
       "> Alarms on (hold the strip in Alarm mode to switch)</label>";
  for (int i = 0; i < MAX_ALARMS; i++) {
    const AlarmSlot& a = settings.alarm(i);
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", a.hour, a.minute);
    String n = String(i);
    h += "<div class='alarm'><label class='chk'><input type='checkbox' name='a" + n + "on'" +
         checked(a.on) + "> Alarm " + String(i + 1) + "</label>";
    h += "<input type='time' name='a" + n + "t' value='" + t + "'>";
    h += "<div class='days'>";
    for (int j = 0; j < 7; j++) {
      int wd = DAY_ORDER[j];
      h += "<label><input type='checkbox' name='a" + n + "d" + String(wd) + "'" +
           checked((a.days >> wd) & 1) + ">" + DAY_SHORT[wd] + "</label>";
    }
    h += "</div><div class='small'>No days ticked: rings once.</div></div>";
  }
  h += "</div>";

  // Reminders.
  h += "<div class='card'><h2>Health reminders</h2>";
  h += "<label class='chk'><input type='checkbox' name='ron'" + checked(settings.remindersOn()) +
       "> Reminders on (hold the strip in Health mode to switch)</label>";
  for (int k = 0; k < REMINDER_COUNT; k++) {
    const ReminderSlot& r = settings.reminder(k);
    String n = String(k);
    h += "<div class='alarm'><label class='chk'><input type='checkbox' name='r" + n + "on'" +
         checked(r.on) + "> " + reminderName(k) + ": " + reminderPrompt(k) + "</label>";
    h += "<label>Every (minutes)</label><input type='number' min='5' max='480' name='r" + n +
         "m' value='" + String(r.everyMin) + "'></div>";
  }
  h += "<div class='grid'><div><label>From (hour)</label><input type='number' min='0' max='23' "
       "name='rfrom' value='" + String(settings.remindFrom()) + "'></div>";
  h += "<div><label>To (hour)</label><input type='number' min='0' max='23' name='rto' value='" +
       String(settings.remindTo()) + "'></div></div>";
  h += "<div class='small'>Reminders only fire between these hours, and not while the cube "
       "lies on its screen (off). Same hour twice: all day.</div></div>";

  // Focus timer.
  h += "<div class='card'><h2>Focus timer</h2><div class='grid'>";
  h += "<div><label>Focus (min)</label><input type='number' min='1' max='120' name='fw' value='" +
       String(settings.focusWorkMin()) + "'></div>";
  h += "<div><label>Break (min)</label><input type='number' min='1' max='60' name='fb' value='" +
       String(settings.focusBreakMin()) + "'></div>";
  h += "<div><label>Long break (min)</label><input type='number' min='1' max='60' name='fl' value='" +
       String(settings.focusLongMin()) + "'></div></div>";
  h += "<div class='small'>A long break follows every " + String(FOCUS_LONG_EVERY) +
       " focus periods.</div></div>";

  // Sound.
  h += "<div class='card'><h2>Sound</h2><label>Volume <span id='vv'>" + String(settings.volume()) +
       "</span></label><input type='range' min='0' max='100' name='vol' value='" +
       String(settings.volume()) + "' oninput='vv.textContent=this.value'><div class='small'>Alarms ring at least at " +
       String(SPK_ALARM_MIN_VOLUME) + ", even when this is lower.</div></div>";

  h += "<button type='submit'>Save</button></form>";
  h += "<form method='post' action='/test'><button class='alt'>Play test sound</button></form>";
  h += "<form method='post' action='/ring'><button class='alt'>Ring a test alarm</button></form>";

  // Radio (its own form: the station list can be long).
  h += "<div class='card'><h2>Radio</h2><div class='small'>Plays when the cube lies on its "
       "back (speaker down, screen up). Tap: next station. Hold: pause.</div>";
  h += "<form method='post' action='/radio'><div id='st'>";
  const auto& list = settings.stations();
  for (size_t i = 0; i < list.size(); i++) h += stationRow(esc(list[i].name), esc(list[i].url), i);
  h += "</div><template id='tpl'>" + stationRow("", "", -1) + "</template>";
  h += "<button type='button' class='alt' onclick='add()'>Add station</button>";
  h += "<div class='small'>Up to " + String(RADIO_MAX_STATIONS) +
       " stations. MP3 and AAC streams (http:// is lighter than https://). "
       "Play uses the saved list.</div>";
  h += "<label>Radio volume <span id='rv'>" + String(settings.radioVolume()) +
       "</span></label><input type='range' min='0' max='100' name='rvol' value='" +
       String(settings.radioVolume()) + "' oninput='rv.textContent=this.value'>";
  h += "<label>Sleep timer</label><select name='rsleep'>";
  static const uint8_t SLEEP_OPTIONS[] = {0, 15, 30, 45, 60, 90, 120};
  for (uint8_t m : SLEEP_OPTIONS) {
    h += "<option value='" + String(m) + "'" + (settings.radioSleepMin() == m ? " selected" : "") +
         ">" + (m ? String(m) + " min" : String("Off")) + "</option>";
  }
  h += "</select><div class='small'>Stops the radio this long after it starts and shows the "
       "clock. A touch plays again.</div>";
  h += "<label class='chk'><input type='checkbox' name='rrem'" + checked(settings.radioReminders()) +
       "> Health reminders interrupt the radio</label>";
  h += "<button type='submit'>Save radio</button></form>";
  h += "<form method='post' action='/radio/reset' onsubmit=\"return confirm('Replace the "
       "station list with the default stations?')\"><button class='alt'>Reset to the default "
       "stations</button></form></div>";
  h += "<p class='small'>" + esc(settings.apName()) + " · firmware " FW_VERSION "</p></body></html>";
  return h;
}

void handleSave() {
  settings.setAlarmsOn(web.hasArg("aon"));
  for (int i = 0; i < MAX_ALARMS; i++) {
    String n = String(i);
    AlarmSlot a = settings.alarm(i);
    a.on = web.hasArg("a" + n + "on");
    String t = web.arg("a" + n + "t");  // "HH:MM"
    if (t.length() >= 5 && t[2] == ':') {
      a.hour = t.substring(0, 2).toInt();
      a.minute = t.substring(3, 5).toInt();
    }
    a.days = 0;
    for (int wd = 0; wd < 7; wd++)
      if (web.hasArg("a" + n + "d" + String(wd))) a.days |= 1 << wd;
    settings.setAlarm(i, a);
  }

  bool remChanged = settings.remindersOn() != web.hasArg("ron");
  settings.setRemindersOn(web.hasArg("ron"));
  for (int k = 0; k < REMINDER_COUNT; k++) {
    String n = String(k);
    ReminderSlot r = settings.reminder(k);
    ReminderSlot v = {(uint8_t)(web.hasArg("r" + n + "on") ? 1 : 0),
                      (uint16_t)argInt(("r" + n + "m").c_str(), r.everyMin, 5, 480)};
    if (v.on != r.on || v.everyMin != r.everyMin) remChanged = true;
    settings.setReminder(k, v);
  }
  settings.setRemindHours(argInt("rfrom", settings.remindFrom(), 0, 23),
                          argInt("rto", settings.remindTo(), 0, 23));
  if (remChanged) schedule.remindersChanged();

  settings.setFocus(argInt("fw", settings.focusWorkMin(), 1, 120),
                    argInt("fb", settings.focusBreakMin(), 1, 60),
                    argInt("fl", settings.focusLongMin(), 1, 60));
  settings.setVolume(argInt("vol", settings.volume(), 0, 100));
  Serial.println("[web] settings saved");
  redirectHome("saved");
}

void handleRadioSave() {
  // Rows arrive as sn/su pairs in page order (moved and deleted on the page).
  std::vector<RadioStation> list;
  String name;
  for (int i = 0; i < web.args(); i++) {
    if (web.argName(i) == "sn") name = web.arg(i);
    else if (web.argName(i) == "su") {
      if (!web.arg(i).isEmpty()) list.push_back({name, web.arg(i)});
      name = "";
    }
  }
  settings.setStations(list);
  settings.setRadioVolume(argInt("rvol", settings.radioVolume(), 0, 100));
  settings.setRadioSleepMin(argInt("rsleep", settings.radioSleepMin(), 0, 240));
  settings.setRadioReminders(web.hasArg("rrem"));
  radioMode.stationsChanged();
  Serial.printf("[web] radio saved: %u stations\n", (unsigned)settings.stations().size());
  redirectHome("radio");
}
}  // namespace

void WebConfig::begin() {
  web.on("/", HTTP_GET, []() { web.send(200, "text/html", pageHtml()); });
  web.on("/save", HTTP_POST, handleSave);
  web.on("/test", HTTP_POST, []() {
    if (speaker.released()) {  // the radio has the speaker
      redirectHome("busy");
      return;
    }
    speaker.play(Sound::Chime);
    redirectHome("test");
  });
  web.on("/radio", HTTP_POST, handleRadioSave);
  web.on("/radio/play", HTTP_POST, []() {
    radioMode.playIndex(argInt("i", -1, -1, RADIO_MAX_STATIONS));
    redirectHome("rplay");
  });
  web.on("/radio/reset", HTTP_POST, []() {
    settings.resetStations();
    radioMode.stationsChanged();
    redirectHome("rreset");
  });
  web.on("/ring", HTTP_POST, []() {
    alerts.startAlarm(-1);  // modes.cpp sees the alert start on its next loop
    redirectHome("ring");
  });
  web.onNotFound([]() { web.send(404, "text/plain", "Not found"); });
  web.begin();
  started_ = true;
}

void WebConfig::loop() {
  bool up = net.connected();
  if (up && !wasConnected_) {
    if (!started_) begin();
    // (Re)announce keycube.local on the network just joined.
    MDNS.end();
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[web] settings page: http://%s.local/ or http://%s/\n", MDNS_NAME,
                    net.ip().c_str());
    } else {
      Serial.println("[web] mDNS failed: use the IP address");
    }
  }
  wasConnected_ = up;
  if (started_) web.handleClient();
}
