/*
  ARIA Hub Firmware v6
  Hardware: ESP32 DevKit C v4

  Pins:
    GPIO 5  -> WS2812B DIN, 30 pixels
    GPIO 16 -> hub buzzer
    GPIO 34 -> single IR motion sensor, assigned by SETIR command

  Primary purpose: exit routing + congestion detection
  Fan relay and ventilation removed — handled at node level only

  Required libraries:
    WiFi.h, esp_now.h, AsyncTCP.h, ESPAsyncWebServer.h, FastLED.h
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#define WIFI_CHANNEL      11
#define AP_SSID           "ARIA-HUB"
#define AP_PASSWORD       "aria1234"
#define DNS_PORT          53
// STA_SSID left empty — hub stays AP-only on channel 1 matching nodes
// To enable Telegram: fill in STA_SSID, STA_PASSWORD, and ensure
// your router is set to channel 1 to avoid ESP-NOW channel conflict
#define STA_SSID          "Eileen"
#define STA_PASSWORD      "eileenyeoh"
#define TELEGRAM_BOT_TOKEN "8594989645:AAGVPETGPWmitPrbUhuToOWrzLNpp5Byswo"
#define TELEGRAM_CHAT_ID   "1487637387"

#define PIN_LED_STRIP     5
#define PIN_BUZZER        16
#define PIN_IR_MOTION     34

#define NUM_LEDS          30
#define ZONE_LEN          10

#define SCORE_AMBER       3
#define SCORE_FIRE        5
#define SCORE_CRITICAL    8

#define NODE_TIMEOUT_MS   5000
#define ANIM_INTERVAL_MS  160
#define WS_PUSH_MS        1000
#define STATUS_MS         3000
#define ALERT_NOTIFY_COOLDOWN_MS 60000


#define CONGEST_CONT_MS   2000
#define CONGEST_CLEAR_MS  3000
#define WINDOW_MS         10000
#define RATE_WINDOW_MS    60000
#define CONGEST_EVENT_THRESHOLD 4
#define CONGEST_BLOCK_HOLD_MS   8000
#define GAS_BEEP_ON_MS    200
#define GAS_BEEP_OFF_MS   300
#define SOS_BEEP_ON_MS    150
#define SOS_BEEP_OFF_MS   150

typedef struct {
  uint8_t nodeID;
  int     mq2Smooth;
  int     mq7Raw;
  int     coPpm;
  int     fireScore;
  bool    flameConfirmed;
  bool    gasLeakMode;
  bool    sosActive;
  float   tempC;
  float   humidity;
} NodePacket;

typedef struct {
  uint8_t magic;
  uint8_t targetNodeID;
  char command[32];
} HubCommandPacket;

struct NodeState {
  int fireScore;
  int coPpm;
  int mq2Smooth;
  int mq7Raw;
  bool flameConfirmed;
  bool gasLeakMode;
  bool sosActive;
  float tempC;
  float humidity;
  unsigned long lastSeen;
  bool forced;
};

struct ExitState {
  uint8_t pin;
  bool motion;
  bool congested;
  bool forced;
  unsigned long motionStart;
  unsigned long clearStart;
  unsigned long windowStart;
  unsigned long rateStart;
  unsigned long motionMsInWindow;
  uint16_t eventsInRateWindow;
  unsigned long congestedHoldUntil;
  bool lastMotion;
  unsigned long lastMotionSample;
};

enum Direction {
  DIR_LEFT,
  DIR_RIGHT,
  DIR_SPLIT,
  DIR_PULSE,
  DIR_OFF
};

enum ZoneColor {
  COLOR_OFF,
  COLOR_GREEN,
  COLOR_RED,
  COLOR_ORANGE,
  COLOR_YELLOW
};

struct ZoneRender {
  ZoneColor color;
  Direction direction;
};

enum IrTarget {
  IR_TARGET_A = 0,
  IR_TARGET_B = 1,
  IR_TARGET_C = 2,
  IR_TARGET_ALL = 3,
  IR_TARGET_UNSET = 255
};

NodeState nodes[7];
ExitState exits[3] = {
  {PIN_IR_MOTION, false, false, false, 0, 0, 0, 0, 0, 0, 0, false, 0},
  {PIN_IR_MOTION, false, false, false, 0, 0, 0, 0, 0, 0, 0, false, 0},
  {PIN_IR_MOTION, false, false, false, 0, 0, 0, 0, 0, 0, 0, false, 0}
};

CRGB leds[NUM_LEDS];
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint8_t chaseStep = 0;
uint8_t pendingNodeMap = 0;
IrTarget irTarget = IR_TARGET_UNSET;
unsigned long lastAnimTime = 0;
unsigned long lastWsPush = 0;
unsigned long lastStatusPrint = 0;
unsigned long buzzerStepStart = 0;
bool buzzerOn = false;
int lastAlertTierSent = 0;
unsigned long lastAlertNotifyTime = 0;

String logLines[12];
uint8_t logIndex = 0;

void setupEspNow();
void sendNodeCommand(uint8_t targetNodeID, const String &command);
const char *espNowErrName(int code);
void setupDashboard();
void updateExitCongestion(unsigned long now);
void updateExitFromMotion(uint8_t index, bool motionNow, unsigned long now);
void updateOutputs();
void updateHubBuzzer(unsigned long now);
void updateAlertNotifications(unsigned long now);
void sendTelegramAlert(const String &text);
void renderLedRouting(unsigned long now);
void computeZoneScores(int &scoreA, int &scoreB, int &scoreC);
void computeZoneRender(ZoneRender render[3]);
void drawZone(uint8_t zone, ZoneRender zr);
void drawChaseRange(uint8_t start, uint8_t end, Direction dir, CRGB color);
void drawSplitZoneB(CRGB color);
void drawPulseZone(uint8_t zone, CRGB color);
CRGB colorFor(ZoneColor c);
bool exitCongestedForZone(uint8_t zone);
void handleSerialCommands();
void runCommand(String cmd, bool fromWeb);
void handleSetIr(String target);
void printStatus();
void clearAllState();
void pushDashboard();
String dashboardJson();
String htmlPage() {
  return R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ARIA Hub</title>
  <style>
    :root{
      --bg:#f4f6f9;--white:#ffffff;--border:#e0e4ea;
      --ink:#181c22;--muted:#5a6475;
      --blue:#0058be;--blue-lt:#ddeaff;
      --red:#c0001a;--red-lt:#ffe0e3;
      --green:#006944;--green-lt:#d4f4e6;
      --orange:#d97706;--orange-lt:#fff3cd;
      --yellow:#ca9500;--yellow-lt:#fff9c4;
    }
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:"Segoe UI",Arial,sans-serif;background:var(--bg);color:var(--ink);min-height:100vh}

    /* â”€â”€ HEADER â”€â”€ */
    .hdr{display:flex;align-items:center;gap:12px;padding:0 20px;height:62px;
      background:var(--white);border-bottom:1px solid var(--border);
      position:sticky;top:0;z-index:100;box-shadow:0 1px 4px rgba(0,0,0,.07)}
    .hdr-logo{width:42px;height:42px;object-fit:contain}
    .hdr-brand{font-size:19px;font-weight:900;letter-spacing:-.02em;flex:1;color:var(--ink)}
    .tabs{display:flex;gap:4px}
    .tab{padding:8px 18px;border-radius:8px;border:1px solid var(--border);
      background:var(--white);font-weight:700;cursor:pointer;font-size:13px;color:var(--muted)}
    .tab.active{background:var(--blue);color:#fff;border-color:var(--blue)}

    /* â”€â”€ PAGE LAYOUT â”€â”€ */
    .page{display:none;padding:20px;max-width:1200px;margin:0 auto}
    .page.active{display:block}

    /* â”€â”€ CARDS â”€â”€ */
    .card{background:var(--white);border:1px solid var(--border);border-radius:12px;
      padding:16px;margin-bottom:14px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
    .card-title{font-size:15px;font-weight:700;margin-bottom:12px;color:var(--ink)}

    /* â”€â”€ ZONE CARDS â”€â”€ */
    .grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:14px}
    .zcard{background:var(--white);border:1px solid var(--border);border-radius:12px;
      padding:14px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
    .zcard-top{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:8px}
    .zicon{width:38px;height:38px;border-radius:9px;display:flex;align-items:center;
      justify-content:center;font-size:19px;flex-shrink:0}
    .zbadge{padding:3px 9px;border-radius:999px;font-size:10px;font-weight:700;text-transform:uppercase}
    .zbadge.safe{background:var(--green-lt);color:var(--green)}
    .zbadge.warning{background:var(--orange-lt);color:var(--orange)}
    .zbadge.fire{background:var(--red-lt);color:var(--red)}
    .zbadge.critical{background:var(--red);color:#fff}
    .zname{font-size:13px;font-weight:600;color:var(--muted);margin-bottom:3px}
    .zscore{font-size:28px;font-weight:900;line-height:1}
    .zscore-sub{font-size:13px;font-weight:600;color:#bbb}
    .safe{color:var(--green)}.warning{color:var(--orange)}.fire{color:var(--red)}.critical{color:#7a0010}

    /* â”€â”€ NODE CARDS â”€â”€ */
    .node-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:14px}
    .ncard{background:var(--white);border:1px solid var(--border);border-radius:10px;overflow:hidden}
    .ncard-hdr{display:flex;justify-content:space-between;align-items:center;
      padding:8px 12px;border-bottom:1px solid var(--border);font-size:13px;font-weight:700}
    .ncard-hdr.live{background:#f0fdf8}.ncard-hdr.forced{background:#fffbeb}.ncard-hdr.offline{background:#fff1f2}
    .nstatus{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.04em}
    .nstatus.live{color:var(--green)}.nstatus.forced{color:var(--orange)}.nstatus.offline{color:var(--red)}
    .ncard-body{display:grid;grid-template-columns:1fr 1fr;gap:5px;padding:10px 12px;font-size:11px}
    .nrow{display:flex;flex-direction:column;gap:1px}
    .nlabel{color:var(--muted);text-transform:uppercase;font-size:9px;font-weight:700;letter-spacing:.05em}
    .nval{font-weight:700;font-size:13px;color:var(--ink)}
    .nval.danger{color:var(--red)}.nval.warn{color:var(--orange)}.nval.ok{color:var(--green)}
    .ndivider{grid-column:1/-1;height:1px;background:var(--border);margin:2px 0}

    /* â”€â”€ ALERT PILLS â”€â”€ */
    .pills{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
    .pill{padding:6px 14px;border-radius:999px;background:var(--bg);border:1px solid var(--border);
      font-weight:700;font-size:12px;color:var(--muted)}
    .pill.on.F{background:var(--red-lt);color:var(--red);border-color:var(--red)}
    .pill.on.G{background:var(--yellow-lt);color:var(--yellow);border-color:var(--yellow)}
    .pill.on.S{background:#dbeafe;color:#1e40af;border-color:#93c5fd}

    /* â”€â”€ FLOOR MAP â”€â”€ */
    .floor-wrap{position:relative;background:#fafbfc;border:1px solid var(--border);
      border-radius:10px;padding:10px;height:220px;margin-bottom:10px}
    .fstrip{position:absolute;border-radius:8px;border:2px solid var(--border);
      display:flex;align-items:center;justify-content:center;font-weight:700;font-size:12px}
    .fza{left:10px;top:20px;width:33%;height:50px}
    .fzb{left:38%;top:90px;width:24%;height:100px}
    .fzc{right:10px;top:20px;width:33%;height:50px}
    .fex{position:absolute;font-size:11px;font-weight:700;color:var(--muted)}
    .fexa{left:10px;top:4px}.fexb{left:42%;bottom:6px}.fexc{right:10px;top:4px}

    /* â”€â”€ ESCAPE CANVAS â”€â”€ */
    #escCanvas{width:100%;height:200px;border-radius:10px;border:1px solid var(--border);
      background:#0a1628;display:block}

    /* â”€â”€ ROUTE TEXT â”€â”€ */
    .routerow{padding:8px 12px;border-radius:8px;margin-bottom:6px;
      border:1px solid var(--border);background:var(--white);font-size:13px}

    /* â”€â”€ TERMINAL (system log) â”€â”€ */
    .terminal{background:#0f1117;border-radius:10px;font-family:monospace;font-size:11px;
      padding:12px;height:220px;overflow-y:auto;color:#94a3b8}
    .terminal div{padding:1px 0;border-bottom:1px solid rgba(255,255,255,.03)}

    /* â”€â”€ CHAT BOX â”€â”€ */
    .chatbox{background:var(--white);border:1px solid var(--border);border-radius:10px;
      font-size:12px;padding:10px 12px;height:160px;overflow-y:auto}
    .chatbox .chat{background:#dbeafe;border-radius:6px;padding:3px 8px;
      margin-bottom:3px;color:#1e3a8a;font-weight:600}

    /* â”€â”€ INPUTS â”€â”€ */
    .inrow{display:flex;gap:8px;margin-top:8px}
    input[type=text]{flex:1;padding:9px 12px;border-radius:8px;border:1px solid var(--border);
      background:var(--white);color:var(--ink);font-size:13px;outline:none}
    input[type=text]:focus{border-color:var(--blue);box-shadow:0 0 0 2px rgba(0,88,190,.15)}
    button{padding:9px 16px;border:0;border-radius:8px;background:var(--blue);
      color:#fff;font-weight:700;cursor:pointer;font-size:13px}
    button:hover{opacity:.88}
    select{padding:8px 12px;border-radius:8px;border:1px solid var(--border);
      font-size:13px;color:var(--ink);background:var(--white)}

    /* â”€â”€ CONN BAR â”€â”€ */
    .connbar{display:grid;grid-template-columns:repeat(6,1fr);gap:6px}
    .cbadge{display:block;text-align:center;padding:7px 4px;border-radius:8px;
      background:var(--bg);font-size:12px;border:1px solid var(--border);font-weight:700}
    .cbadge.live{background:var(--green-lt);color:var(--green)}
    .cbadge.forced{background:var(--orange-lt);color:var(--orange)}
    .cbadge.off{background:var(--red-lt);color:var(--red)}

    /* â”€â”€ DEMO BUTTONS â”€â”€ */
    .demobar{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px}
    .dbtn{padding:8px 14px;border:1px solid var(--border);border-radius:8px;
      background:var(--white);cursor:pointer;font-size:13px;font-weight:600;color:var(--ink)}
    .dbtn:hover{background:var(--bg)}
    .dbtn.red{background:var(--red-lt);color:var(--red);border-color:rgba(192,0,26,.2)}
    .dbtn.clr{background:var(--green-lt);color:var(--green);border-color:rgba(0,105,68,.2)}

    /* â”€â”€ LEGEND â”€â”€ */
    .legend{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:14px}
    .lg{padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700}
    .lgN{background:var(--green-lt);color:var(--green)}
    .lgE{background:var(--red-lt);color:var(--red)}
    .lgO{background:var(--orange-lt);color:var(--orange)}
    .lgY{background:var(--yellow-lt);color:var(--yellow)}

    /* â”€â”€ SOS CARD â”€â”€ */
    #sosCard{border:2px solid #93c5fd}

    /* â”€â”€ RESPONSIVE â”€â”€ */
    @media(max-width:700px){.grid3,.node-grid{grid-template-columns:1fr 1fr}.connbar{grid-template-columns:repeat(3,1fr)}}
    @media(max-width:480px){.grid3{grid-template-columns:1fr}.tabs{gap:2px}.tab{padding:6px 10px;font-size:12px}}
  </style>
</head>
<body>

<div class="hdr">
  <img class="hdr-logo" src="data:image/png;base64,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" alt="ARIA">
  <span class="hdr-brand">ARIA Hub</span>
  <div class="tabs">
    <div class="tab active" onclick="show('warden')">Warden</div>
    <div class="tab" onclick="show('tech')">Technical</div>
    <div class="tab" onclick="show('demo')">Demo / Test</div>
  </div>
</div>

<!-- â•â•â• WARDEN PAGE â•â•â• -->
<div class="page active" id="page-warden">

  <div class="legend">
    <span class="lg lgN">&#9679; Safe route</span>
    <span class="lg lgE">&#9679; Fire &mdash; route away</span>
    <span class="lg lgO">&#9679; Congested exit</span>
    <span class="lg lgY">&#9679; Gas leak</span>
  </div>

  <div class="grid3" id="wZones"></div>

  <div class="card">
    <div class="pills" id="wAlerts"></div>
    <div style="font-size:12px;color:var(--muted)">IR sensor assigned: <b id="irLbl">not set</b></div>
  </div>

  <div class="card" id="sosCard">
    <div class="card-title">SOS Status</div>
    <b id="sosHead" style="font-size:15px">No SOS active</b>
    <div style="font-size:13px;color:var(--muted);margin-top:5px">Location: <span id="sosLoc">none</span></div>
  </div>

  <div class="card">
    <div class="card-title">Live Floor Map</div>
    <div class="floor-wrap">
      <div class="fex fexa" id="fExA">Exit A</div>
      <div class="fex fexb" id="fExB">Exit B</div>
      <div class="fex fexc" id="fExC">Exit C</div>
      <div class="fstrip fza" id="fZA">Zone A</div>
      <div class="fstrip fzb" id="fZB">Zone B</div>
      <div class="fstrip fzc" id="fZC">Zone C</div>
    </div>
    <div id="routeText"></div>
  </div>

  <div class="card">
    <div class="card-title">Live Escape Route</div>
    <canvas id="escCanvas"></canvas>
  </div>

  <div class="card">
    <div class="card-title">Warden Chat</div>
    <div class="chatbox" id="chatBox"></div>
    <div class="inrow">
      <input type="text" id="chatIn" placeholder="Message wardens...">
      <button onclick="sendChat()">Send</button>
    </div>
  </div>
</div>

<!-- â•â•â• TECHNICAL PAGE â•â•â• -->
<div class="page" id="page-tech">

  <div class="grid3" id="tZones"></div>

  <div class="card">
    <div class="card-title">Node Connections</div>
    <div id="connSummary" style="font-size:13px;color:var(--muted);margin-bottom:8px"></div>
    <div class="connbar" id="connBar"></div>
  </div>

  <div class="card">
    <div class="card-title">Node Detail</div>
    <div class="node-grid" id="nodeCards"></div>
  </div>

  <div class="card">
    <div class="card-title">System Log</div>
    <div class="terminal" id="sysLog"></div>
  </div>
</div>

<!-- â•â•â• DEMO / TEST PAGE â•â•â• -->
<div class="page" id="page-demo">

  <div class="card">
    <div class="card-title">Hub Node Mapping</div>
    <p style="font-size:13px;color:var(--muted);margin-bottom:10px">
      Map the next incoming unassigned packet to a specific node ID.
      Nodes default to Node&nbsp;1 until reassigned via serial or this control.
    </p>
    <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
      <select id="nodeMapSel">
        <option value="0">-- clear mapping --</option>
        <option value="1">Node 1 &mdash; Zone A / Exit A</option>
        <option value="2">Node 2 &mdash; Zone B / Exit B</option>
        <option value="3">Node 3 &mdash; Zone B / Exit B</option>
        <option value="4">Node 4 &mdash; Zone C / Exit C</option>
        <option value="5">Node 5 &mdash; Zone A / Exit A</option>
        <option value="6">Node 6 &mdash; Zone C / Exit C</option>
      </select>
      <button onclick="applyNodeMap()">Apply Mapping</button>
      <span id="mapResult" style="font-size:13px;color:var(--green)"></span>
    </div>
    <div style="margin-top:8px;font-size:13px;color:var(--muted)">Pending map: <b id="pendingMap">none</b></div>
  </div>

  <div class="card">
    <div class="card-title">Fire Scenarios</div>
    <div class="demobar">
      <button class="dbtn red" onclick="sc('NODE 1 7')">Node 1 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 2 7')">Node 2 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 3 7')">Node 3 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 4 7')">Node 4 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 5 7')">Node 5 Fire</button>
      <button class="dbtn red" onclick="sc('NODE 6 7')">Node 6 Fire</button>
    </div>
    <div class="demobar">
      <button class="dbtn red" onclick="sc('NODE 1 9')">Node 1 Critical</button>
      <button class="dbtn red" onclick="sc('NODE 4 9')">Node 4 Critical</button>
      <button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 4 7'),200)">Zone A+C Fire</button>
      <button class="dbtn" onclick="sc('NODE 1 7');setTimeout(()=>sc('NODE 2 7'),200);setTimeout(()=>sc('NODE 4 7'),400)">All Zones Fire</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Exit Congestion</div>
    <div class="demobar">
      <button class="dbtn" onclick="sc('CONGEST A')">Congest A</button>
      <button class="dbtn" onclick="sc('CONGEST B')">Congest B</button>
      <button class="dbtn" onclick="sc('CONGEST C')">Congest C</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST A')">Clear A</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST B')">Clear B</button>
      <button class="dbtn clr" onclick="sc('CLEAR CONGEST C')">Clear C</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Gas / SOS</div>
    <div class="demobar">
      <button class="dbtn" onclick="sc('GAS 1 ON')">Gas Node 1</button>
      <button class="dbtn clr" onclick="sc('GAS 1 OFF')">Gas Node 1 Off</button>
      <button class="dbtn" onclick="sc('SOS 1 ON')">SOS Node 1</button>
      <button class="dbtn clr" onclick="sc('SOS 1 OFF')">SOS Node 1 Off</button>
    </div>
  </div>

  <div class="card">
    <div class="card-title">Custom Command</div>
    <p style="font-size:12px;color:var(--muted);margin-bottom:8px">
      NODE &lt;id&gt; &lt;score&gt; | SETSCORE &lt;id&gt; &lt;0-10&gt; | GAS &lt;id&gt; ON/OFF |
      SOS &lt;id&gt; ON/OFF | FLAME &lt;id&gt; ON/OFF | NODE &lt;id&gt; CLEAR |
      NODECMD &lt;id&gt; &lt;cmd&gt; | SETIR A/B/C/ALL | CLEAR | STATUS
    </p>
    <div class="inrow">
      <input type="text" id="demoCmd" placeholder="e.g. SETSCORE 2 6">
      <button onclick="sendDemoCmd()">Send</button>
    </div>
    <div style="margin-top:8px">
      <button class="dbtn clr" onclick="sc('CLEAR')">Clear All State</button>
    </div>
  </div>
</div>

<script>
let ws, lastState = null;

function connect(){
  ws = new WebSocket('ws://'+location.host+'/ws');
  ws.onmessage = e => { lastState = JSON.parse(e.data); render(lastState); };
  ws.onclose   = () => setTimeout(connect, 1500);
}
function send(msg){ if(ws && ws.readyState===1) ws.send(msg); }
function sc(cmd){ send(cmd); }
function sendChat(){
  let v = document.getElementById('chatIn').value.trim();
  if(v){ send(v); document.getElementById('chatIn').value=''; }
}
function sendDemoCmd(){
  let v = document.getElementById('demoCmd').value.trim();
  if(v){ send(v); document.getElementById('demoCmd').value=''; }
}
function applyNodeMap(){
  let v = document.getElementById('nodeMapSel').value;
  send('SETNODE ' + v);
  let r = document.getElementById('mapResult');
  r.textContent = v === '0' ? 'Mapping cleared' : 'Hub will map next packet to Node ' + v;
  setTimeout(()=>{ r.textContent=''; }, 4000);
}

function show(name){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  document.querySelectorAll('.tab').forEach(t => {
    if(t.textContent.trim().toLowerCase().startsWith(name.substring(0,4)))
      t.classList.add('active');
  });
}
document.addEventListener('keydown', e => {
  if(e.key === 'Enter'){
    if(document.activeElement.id === 'chatIn') sendChat();
    if(document.activeElement.id === 'demoCmd') sendDemoCmd();
  }
});

function cls(s){
  s = (s||'').toLowerCase();
  return s==='critical'?'critical':s==='fire'?'fire':s==='warning'?'warning':'safe';
}
function zBg(s){
  const c = cls(s);
  if(c==='critical') return ['#ffdad6','#7a0010'];
  if(c==='fire')     return ['#ffe0e3','#c0001a'];
  if(c==='warning')  return ['#fff3cd','#d97706'];
  return ['#d4f4e6','#006944'];
}
function zIconBg(s){
  const c = cls(s);
  if(c==='critical'||c==='fire') return '#ffcdd2';
  if(c==='warning') return '#fde68a';
  return '#bbf7d0';
}
function zIcon(s){
  const c = cls(s);
  if(c==='critical') return '&#128308;';
  if(c==='fire') return '&#128293;';
  if(c==='warning') return '&#9888;';
  return '&#9989;';
}

function zoneHtml(zones){
  return zones.map(z => {
    let [bg,col] = zBg(z.status);
    let st = cls(z.status);
    return '<div class="zcard" style="background:'+bg+';border-color:'+bg+'">'
      +'<div class="zcard-top">'
      +'<div class="zicon" style="background:'+zIconBg(z.status)+'">'+zIcon(z.status)+'</div>'
      +'<span class="zbadge '+st+'">'+z.status+'</span>'
      +'</div>'
      +'<div class="zname" style="color:'+col+'">Zone '+z.name+'</div>'
      +'<div class="zscore" style="color:'+col+'">'+z.score+'<span class="zscore-sub">/10</span></div>'
      +'</div>';
  }).join('');
}

function stripBg(status){
  const c = cls(status);
  if(c==='critical'||c==='fire') return ['#ffe0e3','#c0001a'];
  if(c==='warning')              return ['#fff3cd','#d97706'];
  return ['#d4f4e6','#006944'];
}

let shownLogCount = 0;
function updateChat(logArr){
  let box = document.getElementById('chatBox');
  let items = logArr.filter(Boolean);
  if(items.length > shownLogCount){
    for(let i = shownLogCount; i < items.length; i++){
      let line = items[i];
      if(line.startsWith('[CHAT]')){
        let div = document.createElement('div');
        div.className = 'chat';
        div.textContent = '\u{1F4AC} ' + line.substring(7);
        box.appendChild(div);
        box.scrollTop = box.scrollHeight;
      }
    }
    shownLogCount = items.length;
  }
}

let animFrame = 0;
let renderState = null;

function drawEscape(){
  animFrame++;
  let cvs = document.getElementById('escCanvas');
  if(!cvs || !renderState){ requestAnimationFrame(drawEscape); return; }
  cvs.width = cvs.offsetWidth;
  cvs.height = 200;
  let ctx = cvs.getContext('2d');
  let W = cvs.width, H = cvs.height;
  ctx.fillStyle = '#0a1628';
  ctx.fillRect(0,0,W,H);

  let s = renderState;
  let zones = s.zones || [];
  let exits = s.exits || [];

  function getZoneColor(idx){
    let z = zones[idx];
    if(!z) return null;
    let st = (z.status||'').toLowerCase();
    let cong = exits[idx] && exits[idx].congested;
    if(st==='fire'||st==='critical') return '#e53935';
    if(cong) return '#f59e0b';
    if(st==='warning') return '#fbbf24';
    if(st==='safe') return '#22c55e';
    return null;
  }

  function drawLedZone(x, y, w, h, color, dir, label){
    if(!color){
      ctx.strokeStyle='#1e2d44';ctx.lineWidth=1;ctx.strokeRect(x,y,w,h);
      ctx.fillStyle='#2a3a55';ctx.fillRect(x,y,w,h);
      ctx.fillStyle='#3a5070';ctx.font='bold 11px sans-serif';
      ctx.textAlign='center';ctx.fillText(label,x+w/2,y+h/2+4);
      return;
    }
    ctx.fillStyle='#1a2840';ctx.fillRect(x,y,w,h);
    ctx.strokeStyle=color+'44';ctx.lineWidth=1;ctx.strokeRect(x,y,w,h);
    let numLeds = Math.floor(w/8);
    let ledSpacing = w / numLeds;
    let step = Math.floor(animFrame/3) % numLeds;
    for(let i = 0; i < numLeds; i++){
      let px = x + i*ledSpacing + ledSpacing/2;
      let py = y + h/2;
      let pos = (dir==='left') ? (numLeds-1-step) : step;
      let lit = false;
      for(let t = 0; t < 3; t++){
        let tp = (dir==='left') ? (pos+t) : (pos-t);
        if(tp===i){ lit=true; break; }
      }
      if(lit){
        ctx.beginPath();ctx.arc(px,py,4,0,Math.PI*2);
        ctx.fillStyle=color;ctx.fill();
        let grd=ctx.createRadialGradient(px,py,0,px,py,10);
        grd.addColorStop(0,color+'99');grd.addColorStop(1,'transparent');
        ctx.beginPath();ctx.arc(px,py,10,0,Math.PI*2);
        ctx.fillStyle=grd;ctx.fill();
      } else {
        ctx.beginPath();ctx.arc(px,py,2,0,Math.PI*2);
        ctx.fillStyle=color+'30';ctx.fill();
      }
    }
    ctx.fillStyle='#ffffff99';ctx.font='10px sans-serif';ctx.textAlign='center';
    ctx.fillText(label,x+w/2,y+h-3);
  }

  let zW=Math.floor(W*0.32), zH=32;
  let fA=zones[0]&&(cls(zones[0].status)==='fire'||cls(zones[0].status)==='critical');
  let fB=zones[1]&&(cls(zones[1].status)==='fire'||cls(zones[1].status)==='critical');
  let fC=zones[2]&&(cls(zones[2].status)==='fire'||cls(zones[2].status)==='critical');
  let dirA=fA?'right':'left';
  let dirC=fC?'left':'right';
  drawLedZone(10,30,zW,zH,getZoneColor(0),dirA,'Zone A');
  drawLedZone(W-zW-10,30,zW,zH,getZoneColor(2),dirC,'Zone C');

  let zBx=Math.floor(W/2)-15, zBw=30, zBy=80, zBh=80;
  let zBc=getZoneColor(1);
  ctx.fillStyle=zBc?'#1a2840':'#141e30';
  ctx.fillRect(zBx,zBy,zBw,zBh);
  if(zBc){
    ctx.strokeStyle=zBc+'44';ctx.lineWidth=1;ctx.strokeRect(zBx,zBy,zBw,zBh);
    let numV=Math.floor(zBh/8);
    let stepV=Math.floor(animFrame/3)%numV;
    for(let i=0;i<numV;i++){
      let py2=zBy+i*(zBh/numV)+(zBh/numV)/2;
      let px2=zBx+zBw/2;
      let lit=(i===stepV)||(i===(stepV+1)%numV);
      ctx.beginPath();ctx.arc(px2,py2,lit?4:2,0,Math.PI*2);
      ctx.fillStyle=lit?zBc:zBc+'30';ctx.fill();
    }
  }

  ctx.fillStyle='#7ab8d8';ctx.font='bold 11px sans-serif';
  ctx.textAlign='left';ctx.fillText('Exit A',10,20);
  ctx.textAlign='right';ctx.fillText('Exit C',W-10,20);
  ctx.textAlign='center';ctx.fillText('Exit B',W/2,H-8);

  ctx.strokeStyle='#1e3050';ctx.lineWidth=2;ctx.setLineDash([4,4]);
  ctx.beginPath();ctx.moveTo(10+zW,46);ctx.lineTo(W-zW-10,46);ctx.stroke();
  ctx.beginPath();ctx.moveTo(W/2,46);ctx.lineTo(W/2,80);ctx.stroke();
  ctx.setLineDash([]);

  requestAnimationFrame(drawEscape);
}

function render(s){
  renderState = s;

  let zh = zoneHtml(s.zones||[]);
  document.getElementById('wZones').innerHTML = zh;
  document.getElementById('tZones').innerHTML = zh;

  let ap =
    '<span class="pill '+(s.alerts.fire?'on F':'')+'">Fire</span>'
   +'<span class="pill '+(s.alerts.gas?'on G':'')+'">Gas Leak</span>'
   +'<span class="pill '+(s.alerts.sos?'on S':'')+'">SOS</span>';
  document.getElementById('wAlerts').innerHTML = ap;
  document.getElementById('irLbl').textContent = s.irTarget || 'not set';

  let sosCard = document.getElementById('sosCard');
  sosCard.style.borderColor = s.alerts.sos ? '#c0001a' : '#93c5fd';
  sosCard.style.background  = s.alerts.sos ? 'linear-gradient(135deg,#ffe0e3,#ffd0d4)' : '';
  document.getElementById('sosHead').textContent = s.alerts.sos ? 'SOS ACTIVE' : 'No SOS active';
  document.getElementById('sosLoc').textContent  = s.sosLocations || 'none';

  let za=s.zones[0], zb=s.zones[1], zc=s.zones[2];
  function applyStrip(el, z){
    let [bg,col] = zBg(z.status);
    el.style.background=bg; el.style.color=col; el.textContent='Zone '+z.name;
  }
  applyStrip(document.getElementById('fZA'),za);
  applyStrip(document.getElementById('fZB'),zb);
  applyStrip(document.getElementById('fZC'),zc);

  let exits = s.exits||[];
  ['A','B','C'].forEach((x,i) => {
    let el = document.getElementById('fEx'+x);
    let cong = exits[i] && exits[i].congested;
    el.style.color = cong ? 'var(--orange)' : 'var(--muted)';
    el.textContent = 'Exit '+x+(cong?' (!)':'');
  });

  let rh = (s.zones||[]).map(z => {
    let st=cls(z.status), fire=st==='fire'||st==='critical';
    let i=z.name.charCodeAt(0)-65;
    let cong=exits[i]&&exits[i].congested;
    let away=z.name==='A'?'towards Exit B or C':z.name==='B'?'towards Exit A or C':'towards Exit A or B';
    return '<div class="routerow"><b>Zone '+z.name+':</b> '+(fire?'Move away &mdash; '+away:'Proceed to Exit '+z.name)
      +(cong?' <span style="color:var(--orange)">(Exit '+z.name+' congested)</span>':'')+'</div>';
  }).join('');
  document.getElementById('routeText').innerHTML = rh;

  let nodes = s.nodes||[];
  let live = nodes.filter(n=>n.online).length;
  let active = nodes.filter(n=>n.active).length;
  document.getElementById('connSummary').textContent = live+' node(s) live / '+active+' active';
  document.getElementById('connBar').innerHTML = nodes.map(n =>
    '<span class="cbadge '+(n.online?'live':n.forced?'forced':'off')+'">'
    +'Node '+n.id+'<br><small>'+(n.online?'LIVE':n.forced?'FORCED':'OFFLINE')+'</small>'
    +'</span>').join('');

  document.getElementById('nodeCards').innerHTML = nodes.map(n => {
    let hCls = n.online?'live':n.forced?'forced':'offline';
    let sTxt = n.online?'LIVE':n.forced?'FORCED':'OFFLINE';
    let sCls = n.score>=8?'danger':n.score>=5?'danger':n.score>=3?'warn':'ok';
    return '<div class="ncard">'
      +'<div class="ncard-hdr '+hCls+'">'
      +'<span>Node '+n.id+'</span>'
      +'<span class="nstatus '+hCls+'">'+sTxt+'</span>'
      +'</div>'
      +'<div class="ncard-body">'
      +'<div class="nrow"><span class="nlabel">Score</span><span class="nval '+sCls+'">'+n.score+'/10</span></div>'
      +'<div class="nrow"><span class="nlabel">CO</span><span class="nval">'+n.co+' ppm</span></div>'
      +'<div class="ndivider"></div>'
      +'<div class="nrow"><span class="nlabel">Temp</span><span class="nval">'+n.temp+'&deg;C</span></div>'
      +'<div class="nrow"><span class="nlabel">Humidity</span><span class="nval">'+n.hum+'%</span></div>'
      +'<div class="nrow"><span class="nlabel">Flame</span><span class="nval '+(n.flame?'danger':'ok')+'">'+(n.flame?'YES':'No')+'</span></div>'
      +'<div class="nrow"><span class="nlabel">Gas</span><span class="nval '+(n.gas?'warn':'ok')+'">'+(n.gas?'YES':'No')+'</span></div>'
      +'</div>'
      +'</div>';
  }).join('');

  let logArr = (s.log||[]).filter(Boolean);
  let sl = document.getElementById('sysLog');
  sl.innerHTML = logArr.map(l => {
    let col = (l.includes('FIRE')||l.includes('CRITICAL')) ? 'color:#f87171'
            : (l.includes('GAS')||l.includes('WARN')) ? 'color:#fbbf24'
            : (l.includes('Ready')||l.includes('OK')) ? 'color:#34d399' : '';
    return '<div'+(col?' style="'+col+'"':'')+'>&gt; '+l+'</div>';
  }).join('');
  sl.scrollTop = sl.scrollHeight;

  updateChat(logArr);

  let pm = s.pendingMap||0;
  document.getElementById('pendingMap').textContent = pm>0 ? 'Node '+pm : 'none';
}

requestAnimationFrame(drawEscape);
connect();
</script>
</body>
</html>
)rawliteral";
}