#include "wifi_setup.h"

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>
#include <climits>
#include <vector>

#include "config.h"
#include "display.h"
#include "settings.h"

SetupPortal portal;

namespace {
WebServer server(80);
DNSServer dns;
bool routesAdded = false;

struct ScanEntry {
  String ssid;
  int rssi;
  bool open;
};
std::vector<ScanEntry> scanResults;

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

const char* PAGE_HEAD_START = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>)HTML";
const char* PAGE_HEAD_STYLE = R"HTML(</title><style>
body{font-family:-apple-system,system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#f4f4f5;color:#18181b}
h1{font-size:22px;margin:4px 0 12px}h2{font-size:16px;margin:20px 0 8px}
.card{background:#fff;border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 1px 2px #0001}
label{display:block;font-size:13px;color:#52525b;margin:10px 0 4px}
input,select{width:100%;box-sizing:border-box;font-size:16px;padding:10px;border:1px solid #d4d4d8;border-radius:8px;background:#fff}
button{width:100%;font-size:16px;padding:12px;margin-top:16px;border:0;border-radius:8px;background:#2563eb;color:#fff}
.small{font-size:12px;color:#71717a}.row{display:flex;justify-content:space-between;align-items:center;padding:6px 0}
.row form{margin:0}.row button{width:auto;margin:0;padding:6px 10px;font-size:13px;background:#ef4444}
a{color:#2563eb}
@media(prefers-color-scheme:dark){body{background:#18181b;color:#f4f4f5}.card{background:#27272a}input,select{background:#18181b;color:#f4f4f5;border-color:#3f3f46}label{color:#a1a1aa}}
</style></head><body>)HTML";

const String PAGE_HEAD = pageHead("Key Cube setup");

int signalBars(int rssi) { return rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1; }
}  // namespace

String pageHead(const char* title) {
  return String(PAGE_HEAD_START) + title + PAGE_HEAD_STYLE;
}

// ------------------------------------------------------------------
// Connect to saved networks
// ------------------------------------------------------------------

bool connectToSavedNetworks() {
  const std::vector<SavedNetwork> nets = settings.networks();  // copy: markUsed edits the list
  if (nets.empty()) return false;

  WiFi.persistent(false);  // we store credentials ourselves
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  display.showConnecting("(scanning)");

  int found = WiFi.scanNetworks();
  struct Candidate {
    size_t idx;
    int rssi;
  };
  std::vector<Candidate> visible, hidden;
  for (size_t i = 0; i < nets.size(); i++) {
    int best = INT_MIN;
    for (int j = 0; j < found; j++)
      if (WiFi.SSID(j) == nets[i].ssid) best = std::max(best, (int)WiFi.RSSI(j));
    (best == INT_MIN ? hidden : visible).push_back({i, best});
  }
  WiFi.scanDelete();
  std::sort(visible.begin(), visible.end(),
            [](const Candidate& a, const Candidate& b) { return a.rssi > b.rssi; });
  // Saved networks not seen in the scan may be hidden SSIDs: try them last.
  visible.insert(visible.end(), hidden.begin(), hidden.end());

  uint32_t deadline = millis() + WIFI_TOTAL_TIMEOUT_MS;
  for (const auto& c : visible) {
    if ((int32_t)(millis() - deadline) >= 0) break;
    const SavedNetwork& n = nets[c.idx];
    Serial.printf("[wifi] trying \"%s\" (rssi %d)\n", n.ssid.c_str(),
                  c.rssi == INT_MIN ? 0 : c.rssi);
    WiFi.begin(n.ssid.c_str(), n.pass.c_str());
    uint32_t t0 = millis();
    while (millis() - t0 < WIFI_PER_NET_TIMEOUT_MS && (int32_t)(millis() - deadline) < 0) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] connected to \"%s\", IP %s\n", n.ssid.c_str(),
                      WiFi.localIP().toString().c_str());
        settings.markUsed(n.ssid);
        return true;
      }
      display.showConnecting(n.ssid);
      delay(150);
    }
    WiFi.disconnect();
  }
  Serial.println("[wifi] no saved network connected");
  return false;
}

// ------------------------------------------------------------------
// Setup portal
// ------------------------------------------------------------------

void SetupPortal::start() {
  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);  // AP for the phone, STA to test the chosen network
  IPAddress ip;
  ip.fromString(AP_IP_STR);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(settings.apName().c_str(), settings.apPass().c_str());
  delay(100);

  // Answer every DNS name with our IP, so phones detect a captive portal.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", ip);

  scan();
  if (!routesAdded) {
    setupRoutes();
    routesAdded = true;
  }
  server.begin();

  active_ = true;
  everHadPhone_ = false;
  startedAt_ = millis();
  attempt_ = Attempt::None;
  Serial.printf("[setup] hotspot \"%s\" at http://%s/\n", settings.apName().c_str(), AP_IP_STR);
}

void SetupPortal::stop() {
  if (!active_) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);  // AP off, station connection stays up
  active_ = false;
  Serial.println("[setup] hotspot stopped");
}

bool SetupPortal::phoneConnected() const { return active_ && WiFi.softAPgetStationNum() > 0; }

void SetupPortal::setAttempt(Attempt a) {
  attempt_ = a;
  attemptChanged_ = true;
  attemptChangedAt_ = millis();
}

bool SetupPortal::consumeAttemptChange() {
  bool c = attemptChanged_;
  attemptChanged_ = false;
  return c;
}

void SetupPortal::clearAttempt() { setAttempt(Attempt::None); }

void SetupPortal::loop() {
  if (!active_) return;
  dns.processNextRequest();
  server.handleClient();
  if (phoneConnected()) everHadPhone_ = true;

  if (attempt_ == Attempt::Testing) {
    wl_status_t s = WiFi.status();
    uint32_t age = millis() - tryStart_;
    if (s == WL_CONNECTED) {
      settings.addNetwork(trySsid_, tryPass_);  // save only after it works
      tryPass_ = "";
      Serial.printf("[setup] joined \"%s\", saved\n", trySsid_.c_str());
      setAttempt(Attempt::Connected);
    } else if (age > SETUP_CONNECT_TIMEOUT_MS ||
               (age > 2000 && (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL))) {
      WiFi.disconnect();  // station only; the hotspot stays up
      tryPass_ = "";
      Serial.printf("[setup] could not join \"%s\" (status %d)\n", trySsid_.c_str(), (int)s);
      setAttempt(Attempt::Failed);
    }
  }
}

void SetupPortal::scan() {
  scanResults.clear();
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;  // hidden network: user can type its name
    bool dup = false;
    for (auto& e : scanResults) {
      if (e.ssid == ssid) {
        e.rssi = std::max(e.rssi, (int)WiFi.RSSI(i));
        dup = true;
      }
    }
    if (!dup) scanResults.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
  }
  WiFi.scanDelete();
  std::sort(scanResults.begin(), scanResults.end(),
            [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
  if (scanResults.size() > 20) scanResults.resize(20);
  Serial.printf("[setup] scan found %u networks\n", (unsigned)scanResults.size());
}

String SetupPortal::pageHtml() {
  String h;
  h.reserve(6000);
  h += PAGE_HEAD;
  h += "<h1>Key Cube setup</h1>";
  h += "<form method='post' action='/save'><div class='card'><h2>WiFi network</h2>";
  h += "<label>Network</label><select name='ssid'><option value=''>Choose a network</option>";
  for (const auto& e : scanResults) {
    String esc = htmlEscape(e.ssid);
    h += "<option value='" + esc + "'>" + esc + "  " + String("▂▄▆█").substring(0, signalBars(e.rssi) * 3) +
         (e.open ? " (open)" : "") + "</option>";
  }
  h += "</select><div class='small'><a href='/rescan'>Scan again</a> · 2.4 GHz networks only</div>";
  h += "<label>Or type a network name (hidden network)</label>";
  h += "<input name='ssid_other' maxlength='32' autocapitalize='none' autocorrect='off'>";
  h += "<label>Password</label><input id='pw' name='pass' type='password' maxlength='63'>";
  h += "<div class='small'><label style='display:inline'><input type='checkbox' style='width:auto' "
       "onclick=\"pw.type=this.checked?'text':'password'\"> Show password</label></div></div>";

  h += "<div class='card'><h2>Server</h2>";
  h += "<label>Server URL</label><input name='server' type='url' placeholder='https://your.domain.com' "
       "autocapitalize='none' value='" + htmlEscape(settings.serverUrl()) + "'>";
  h += "<label>API key</label><input name='apikey' type='password' autocomplete='off' placeholder='";
  h += settings.apiKey().isEmpty() ? "Required" : "Saved. Leave blank to keep it";
  h += "'>";
  h += "<label>Device ID</label><input name='devid' maxlength='64' value='" +
       htmlEscape(settings.deviceId()) + "'>";
  h += "<label>Timezone (POSIX)</label><input name='tz' value='" + htmlEscape(settings.timezone()) + "'>";
  h += "<div class='small'>Sri Lanka: &lt;+0530&gt;-5:30</div></div>";
  h += "<button type='submit'>Save and connect</button></form>";

  const auto& nets = settings.networks();
  h += "<div class='card'><h2>Saved networks (" + String(nets.size()) + "/" + String(MAX_NETWORKS) + ")</h2>";
  if (nets.empty()) h += "<div class='small'>None yet.</div>";
  for (size_t i = 0; i < nets.size(); i++) {
    h += "<div class='row'><span>" + htmlEscape(nets[i].ssid) +
         "</span><form method='post' action='/forget'><input type='hidden' name='i' value='" +
         String(i) + "'><button>Forget</button></form></div>";
  }
  h += "</div>";
  h += "<p class='small'>" + htmlEscape(settings.apName()) + " · firmware " FW_VERSION "</p>";
  h += "</body></html>";
  return h;
}

void SetupPortal::setupRoutes() {
  server.on("/", HTTP_GET, [this]() { server.send(200, "text/html", pageHtml()); });

  server.on("/rescan", HTTP_GET, [this]() {
    if (attempt_ != Attempt::Testing) scan();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/forget", HTTP_POST, []() {
    settings.forgetNetwork(server.arg("i").toInt());
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });

  server.on("/save", HTTP_POST, [this]() {
    // Server settings are saved right away; blank API key keeps the old one.
    if (!server.arg("server").isEmpty()) settings.setServerUrl(server.arg("server"));
    if (!server.arg("apikey").isEmpty()) settings.setApiKey(server.arg("apikey"));
    String devid = server.arg("devid");
    devid.trim();
    if (!devid.isEmpty()) settings.setDeviceId(devid);
    String tz = server.arg("tz");
    tz.trim();
    if (!tz.isEmpty()) settings.setTimezone(tz);

    String ssid = server.arg("ssid_other");
    ssid.trim();
    if (ssid.isEmpty()) ssid = server.arg("ssid");
    String pass = server.arg("pass");

    String err;
    if (ssid.isEmpty()) err = "Settings saved. Now choose a WiFi network to finish.";
    else if (ssid.length() > 32) err = "Network name is too long.";
    else if (!pass.isEmpty() && (pass.length() < 8 || pass.length() > 63))
      err = "WiFi passwords are 8 to 63 characters.";
    else if (attempt_ == Attempt::Testing) err = "Already testing a network. Wait a moment.";
    if (!err.isEmpty()) {
      server.send(200, "text/html",
                  PAGE_HEAD + "<h1>Key Cube setup</h1><div class='card'><p>" + err +
                      "</p><a href='/'>Back</a></div></body></html>");
      return;
    }

    trySsid_ = ssid;
    tryPass_ = pass;
    tryStart_ = millis();
    WiFi.disconnect();
    WiFi.begin(trySsid_.c_str(), tryPass_.c_str());
    setAttempt(Attempt::Testing);
    Serial.printf("[setup] testing \"%s\"\n", trySsid_.c_str());

    String h = PAGE_HEAD;
    h += "<h1>Connecting</h1><div class='card'><p id='m'>Testing " + htmlEscape(ssid) +
         "...</p><p class='small'>The cube shows the result on its screen too.</p></div>";
    h += R"HTML(<script>
const m=document.getElementById('m');
function poll(){fetch('/status').then(r=>r.json()).then(s=>{
 if(s.state==='testing'){setTimeout(poll,1500);return;}
 if(s.state==='connected'){m.textContent='Connected to '+s.ssid+'. Setup is done: the cube will turn off its setup WiFi now. You can close this page.';return;}
 m.textContent='Could not join '+s.ssid+'. Check the password and try again. ';
 const a=document.createElement('a');a.href='/';a.textContent='Back';m.appendChild(a);
}).catch(()=>{m.textContent='Lost contact with the cube. If its screen says Connected, setup is done. Otherwise rejoin the cube WiFi and try again.';});}
setTimeout(poll,1500);
</script></body></html>)HTML";
    server.send(200, "text/html", h);
  });


  server.on("/status", HTTP_GET, [this]() {
    JsonDocument doc;
    const char* st = attempt_ == Attempt::Testing     ? "testing"
                     : attempt_ == Attempt::Connected ? "connected"
                     : attempt_ == Attempt::Failed    ? "failed"
                                                      : "idle";
    doc["state"] = st;
    doc["ssid"] = trySsid_;
    if (attempt_ == Attempt::Connected) doc["ip"] = WiFi.localIP().toString();
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // Anything else (captive-portal probes from iOS, Android, Windows): send to the setup page.
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" AP_IP_STR "/");
    server.send(302, "text/plain", "");
  });
}
