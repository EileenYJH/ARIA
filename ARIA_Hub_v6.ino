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
String htmlPage();
void addLog(const String &line);
const char *zoneName(uint8_t zone);
const char *statusForScore(int score);
bool nodeOnline(uint8_t id);
bool nodeActive(uint8_t id);
int nodeEffectiveScore(uint8_t id);
bool anyFire();
bool anyGas();
bool anySos();
const char *irTargetName();
const char *zoneOfNode(uint8_t id);
String sosLocations();
String gasLocations();
int max3(int a, int b, int c);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
#else
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
#endif
  if (len != sizeof(NodePacket)) {
    addLog("[ESP-NOW] Ignored packet with unexpected size");
    return;
  }

  NodePacket packet;
  memcpy(&packet, incomingData, sizeof(packet));

  // Option B: if hub has a pending node assignment and packet has no ID,
  // remap it to the pending ID
  if (packet.nodeID == 0 && pendingNodeMap >= 1 && pendingNodeMap <= 6) {
    packet.nodeID = pendingNodeMap;
    addLog(String("[MAP] Unassigned node remapped to Node ") + String(pendingNodeMap));
  }

  if (packet.nodeID < 1 || packet.nodeID > 6) return;

  NodeState &n = nodes[packet.nodeID];
  n.coPpm = packet.coPpm;
  n.mq2Smooth = packet.mq2Smooth;
  n.mq7Raw = packet.mq7Raw;
  n.tempC = packet.tempC;
  n.humidity = packet.humidity;
  n.lastSeen = millis();

  n.fireScore = constrain(packet.fireScore, 0, 10);
  n.flameConfirmed = packet.flameConfirmed;
  n.gasLeakMode = packet.gasLeakMode;
  n.sosActive = packet.sosActive;

  static unsigned long lastPacketLog[7] = {0};
  if (millis() - lastPacketLog[packet.nodeID] > 5000 || n.fireScore >= SCORE_AMBER || n.gasLeakMode || n.sosActive) {
    lastPacketLog[packet.nodeID] = millis();
    addLog(String("[NODE] Node ") + String(packet.nodeID) + " score " + String(n.fireScore) +
           (n.gasLeakMode ? " GAS" : "") + (n.sosActive ? " SOS" : ""));
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_IR_MOTION, INPUT);

  digitalWrite(PIN_BUZZER, LOW);

  FastLED.addLeds<WS2812B, PIN_LED_STRIP, GRB>(leds, NUM_LEDS);
  FastLED.clear(true);

  clearAllState();
  setupEspNow();
  setupDashboard();

  if (String(STA_SSID).length() > 0) {
    WiFi.begin(STA_SSID, STA_PASSWORD);
    addLog(String("[NET] Connecting STA to ") + STA_SSID);
  } else {
    addLog("[NET] STA not configured, Telegram disabled");
  }

  Serial.println();
  Serial.println("ARIA Hub v6 ready");
  Serial.println("AP SSID: ARIA-HUB");
  Serial.println("AP password: aria1234");
  Serial.print("Dashboard: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("Type HELP for serial commands.");
  Serial.println();
  Serial.println("IR setup required:");
  Serial.println("  Type SETIR A    to use D34 for Exit A");
  Serial.println("  Type SETIR B    to use D34 for Exit B");
  Serial.println("  Type SETIR C    to use D34 for Exit C");
  Serial.println("  Type SETIR ALL  to apply D34 congestion to all exits");
  addLog(String("[HUB] Ready at http://") + WiFi.softAPIP().toString());
  addLog("[IR] Waiting for SETIR A/B/C/ALL");
}

void loop() {
  unsigned long now = millis();

  ws.cleanupClients();
  dnsServer.processNextRequest();
  handleSerialCommands();
  updateExitCongestion(now);
  updateOutputs();
  updateHubBuzzer(now);
  updateAlertNotifications(now);
  renderLedRouting(now);

  if (now - lastWsPush >= WS_PUSH_MS) {
    lastWsPush = now;
    pushDashboard();
  }

  if (now - lastStatusPrint >= STATUS_MS) {
    lastStatusPrint = now;
    if (anyFire() || anyGas() || anySos()) printStatus();
  }
}

void setupEspNow() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_CHANNEL);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    addLog("[ESP-NOW] Init failed");
    return;
  }

  uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddr, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(broadcastAddr)) {
    esp_now_add_peer(&peerInfo);
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.print("[ESP-NOW] Ready. Hub STA MAC: ");
  Serial.println(WiFi.macAddress());
}

void sendNodeCommand(uint8_t targetNodeID, const String &command) {
  HubCommandPacket packet = {};
  packet.magic = 0xA6;
  packet.targetNodeID = targetNodeID;
  String c = command;
  c.trim();
  c.toUpperCase();
  c.toCharArray(packet.command, sizeof(packet.command));

  uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t *)&packet, sizeof(packet));
  if (result == ESP_OK) {
    addLog(String("[HUB->NODE] target ") + String(targetNodeID) + " cmd: " + c);
  } else {
    addLog(String("[HUB->NODE] send failed code ") + String((int)result) + " (" + espNowErrName((int)result) + ")");
    uint8_t ch = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&ch, &second) == ESP_OK) {
      addLog(String("[HUB->NODE] current Wi-Fi channel: ") + String(ch) + ", configured ESPNOW channel: " + String(WIFI_CHANNEL));
      addLog("[HUB->NODE] If using STA for Telegram, router channel must match node channel.");
    }
  }
}

const char *espNowErrName(int code) {
  switch (code) {
    case ESP_ERR_ESPNOW_NOT_INIT: return "NOT_INIT";
    case ESP_ERR_ESPNOW_ARG: return "ARG";
    case ESP_ERR_ESPNOW_NO_MEM: return "NO_MEM";
    case ESP_ERR_ESPNOW_FULL: return "FULL";
    case ESP_ERR_ESPNOW_NOT_FOUND: return "NOT_FOUND";
    case ESP_ERR_ESPNOW_INTERNAL: return "INTERNAL";
    case ESP_ERR_ESPNOW_EXIST: return "EXIST";
    case ESP_ERR_ESPNOW_IF: return "IF";
    case ESP_ERR_ESPNOW_CHAN: return "CHAN";
    default: return "UNKNOWN";
  }
}

void setupDashboard() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      client->text(dashboardJson());
    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String cmd = "";
        for (size_t i = 0; i < len; i++) cmd += (char)data[i];
        cmd.trim();
        if (cmd.length()) runCommand(cmd, true);
      }
    }
  });

  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage());
  });
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlPage());
  });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", dashboardJson());
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.begin();
}

void updateExitCongestion(unsigned long now) {
  if (irTarget == IR_TARGET_UNSET) {
    for (uint8_t i = 0; i < 3; i++) {
      exits[i].motion = false;
      if (!exits[i].forced) exits[i].congested = false;
    }
    return;
  }

  bool motionNow = digitalRead(PIN_IR_MOTION) == HIGH;

  for (uint8_t i = 0; i < 3; i++) {
    bool sensorApplies = (irTarget == IR_TARGET_ALL || irTarget == (IrTarget)i);

    if (sensorApplies) {
      updateExitFromMotion(i, motionNow, now);
    } else {
      exits[i].motion = false;
      if (!exits[i].forced && exits[i].congested) {
        exits[i].congested = false;
        char exitName = 'A' + i;
        addLog(String("[IR] Exit ") + exitName + " clear (not assigned to D34)");
      }
    }
  }
}

void updateExitFromMotion(uint8_t index, bool motionNow, unsigned long now) {
    ExitState &e = exits[index];
    e.motion = motionNow;

    if (e.windowStart == 0) e.windowStart = now;
    if (e.rateStart == 0) e.rateStart = now;
    if (e.lastMotionSample == 0) e.lastMotionSample = now;

    unsigned long elapsed = now - e.lastMotionSample;
    e.lastMotionSample = now;
    if (motionNow) e.motionMsInWindow += elapsed;

    if (motionNow && !e.lastMotion) {
      e.eventsInRateWindow++;
      e.motionStart = now;
      e.clearStart = 0;
    }
    if (!motionNow && e.lastMotion) {
      e.clearStart = now;
      e.motionStart = 0;
    }
    e.lastMotion = motionNow;

    if (now - e.windowStart >= WINDOW_MS) {
      e.motionMsInWindow = 0;
      e.windowStart = now;
    }
    if (now - e.rateStart >= RATE_WINDOW_MS) {
      e.eventsInRateWindow = 0;
      e.rateStart = now;
    }

    bool continuous = motionNow && e.motionStart > 0 && now - e.motionStart >= CONGEST_CONT_MS;
    bool windowBusy = e.motionMsInWindow > (WINDOW_MS * 3UL / 10UL);
    bool rateBusy = e.eventsInRateWindow > 5;
    bool countBusy = e.eventsInRateWindow >= CONGEST_EVENT_THRESHOLD;
    bool measuredCongested = continuous || windowBusy || rateBusy || countBusy;

    if (measuredCongested) {
      e.congestedHoldUntil = now + CONGEST_BLOCK_HOLD_MS;
    }

    bool holdActive = now < e.congestedHoldUntil;
    bool canClear = !motionNow &&
                    e.clearStart > 0 &&
                    now - e.clearStart >= CONGEST_CLEAR_MS &&
                    !rateBusy &&
                    !windowBusy &&
                    !countBusy &&
                    !holdActive;

    bool nextCongested = e.forced || measuredCongested || (e.congested && !canClear);
    if (nextCongested != e.congested) {
      e.congested = nextCongested;
      char exitName = 'A' + index;
      addLog(String("[IR] Exit ") + exitName + (e.congested ? " congested" : " clear"));
    }
}

void updateOutputs() {
  bool fire = anyFire();
  // Fan relay and ventilation handled at node level only
  // Hub focuses on exit routing and LED guidance
}

// ── HUB BUZZER TIMING ───────────────────────────────────────────
// Active buzzer: digitalWrite HIGH=ON LOW=OFF, no tone() needed
// Emergency: 500ms on / 500ms off (classic alarm)
// Critical:  100ms on / 100ms off (urgent rapid beep)
// Gas:       200ms on / 300ms off
// SOS:       150ms on / 150ms off

void updateHubBuzzer(unsigned long now) {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);
  int overall = max3(scoreA, scoreB, scoreC);

  if (overall >= SCORE_CRITICAL) {
    // Critical — rapid 100ms beep, very urgent
    unsigned long phase = buzzerOn ? 100UL : 100UL;
    if (now - buzzerStepStart >= phase) {
      buzzerStepStart = now;
      buzzerOn = !buzzerOn;
    }
    digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
    return;
  }

  if (overall >= SCORE_FIRE) {
    // Emergency — classic alarm 500ms on/off
    unsigned long phase = buzzerOn ? 500UL : 500UL;
    if (now - buzzerStepStart >= phase) {
      buzzerStepStart = now;
      buzzerOn = !buzzerOn;
    }
    digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
    return;
  }

  if (anyGas()) {
    // Gas — 200ms on / 300ms off
    unsigned long phase = buzzerOn ? (unsigned long)GAS_BEEP_ON_MS : (unsigned long)GAS_BEEP_OFF_MS;
    if (now - buzzerStepStart >= phase) {
      buzzerStepStart = now;
      buzzerOn = !buzzerOn;
    }
    digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
    return;
  }

  if (anySos()) {
    // SOS indicator — 150ms on / 150ms off
    unsigned long phase = buzzerOn ? (unsigned long)SOS_BEEP_ON_MS : (unsigned long)SOS_BEEP_OFF_MS;
    if (now - buzzerStepStart >= phase) {
      buzzerStepStart = now;
      buzzerOn = !buzzerOn;
    }
    digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
    return;
  }

  // Idle
  buzzerOn = false;
  digitalWrite(PIN_BUZZER, LOW);
}

void updateAlertNotifications(unsigned long now) {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);
  int overall = max3(scoreA, scoreB, scoreC);

  int tier = 0;
  // Tier 1 pre-warning also sends (once)
  int tier = 0;
  if (overall >= SCORE_CRITICAL) tier = 3;
  else if (overall >= SCORE_FIRE) tier = 2;
  else if (overall >= SCORE_AMBER) tier = 1;

  if (tier == 0) {
    lastAlertTierSent = 0;
    return;
  }

  if (tier == lastAlertTierSent && now - lastAlertNotifyTime < ALERT_NOTIFY_COOLDOWN_MS) {
    return;
  }

  // Build message with real newlines
  String msg = "ARIA FIRE ALERT\n";
  if (tier == 3) msg += "CRITICAL (Tier 3) — Warden contact BOMBA 999\n";
  else if (tier == 2) msg += "EMERGENCY (Tier 2) — Evacuate now\n";
  else msg += "PRE-WARNING (Tier 1) — Investigate\n";
  msg += "\nZone A: " + String(scoreA) + "/10\n";
  msg += "Zone B: " + String(scoreB) + "/10\n";
  msg += "Zone C: " + String(scoreC) + "/10\n";
  msg += "\nExit A: " + String(exits[0].congested ? "CONGESTED" : "clear") + "\n";
  msg += "Exit B: " + String(exits[1].congested ? "CONGESTED" : "clear") + "\n";
  msg += "Exit C: " + String(exits[2].congested ? "CONGESTED" : "clear");
  if (anySos())  msg += "\n\nSOS: " + sosLocations();
  if (anyGas())  msg += "\nGas: " + gasLocations();

  sendTelegramAlert(msg);
  lastAlertTierSent = tier;
  lastAlertNotifyTime = now;
}

void sendTelegramAlert(const String &text) {
  if (String(TELEGRAM_BOT_TOKEN).length() == 0 || String(TELEGRAM_CHAT_ID).length() == 0) {
    addLog("[TELEGRAM] Not configured — fill STA_SSID + token in firmware");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    addLog("[TELEGRAM] No STA internet connection");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendMessage";

  if (!http.begin(client, url)) {
    addLog("[TELEGRAM] HTTP begin failed");
    return;
  }

  // JSON body — handles newlines and special chars cleanly
  http.addHeader("Content-Type", "application/json");

  String escaped = text;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");

  String body = "{\"chat_id\":\"";
  body += TELEGRAM_CHAT_ID;
  body += "\",\"text\":\"";
  body += escaped;
  body += "\"}";

  int code = http.POST(body);
  if (code == 200) addLog("[TELEGRAM] Alert sent OK");
  else if (code > 0) addLog(String("[TELEGRAM] Code ") + code + " — check token/chat_id");
  else addLog(String("[TELEGRAM] Failed: ") + http.errorToString(code));
  http.end();
}

void computeZoneScores(int &scoreA, int &scoreB, int &scoreC) {
  scoreA = max(nodeEffectiveScore(1), nodeEffectiveScore(5));
  scoreB = max(nodeEffectiveScore(2), nodeEffectiveScore(3));
  scoreC = max(nodeEffectiveScore(4), nodeEffectiveScore(6));
}

void computeZoneRender(ZoneRender render[3]) {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);

  bool fireA = scoreA >= SCORE_FIRE;
  bool fireB = scoreB >= SCORE_FIRE;
  bool fireC = scoreC >= SCORE_FIRE;
  bool warningOnly = !fireA && !fireB && !fireC && max3(scoreA, scoreB, scoreC) >= SCORE_AMBER;
  bool gasOnly = anyGas() && !fireA && !fireB && !fireC;

  for (uint8_t i = 0; i < 3; i++) {
    render[i] = {COLOR_OFF, DIR_OFF};
  }

  if (gasOnly) {
    render[0] = {COLOR_YELLOW, DIR_LEFT};
    render[1] = {COLOR_YELLOW, DIR_SPLIT};
    render[2] = {COLOR_YELLOW, DIR_RIGHT};
    return;
  }

  if (warningOnly) {
    for (uint8_t i = 0; i < 3; i++) render[i] = {COLOR_ORANGE, DIR_PULSE};
    return;
  }

  if (!fireA && !fireB && !fireC) return;

  if (fireA && fireB && fireC) {
    render[0] = {COLOR_RED, DIR_LEFT};
    render[1] = {COLOR_RED, DIR_SPLIT};
    render[2] = {COLOR_RED, DIR_RIGHT};
  } else if (fireA && fireC) {
    render[0] = {COLOR_RED, DIR_RIGHT};
    render[1] = {COLOR_GREEN, DIR_PULSE};
    render[2] = {COLOR_RED, DIR_LEFT};
  } else if (fireA && fireB) {
    render[0] = {COLOR_RED, DIR_RIGHT};
    render[1] = {COLOR_RED, DIR_RIGHT};
    render[2] = {COLOR_GREEN, DIR_RIGHT};
  } else if (fireB && fireC) {
    render[0] = {COLOR_GREEN, DIR_LEFT};
    render[1] = {COLOR_RED, DIR_LEFT};
    render[2] = {COLOR_RED, DIR_LEFT};
  } else if (fireA) {
    render[0] = {COLOR_RED, DIR_RIGHT};
    render[1] = {COLOR_GREEN, DIR_SPLIT};
    render[2] = {COLOR_GREEN, DIR_RIGHT};
  } else if (fireB) {
    render[0] = {COLOR_GREEN, DIR_LEFT};
    render[1] = {COLOR_RED, DIR_SPLIT};
    render[2] = {COLOR_GREEN, DIR_RIGHT};
  } else if (fireC) {
    render[0] = {COLOR_GREEN, DIR_LEFT};
    render[1] = {COLOR_GREEN, DIR_SPLIT};
    render[2] = {COLOR_RED, DIR_LEFT};
  }

  for (uint8_t z = 0; z < 3; z++) {
    if (render[z].color == COLOR_GREEN && exitCongestedForZone(z)) {
      render[z].color = COLOR_ORANGE;
    }
  }
}

void renderLedRouting(unsigned long now) {
  if (now - lastAnimTime < ANIM_INTERVAL_MS) return;
  lastAnimTime = now;
  chaseStep++;

  ZoneRender render[3];
  computeZoneRender(render);

  FastLED.clear();
  for (uint8_t z = 0; z < 3; z++) drawZone(z, render[z]);
  FastLED.show();
}

void drawZone(uint8_t zone, ZoneRender zr) {
  if (zr.color == COLOR_OFF || zr.direction == DIR_OFF) return;
  CRGB c = colorFor(zr.color);

  if (zr.direction == DIR_PULSE) {
    drawPulseZone(zone, c);
    return;
  }

  if (zone == 1 && zr.direction == DIR_SPLIT) {
    drawSplitZoneB(c);
    return;
  }

  uint8_t start = zone * ZONE_LEN;
  uint8_t end = start + ZONE_LEN - 1;
  drawChaseRange(start, end, zr.direction, c);
}

void drawChaseRange(uint8_t start, uint8_t end, Direction dir, CRGB color) {
  // Single moving packet — 1 lead pixel + trailing fade pixels based on proximity to exit
  // No secondary packet — keeps it clean and avoids colour mixing
  int len = (int)end - (int)start + 1;
  if (len <= 0) return;

  int progress = chaseStep % len;
  // Lead pixel position
  int lead = (dir == DIR_LEFT)
    ? ((int)end - progress)
    : ((int)start + progress);

  auto inRange = [&](int p) -> bool {
    return p >= (int)start && p <= (int)end;
  };

  // Distance from exit determines trail length (proximity packing)
  int exitPos  = (dir == DIR_LEFT) ? (int)start : (int)end;
  int distToExit = abs(lead - exitPos);

  int trailLen;
  if      (distToExit <= 2) trailLen = 4;  // dense near exit
  else if (distToExit <= 5) trailLen = 3;  // medium
  else if (distToExit <= 8) trailLen = 2;  // sparse
  else                      trailLen = 1;  // very sparse far from exit

  // Draw lead pixel at full brightness
  if (inRange(lead)) leds[lead] = color;

  // Draw trail pixels behind the lead, dimming progressively
  for (int t = 1; t < trailLen; t++) {
    int tp = (dir == DIR_LEFT) ? (lead + t) : (lead - t);
    if (!inRange(tp)) continue;
    CRGB trailColor = color;
    // Dim each trail pixel: 60% → 35% → 15%
    uint8_t dim = (t == 1) ? 150 : (t == 2) ? 90 : 40;
    trailColor.nscale8_video(dim);
    leds[tp] = trailColor;
  }
}

void drawSplitZoneB(CRGB color) {
  drawChaseRange(10, 14, DIR_LEFT, color);
  drawChaseRange(15, 19, DIR_RIGHT, color);
}

void drawPulseZone(uint8_t zone, CRGB color) {
  uint8_t start = zone * ZONE_LEN;
  uint8_t end = start + ZONE_LEN - 1;
  uint8_t brightness = beatsin8(30, 45, 255);
  CRGB pulse = color;
  pulse.nscale8_video(brightness);
  for (uint8_t i = start; i <= end; i++) leds[i] = pulse;
}

CRGB colorFor(ZoneColor c) {
  if (c == COLOR_GREEN) return CRGB(0, 180, 0);
  if (c == COLOR_RED) return CRGB(220, 0, 0);
  if (c == COLOR_ORANGE) return CRGB(255, 140, 0);
  if (c == COLOR_YELLOW) return CRGB(255, 212, 0);
  return CRGB::Black;
}

bool exitCongestedForZone(uint8_t zone) {
  if (zone == 0) return exits[0].congested;
  if (zone == 1) return exits[1].congested;
  return exits[2].congested;
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length()) runCommand(cmd, false);
}

void runCommand(String cmd, bool fromWeb) {
  String original = cmd;
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("NODE ")) {
    String rest = cmd.substring(5);
    rest.trim();
    int space = rest.indexOf(' ');

    if (space > 0) {
      String first = rest.substring(0, space);
      String second = rest.substring(space + 1);
      first.trim();
      second.trim();
      int id = first.toInt();

      if (id >= 1 && id <= 6 && second == "CLEAR") {
        nodes[id].forced = false;
        nodes[id].fireScore = 0;
        nodes[id].gasLeakMode = false;
        nodes[id].sosActive = false;
        nodes[id].flameConfirmed = false;
        addLog(String("[CMD] Node ") + String(id) + " state cleared");
      } else {
        int score = second.toInt();
        if (id >= 1 && id <= 6 && score >= 0 && score <= 10) {
          nodes[id].fireScore = score;
          nodes[id].forced = true;
          addLog(String("[CMD] Node ") + String(id) + " score " + String(score));
        }
      }
    } else {
      int score = rest.toInt();
      if (score >= 0 && score <= 10) {
        nodes[1].fireScore = score;
        nodes[1].forced = true;
        addLog(String("[CMD] Node 1 score ") + String(score));
      }
    }
  } else if (cmd.startsWith("NODECMD ")) {
    String rest = cmd.substring(8);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      int id = rest.substring(0, space).toInt();
      String nodeCmd = rest.substring(space + 1);
      nodeCmd.trim();
      if (id >= 0 && id <= 255 && nodeCmd.length()) {
        sendNodeCommand((uint8_t)id, nodeCmd);
      } else {
        addLog("[CMD] Use NODECMD <targetID> <command>");
      }
    } else {
      addLog("[CMD] Use NODECMD <targetID> <command>");
    }
  } else if (cmd.startsWith("SETNODE ")) {
    String rest = cmd.substring(8);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      // Hub-to-node wireless command (old behaviour)
      int target = rest.substring(0, space).toInt();
      int newId = rest.substring(space + 1).toInt();
      if (target >= 0 && target <= 255 && newId >= 1 && newId <= 6) {
        sendNodeCommand((uint8_t)target, String("SETNODE ") + String(newId));
      }
    } else {
      // Option B: set hub-side pending map for next unassigned packet
      int newId = rest.toInt();
      if (newId >= 1 && newId <= 6) {
        pendingNodeMap = (uint8_t)newId;
        addLog(String("[MAP] Hub will remap next unassigned node to Node ") + String(newId));
      } else if (newId == 0) {
        pendingNodeMap = 0;
        addLog("[MAP] Node remapping cleared");
      } else {
        addLog("[CMD] Use SETNODE <1-6> to remap, or SETNODE 0 to clear");
      }
    }
  } else if (cmd.startsWith("SETSCORE ")) {
    String rest = cmd.substring(9);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      int id = rest.substring(0, space).toInt();
      int score = rest.substring(space + 1).toInt();
      if (id >= 1 && id <= 6 && score >= 0 && score <= 10) {
        nodes[id].fireScore = score;
        if (score >= SCORE_FIRE) nodes[id].gasLeakMode = false;
        nodes[id].forced = true;
        addLog(String("[CMD] Set Node ") + String(id) + " score " + String(score));
      } else {
        addLog("[CMD] Use SETSCORE <1-6> <0-10>");
      }
    } else {
      addLog("[CMD] Use SETSCORE <1-6> <0-10>");
    }
  } else if (cmd.startsWith("GAS ")) {
    String rest = cmd.substring(4);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      int id = rest.substring(0, space).toInt();
      String value = rest.substring(space + 1);
      value.trim();
      if (id >= 1 && id <= 6 && (value == "ON" || value == "OFF")) {
        nodes[id].gasLeakMode = (value == "ON");
        if (nodes[id].gasLeakMode) nodes[id].fireScore = 0;
        nodes[id].forced = true;
        addLog(String("[CMD] Node ") + String(id) + " gas " + value);
      }
    }
  } else if (cmd.startsWith("SOS ")) {
    String rest = cmd.substring(4);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      int id = rest.substring(0, space).toInt();
      String value = rest.substring(space + 1);
      value.trim();
      if (id >= 1 && id <= 6 && (value == "ON" || value == "OFF")) {
        nodes[id].sosActive = (value == "ON");
        nodes[id].forced = true;
        addLog(String("[CMD] Node ") + String(id) + " SOS " + value);
      }
    }
  } else if (cmd.startsWith("FLAME ")) {
    String rest = cmd.substring(6);
    rest.trim();
    int space = rest.indexOf(' ');
    if (space > 0) {
      int id = rest.substring(0, space).toInt();
      String value = rest.substring(space + 1);
      value.trim();
      if (id >= 1 && id <= 6 && (value == "ON" || value == "OFF")) {
        nodes[id].flameConfirmed = (value == "ON");
        nodes[id].forced = true;
        addLog(String("[CMD] Node ") + String(id) + " flame " + value);
      }
    }
  } else if (cmd.startsWith("SETIR ")) {
    handleSetIr(cmd.substring(6));
  } else if (cmd.startsWith("CONGEST ")) {
    char e = cmd.charAt(8);
    if (e >= 'A' && e <= 'C') {
      exits[e - 'A'].forced = true;
      exits[e - 'A'].congested = true;
      addLog(String("[CMD] Exit ") + e + " forced congested");
    }
  } else if (cmd.startsWith("CLEAR CONGEST ")) {
    char e = cmd.charAt(14);
    if (e >= 'A' && e <= 'C') {
      exits[e - 'A'].forced = false;
      exits[e - 'A'].congested = false;
      addLog(String("[CMD] Exit ") + e + " congestion cleared");
    }
  } else if (cmd == "CLEAR") {
    clearAllState();
    addLog("[CMD] All state cleared");
  } else if (cmd == "STATUS") {
    printStatus();
  } else if (cmd == "HELP") {
    Serial.println("Commands:");
    Serial.println("  NODECMD <id> <cmd> send remote command to node");
    Serial.println("  SETNODE <target> <newID> set node ID remotely (target 0 for unassigned)");
    Serial.println("  NODE <id> <score>  e.g. NODE 1 7");
    Serial.println("  NODE <score>       shortcut, sets Node 1");
    Serial.println("  SETSCORE <id> <0-10> set node fire score for reaction test");
    Serial.println("  NODE <id> CLEAR    clear forced state for one node");
    Serial.println("  GAS <id> ON/OFF    force gas leak on one node");
    Serial.println("  SOS <id> ON/OFF    force SOS on one node");
    Serial.println("  FLAME <id> ON/OFF  force flame flag on one node");
    Serial.println("  SETIR A/B/C/ALL    assign D34 IR motion sensor");
    Serial.println("  CONGEST A/B/C      force exit congestion");
    Serial.println("  CLEAR CONGEST A/B/C");
    Serial.println("  CLEAR              reset all state");
    Serial.println("  STATUS             print node and exit state");
    Serial.println("  HELP               show commands");
  } else if (cmd.length()) {
    addLog(String("[CMD] Unknown: ") + original);
  }

  if (fromWeb && !cmd.startsWith("NODE ") && !cmd.startsWith("NODECMD ") &&
      !cmd.startsWith("SETNODE ") && !cmd.startsWith("SETSCORE ") &&
      !cmd.startsWith("GAS ") &&
      !cmd.startsWith("SOS ") && !cmd.startsWith("FLAME ") &&
      !cmd.startsWith("SETIR ") && !cmd.startsWith("CONGEST ") &&
      cmd != "CLEAR" && cmd != "STATUS") {
    addLog(String("[CHAT] ") + original);
  }
  pushDashboard();
}

void handleSetIr(String target) {
  target.trim();
  target.toUpperCase();

  if (target == "A") {
    irTarget = IR_TARGET_A;
  } else if (target == "B") {
    irTarget = IR_TARGET_B;
  } else if (target == "C") {
    irTarget = IR_TARGET_C;
  } else if (target == "ALL") {
    irTarget = IR_TARGET_ALL;
  } else {
    addLog("[IR] Invalid target. Use SETIR A, SETIR B, SETIR C, or SETIR ALL");
    return;
  }

  for (uint8_t i = 0; i < 3; i++) {
    exits[i].motion = false;
    if (!exits[i].forced) exits[i].congested = false;
    exits[i].motionStart = 0;
    exits[i].clearStart = 0;
    exits[i].windowStart = millis();
    exits[i].rateStart = millis();
    exits[i].motionMsInWindow = 0;
    exits[i].eventsInRateWindow = 0;
    exits[i].congestedHoldUntil = 0;
    exits[i].lastMotion = false;
    exits[i].lastMotionSample = millis();
  }

  addLog(String("[IR] D34 assigned to ") + irTargetName());
}

void clearAllState() {
  for (uint8_t i = 0; i < 7; i++) {
    nodes[i] = NodeState{0, 0, 0, 0, false, false, false, 0.0f, 0.0f, 0, false};
  }
  for (uint8_t i = 0; i < 3; i++) {
    exits[i].motion = false;
    exits[i].congested = false;
    exits[i].forced = false;
    exits[i].motionStart = 0;
    exits[i].clearStart = 0;
    exits[i].windowStart = millis();
    exits[i].rateStart = millis();
    exits[i].motionMsInWindow = 0;
    exits[i].eventsInRateWindow = 0;
    exits[i].congestedHoldUntil = 0;
    exits[i].lastMotion = false;
    exits[i].lastMotionSample = millis();
  }
  FastLED.clear(true);
}

void printStatus() {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);

  Serial.println("========================================");
  Serial.printf("Zone A: %d (%s)\n", scoreA, statusForScore(scoreA));
  Serial.printf("Zone B: %d (%s)\n", scoreB, statusForScore(scoreB));
  Serial.printf("Zone C: %d (%s)\n", scoreC, statusForScore(scoreC));
  for (uint8_t i = 1; i <= 6; i++) {
    Serial.printf("Node %u: score=%d co=%d mq2=%d gas=%s sos=%s online=%s active=%s forced=%s\n",
      i, nodes[i].fireScore, nodes[i].coPpm, nodes[i].mq2Smooth,
      nodes[i].gasLeakMode ? "YES" : "no",
      nodes[i].sosActive ? "YES" : "no",
      nodeOnline(i) ? "YES" : "no",
      nodeActive(i) ? "YES" : "no",
      nodes[i].forced ? "YES" : "no");
  }
  Serial.printf("Exit A: %s%s\n", exits[0].congested ? "CONGESTED" : "clear", exits[0].forced ? " forced" : "");
  Serial.printf("Exit B: %s%s\n", exits[1].congested ? "CONGESTED" : "clear", exits[1].forced ? " forced" : "");
  Serial.printf("Exit C: %s%s\n", exits[2].congested ? "CONGESTED" : "clear", exits[2].forced ? " forced" : "");
  Serial.printf("IR D34 target: %s\n", irTargetName());
  Serial.println("========================================");
}

bool nodeOnline(uint8_t id) {
  if (id < 1 || id > 6) return false;
  return nodes[id].lastSeen > 0 && millis() - nodes[id].lastSeen <= NODE_TIMEOUT_MS;
}

bool nodeActive(uint8_t id) {
  if (id < 1 || id > 6) return false;
  // A node is active if it is live OR if it has been manually forced
  return nodeOnline(id) || nodes[id].forced;
}

// Option B: hub-side node ID mapping for demo
// When a packet arrives with nodeID=0 (unassigned), hub maps it to pendingNodeMap
uint8_t pendingNodeMap = 0;  // 0 = no pending assignment

int nodeEffectiveScore(uint8_t id) {
  if (!nodeActive(id)) return 0;
  return nodes[id].fireScore;
}

bool anyFire() {
  for (uint8_t i = 1; i <= 6; i++) {
    if (nodeActive(i) && nodes[i].fireScore >= SCORE_FIRE) return true;
  }
  return false;
}

bool anyGas() {
  for (uint8_t i = 1; i <= 6; i++) {
    if (nodeActive(i) && nodes[i].gasLeakMode) return true;
  }
  return false;
}

bool anySos() {
  for (uint8_t i = 1; i <= 6; i++) {
    if (nodeActive(i) && nodes[i].sosActive) return true;
  }
  return false;
}


const char *irTargetName() {
  if (irTarget == IR_TARGET_A) return "Exit A";
  if (irTarget == IR_TARGET_B) return "Exit B";
  if (irTarget == IR_TARGET_C) return "Exit C";
  if (irTarget == IR_TARGET_ALL) return "All exits";
  return "not set";
}

const char *zoneOfNode(uint8_t id) {
  if (id == 1 || id == 5) return "Zone A";
  if (id == 2 || id == 3) return "Zone B";
  if (id == 4 || id == 6) return "Zone C";
  return "Unknown";
}

String sosLocations() {
  String out = "";
  for (uint8_t i = 1; i <= 6; i++) {
    if (!nodeActive(i) || !nodes[i].sosActive) continue;
    if (out.length()) out += ", ";
    out += "Node ";
    out += String(i);
    out += " (";
    out += zoneOfNode(i);
    out += ")";
  }
  if (!out.length()) out = "none";
  return out;
}

String gasLocations() {
  String out = "";
  for (uint8_t i = 1; i <= 6; i++) {
    if (!nodeActive(i) || !nodes[i].gasLeakMode) continue;
    if (out.length()) out += ", ";
    out += "Node ";
    out += String(i);
    out += " (";
    out += zoneOfNode(i);
    out += ")";
  }
  if (!out.length()) out = "none";
  return out;
}

int max3(int a, int b, int c) {
  return max(a, max(b, c));
}

const char *statusForScore(int score) {
  if (score >= SCORE_CRITICAL) return "CRITICAL";
  if (score >= SCORE_FIRE) return "FIRE";
  if (score >= SCORE_AMBER) return "WARNING";
  return "SAFE";
}

const char *zoneName(uint8_t zone) {
  if (zone == 0) return "A";
  if (zone == 1) return "B";
  return "C";
}

void addLog(const String &line) {
  logLines[logIndex] = line;
  logIndex = (logIndex + 1) % 12;
  Serial.println(line);
}

String dashboardJson() {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);

  String json = "{";
  json += "\"zones\":[";
  json += "{\"name\":\"A\",\"score\":";
  json += String(scoreA);
  json += ",\"status\":\"";
  json += statusForScore(scoreA);
  json += "\"},";
  json += "{\"name\":\"B\",\"score\":";
  json += String(scoreB);
  json += ",\"status\":\"";
  json += statusForScore(scoreB);
  json += "\"},";
  json += "{\"name\":\"C\",\"score\":";
  json += String(scoreC);
  json += ",\"status\":\"";
  json += statusForScore(scoreC);
  json += "\"}";
  json += "],";

  json += "\"nodes\":[";
  for (uint8_t i = 1; i <= 6; i++) {
    if (i > 1) json += ",";
    json += "{";
    json += "\"id\":";
    json += String(i);
    json += ",\"score\":";
    json += String(nodes[i].fireScore);
    json += ",\"co\":";
    json += String(nodes[i].coPpm);
    json += ",\"mq2\":";
    json += String(nodes[i].mq2Smooth);
    json += ",\"mq7\":";
    json += String(nodes[i].mq7Raw);
    json += ",\"temp\":";
    json += String(nodes[i].tempC, 1);
    json += ",\"hum\":";
    json += String(nodes[i].humidity, 1);
    json += ",\"flame\":";
    json += nodes[i].flameConfirmed ? "true" : "false";
    json += ",\"gas\":";
    json += nodes[i].gasLeakMode ? "true" : "false";
    json += ",\"sos\":";
    json += nodes[i].sosActive ? "true" : "false";
    json += ",\"online\":";
    json += nodeOnline(i) ? "true" : "false";
    json += ",\"active\":";
    json += nodeActive(i) ? "true" : "false";
    json += ",\"forced\":";
    json += nodes[i].forced ? "true" : "false";
    json += ",\"age\":";
    if (nodes[i].lastSeen > 0) json += String((millis() - nodes[i].lastSeen) / 1000);
    else json += "-1";
    json += "}";
  }
  json += "],";

  json += "\"exits\":[";
  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"";
    json += (char)('A' + i);
    json += "\",\"motion\":";
    json += exits[i].motion ? "true" : "false";
    json += ",\"congested\":";
    json += exits[i].congested ? "true" : "false";
    json += ",\"forced\":";
    json += exits[i].forced ? "true" : "false";
    json += "}";
  }
  json += "],";

  json += "\"alerts\":{";
  json += "\"fire\":";
  json += anyFire() ? "true" : "false";
  json += ",\"gas\":";
  json += anyGas() ? "true" : "false";
  json += ",\"sos\":";
  json += anySos() ? "true" : "false";
  json += "},";
  json += "\"sosLocations\":\"";
  String sos = sosLocations();
  sos.replace("\\", "\\\\");
  sos.replace("\"", "\\\"");
  json += sos;
  json += "\",";
  json += "\"gasLocations\":\"";
  String gas = gasLocations();
  gas.replace("\\", "\\\\");
  gas.replace("\"", "\\\"");
  json += gas;
  json += "\",";

  json += "\"irTarget\":\"";
  json += irTargetName();
  json += "\",";
  json += "\"pendingMap\":";
  json += String(pendingNodeMap);
  json += ",";

  json += "\"log\":[";
  for (uint8_t i = 0; i < 12; i++) {
    uint8_t idx = (logIndex + i) % 12;
    if (i > 0) json += ",";
    String line = logLines[idx];
    line.replace("\\", "\\\\");
    line.replace("\"", "\\\"");
    json += "\"" + line + "\"";
  }
  json += "]}";
  return json;
}

void pushDashboard() {
  ws.textAll(dashboardJson());
}



String htmlPage() {
  return R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ARIA Hub</title>
  <style>
    :root{--bg1:#e6f7ff;--bg2:#fff8e6;--ink:#12212e;--muted:#3f5568;
      --glass:rgba(255,255,255,.6);--line:rgba(18,33,46,.13);
      --green:#1fba62;--red:#e53935;--orange:#ff8c00;--yellow:#f9c500;--blue:#1e88e5}
    *{box-sizing:border-box;margin:0;padding:0}
    body{color:var(--ink);font-family:"Segoe UI",Arial,sans-serif;
      background:radial-gradient(900px at 0% 0%,#d7f3ff,transparent 60%),
                 radial-gradient(900px at 100% 0%,#ffeec8,transparent 55%),
                 linear-gradient(135deg,var(--bg1),var(--bg2));min-height:100vh}
    /* ── TOP BAR ── */
    .topbar{display:flex;align-items:center;gap:10px;padding:12px 18px;
      background:rgba(255,255,255,.75);backdrop-filter:blur(14px);
      border-bottom:1px solid var(--line);position:sticky;top:0;z-index:100}
    .topbar h1{font-size:20px;flex:1}
    .tabs{display:flex;gap:6px}
    .tab{padding:7px 16px;border-radius:999px;border:1px solid var(--line);
      background:#ffffffcc;font-weight:700;cursor:pointer;font-size:13px;color:var(--ink)}
    .tab.active{background:var(--blue);color:#fff;border-color:var(--blue)}
    /* ── LAYOUT ── */
    .page{display:none;padding:14px 16px}
    .page.active{display:block}
    .card{background:var(--glass);border:1px solid var(--line);border-radius:14px;
      padding:12px;backdrop-filter:blur(9px);margin-bottom:12px}
    h3{font-size:15px;margin-bottom:8px}
    .grid3{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:12px}
    .grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}
    /* ── STATUS CLASSES ── */
    .safe{color:var(--green)}.warning{color:var(--orange)}.fire{color:var(--red)}.critical{color:#b71c1c}
    /* ── ZONE CARDS ── */
    .zcard{background:var(--glass);border:1px solid var(--line);border-radius:12px;padding:10px;text-align:center}
    .zscore{font-size:28px;font-weight:900;line-height:1}
    .zstatus{font-size:13px;margin-top:4px;font-weight:700}
    /* ── NODE GRID ── */
    .node-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:12px}
    .ncard{background:var(--glass);border:1px solid var(--line);border-radius:12px;padding:10px}
    .ncard.offline{opacity:.5}
    .nrow{display:flex;justify-content:space-between;border-top:1px solid var(--line);
      padding-top:3px;margin-top:3px;font-size:12px}
    .nrow span:first-child{color:var(--muted)}
    /* ── ALERTS ── */
    .pills{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
    .pill{padding:6px 12px;border-radius:999px;background:#ffffffbb;
      border:1px solid var(--line);font-weight:700;font-size:13px}
    .pill.on.F{background:#ffd8d8;color:#8a1c1c}
    .pill.on.G{background:#fff7b5;color:#6a5800}
    .pill.on.S{background:#d7ebff;color:#104e8b}
    .pill.on.V{background:#d8ffe5;color:#0f6a33}
    /* ── FLOOR MAP ── */
    .floor-wrap{position:relative;background:#ffffffbb;border:1px solid var(--line);
      border-radius:12px;padding:10px;height:220px;margin-bottom:10px}
    .fstrip{position:absolute;border-radius:8px;border:2px solid var(--line);
      display:flex;align-items:center;justify-content:center;font-weight:700;font-size:12px}
    .fza{left:10px;top:20px;width:33%;height:50px}
    .fzb{left:38%;top:90px;width:24%;height:100px}
    .fzc{right:10px;top:20px;width:33%;height:50px}
    .fex{position:absolute;font-size:11px;font-weight:700;color:#5a6f81}
    .fexa{left:10px;top:4px}.fexb{left:42%;bottom:6px}.fexc{right:10px;top:4px}
    /* ── LIVE ESCAPE CANVAS ── */
    #escCanvas{width:100%;height:200px;border-radius:10px;border:1px solid var(--line);
      background:#0a1628;display:block}
    /* ── ROUTE TEXT ── */
    .routerow{padding:7px 10px;border-radius:8px;margin-bottom:6px;
      border:1px solid var(--line);background:#ffffffa8;font-size:13px}
    /* ── LOG + CHAT ── */
    .logbox{height:160px;overflow-y:auto;background:#ffffffb8;padding:8px 10px;
      border-radius:10px;font-family:monospace;font-size:11px;border:1px solid var(--line)}
    .logbox div{padding:1px 0;border-bottom:1px solid rgba(0,0,0,.04)}
    .logbox .chat{background:#e8f4ff;border-radius:4px;padding:1px 4px}
    .inrow{display:flex;gap:8px;margin-top:8px}
    input[type=text]{flex:1;padding:9px 12px;border-radius:8px;border:1px solid var(--line);
      background:#fff;color:var(--ink);font-size:13px}
    button{padding:9px 14px;border:0;border-radius:8px;background:var(--blue);
      color:#fff;font-weight:700;cursor:pointer;font-size:13px;white-space:nowrap}
    /* ── DEMO BUTTONS ── */
    .demobar{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px}
    .dbtn{padding:7px 13px;border:1px solid var(--line);border-radius:8px;
      background:#ffffffdd;cursor:pointer;font-size:13px;font-weight:600;
      color:var(--ink);white-space:nowrap}
    .dbtn:hover{background:#e8f4ff}
    .dbtn.red{background:#ffd8d8;color:#8a1c1c;border-color:#e5393522}
    .dbtn.clr{background:#d8ffe5;color:#0f6a33;border-color:#1fba6222}
    /* ── CONN BAR ── */
    .connbar{display:grid;grid-template-columns:repeat(6,1fr);gap:6px;margin-top:8px}
    .cbadge{display:block;text-align:center;padding:6px 4px;border-radius:8px;
      background:#ffffffaa;font-size:12px;border:1px solid var(--line)}
    .cbadge.live{background:#d5f7e4}.cbadge.forced{background:#fff3cd}.cbadge.off{background:#ffd4d4}
    /* ── LEGEND ── */
    .legend{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
    .lg{padding:4px 10px;border-radius:999px;font-size:12px;font-weight:700}
    .lgN{background:#d5f7e4;color:#0a4225}.lgE{background:#ffd8d8;color:#6a0f0f}
    .lgO{background:#ffe8ca;color:#6a3800}.lgY{background:#fff7b5;color:#5a4500}
    /* ── RESP ── */
    @media(max-width:700px){.grid3,.node-grid{grid-template-columns:1fr 1fr}
      .grid2{grid-template-columns:1fr}.connbar{grid-template-columns:repeat(3,1fr)}}
  </style>
</head>
<body>

<div class="topbar">
  <h1>🔥 ARIA Hub</h1>
  <div class="tabs">
    <div class="tab active" onclick="show('warden')">Warden</div>
    <div class="tab" onclick="show('tech')">Technical</div>
    <div class="tab" onclick="show('demo')">Demo / Test</div>
  </div>
</div>

<!-- ═══════════════════════════════════════
     WARDEN PAGE
═══════════════════════════════════════ -->
<div class="page active" id="page-warden">

  <div class="legend">
    <span class="lg lgN">● Green = Safe route</span>
    <span class="lg lgE">● Red = Fire — route away</span>
    <span class="lg lgO">● Orange = Congested</span>
    <span class="lg lgY">● Yellow = Gas Leak</span>
  </div>

  <!-- Zone status -->
  <div class="grid3" id="wZones"></div>

  <!-- Alert pills -->
  <div class="card">
    <div class="pills" id="wAlerts"></div>
    <div style="font-size:12px;color:var(--muted)">IR sensor: <b id="irLbl">not set</b></div>
  </div>

  <!-- SOS panel -->
  <div class="card" id="sosCard" style="border:2px solid #77b6ff">
    <h3>🆘 SOS Status</h3>
    <b id="sosHead">No SOS active</b>
    <div style="font-size:13px;color:var(--muted);margin-top:4px">Location: <span id="sosLoc">none</span></div>
  </div>

  <!-- Live floor map -->
  <div class="card">
    <h3>Live Floor Map</h3>
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

  <!-- Live escape route canvas -->
  <div class="card">
    <h3>Live Escape Route</h3>
    <canvas id="escCanvas"></canvas>
  </div>

  <!-- Warden chat (synced across all clients) -->
  <div class="card">
    <h3>Warden Chat</h3>
    <div class="logbox" id="chatBox"></div>
    <div class="inrow">
      <input type="text" id="chatIn" placeholder="Message or command (CLEAR, CONGEST A...)">
      <button onclick="sendChat()">Send</button>
    </div>
  </div>
</div>

<!-- ═══════════════════════════════════════
     TECHNICAL PAGE
═══════════════════════════════════════ -->
<div class="page" id="page-tech">

  <!-- Zone status -->
  <div class="grid3" id="tZones"></div>

  <!-- Node connections -->
  <div class="card">
    <h3>Node Connections</h3>
    <div id="connSummary" style="font-size:13px;color:var(--muted);margin-bottom:6px"></div>
    <div class="connbar" id="connBar"></div>
  </div>

  <!-- Node detail cards -->
  <div class="node-grid" id="nodeCards"></div>

  <!-- System log -->
  <div class="card">
    <h3>System Log</h3>
    <div class="logbox" id="sysLog"></div>
  </div>
</div>

<!-- ═══════════════════════════════════════
     DEMO / TEST PAGE
═══════════════════════════════════════ -->
<div class="page" id="page-demo">

  <!-- Node assignment (Option B hub-side mapping) -->
  <div class="card">
    <h3>📡 Hub Node Mapping (Demo)</h3>
    <p style="font-size:13px;color:var(--muted);margin-bottom:10px">
      Select which node ID the hub assigns to the next incoming unassigned packet.
      The physical node sends with ID=0 until <code>SETNODE</code> is typed on the node.
      This lets the hub remap it without touching the node serial.
    </p>
    <div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap">
      <select id="nodeMapSel" style="padding:8px 12px;border-radius:8px;border:1px solid var(--line);font-size:14px">
        <option value="0">-- clear mapping --</option>
        <option value="1">Node 1 — Zone A / Exit A</option>
        <option value="2">Node 2 — Zone B / Exit B</option>
        <option value="3">Node 3 — Zone B / Exit B</option>
        <option value="4">Node 4 — Zone C / Exit C</option>
        <option value="5">Node 5 — Zone A / Exit A</option>
        <option value="6">Node 6 — Zone C / Exit C</option>
      </select>
      <button onclick="applyNodeMap()">Apply Mapping</button>
      <span id="mapResult" style="font-size:13px;color:var(--green)"></span>
    </div>
    <div style="margin-top:8px;font-size:13px;color:var(--muted)">
      Current pending map: <b id="pendingMap">none</b>
    </div>
  </div>

  <!-- Quick fire scenario buttons -->
  <div class="card">
    <h3>🔥 Fire Scenarios</h3>
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

  <!-- Congestion -->
  <div class="card">
    <h3>🚶 Exit Congestion</h3>
    <div class="demobar">
      <button class="dbtn" onclick="sc('CONGEST A')">Congest A</button>
      <button class="dbtn" onclick="sc('CONGEST B')">Congest B</button>
      <button class="dbtn" onclick="sc('CONGEST C')">Congest C</button>
      <button class="dbtn" onclick="sc('CLEAR CONGEST A')">Clear A</button>
      <button class="dbtn" onclick="sc('CLEAR CONGEST B')">Clear B</button>
      <button class="dbtn" onclick="sc('CLEAR CONGEST C')">Clear C</button>
    </div>
  </div>

  <!-- Gas / SOS -->
  <div class="card">
    <h3>⚠️ Gas / SOS</h3>
    <div class="demobar">
      <button class="dbtn" onclick="sc('GAS 1 ON')">Gas Node 1</button>
      <button class="dbtn" onclick="sc('GAS 1 OFF')">Gas Node 1 OFF</button>
      <button class="dbtn" onclick="sc('SOS 1 ON')">SOS Node 1</button>
      <button class="dbtn" onclick="sc('SOS 1 OFF')">SOS Node 1 OFF</button>
    </div>
  </div>

  <!-- Custom command -->
  <div class="card">
    <h3>⌨️ Custom Command</h3>
    <p style="font-size:12px;color:var(--muted);margin-bottom:8px">
      NODE &lt;id&gt; &lt;score&gt; | SETSCORE &lt;id&gt; &lt;0-10&gt; | GAS &lt;id&gt; ON/OFF |
      SOS &lt;id&gt; ON/OFF | FLAME &lt;id&gt; ON/OFF | NODE &lt;id&gt; CLEAR |
      NODECMD &lt;id&gt; &lt;cmd&gt; | SETIR A/B/C/ALL | CLEAR | STATUS
    </p>
    <div class="inrow">
      <input type="text" id="demoCmd" placeholder="e.g. SETSCORE 2 6">
      <button onclick="sendDemoCmd()">Send</button>
    </div>
    <div class="clr" style="margin-top:8px">
      <button class="dbtn clr" onclick="sc('CLEAR')">🗑 Clear All State</button>
    </div>
  </div>
</div>

<script>
let ws, lastState = null;

// ── WebSocket ────────────────────────────────────────────────
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
  r.textContent = v === '0' ? 'Mapping cleared' : 'Hub will map next packet → Node ' + v;
  setTimeout(()=>{ r.textContent=''; }, 4000);
}

// ── Page tabs ────────────────────────────────────────────────
function show(name){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  document.querySelectorAll('.tab').forEach(t => {
    if(t.textContent.trim().toLowerCase().startsWith(name.substring(0,4)))
      t.classList.add('active');
  });
}
// Enter key support
document.addEventListener('keydown', e => {
  if(e.key === 'Enter'){
    if(document.activeElement.id === 'chatIn') sendChat();
    if(document.activeElement.id === 'demoCmd') sendDemoCmd();
  }
});

// ── Helpers ──────────────────────────────────────────────────
function cls(s){
  s = (s||'').toLowerCase();
  return s==='critical'?'critical':s==='fire'?'fire':s==='warning'?'warning':'safe';
}
function zBg(s){
  const c = cls(s);
  if(c==='critical') return ['#ffd8d8','#8a1c1c'];
  if(c==='fire')     return ['#ffd8d8','#c62828'];
  if(c==='warning')  return ['#ffe8ca','#7a4b00'];
  return ['#d5f7e4','#0a4225'];
}
function zoneHtml(zones){
  return zones.map(z => {
    let [bg,col] = zBg(z.status);
    return `<div class="zcard" style="background:${bg};border-color:${bg}">
      <div class="zscore" style="color:${col}">${z.score}</div>
      <div class="zstatus" style="color:${col}">Zone ${z.name}: ${z.status}</div>
    </div>`;
  }).join('');
}
function stripBg(status){
  const c = cls(status);
  if(c==='critical'||c==='fire') return ['#ffd8d8','#c62828'];
  if(c==='warning')              return ['#ffe8ca','#7a4b00'];
  return ['#d5f7e4','#0a4225'];
}

// ── Chat log (server-side log shown, chat messages highlighted) ──
let shownLogCount = 0;
function updateChat(logArr){
  let box = document.getElementById('chatBox');
  let items = logArr.filter(Boolean);
  // Append only new entries
  if(items.length > shownLogCount){
    for(let i = shownLogCount; i < items.length; i++){
      let div = document.createElement('div');
      let line = items[i];
      if(line.startsWith('[CHAT]')){
        div.className = 'chat';
        div.textContent = '💬 ' + line.substring(7);
      } else {
        div.textContent = '> ' + line;
      }
      box.appendChild(div);
    }
    shownLogCount = items.length;
    box.scrollTop = box.scrollHeight;
  }
}

// ── Escape route canvas ──────────────────────────────────────
let animFrame = 0;
let renderState = null;

function drawEscape(){
  animFrame++;
  let cvs = document.getElementById('escCanvas');
  if(!cvs || !renderState) { requestAnimationFrame(drawEscape); return; }

  cvs.width = cvs.offsetWidth;
  cvs.height = 200;
  let ctx = cvs.getContext('2d');
  let W = cvs.width, H = cvs.height;

  ctx.fillStyle = '#0a1628';
  ctx.fillRect(0,0,W,H);

  let s = renderState;
  let zones = s.zones || [];
  let exits = s.exits || [];

  // Zone A = left third, Zone B = centre-bottom, Zone C = right third
  // Draw each zone as a LED strip segment

  function getZoneColor(idx){
    let z = zones[idx];
    if(!z) return null;
    let st = (z.status||'').toLowerCase();
    let cong = exits[idx] && exits[idx].congested;
    if(st==='fire'||st==='critical') return '#e53935';
    if(cong) return '#ff8c00';
    if(st==='warning') return '#f9c500';
    if(st==='safe') return '#1fba62';
    return null;
  }

  // Render each zone as a row of LEDs with a moving dot
  function drawLedZone(x, y, w, h, color, dir, label){
    if(!color){ 
      ctx.strokeStyle='#1e2d44';ctx.lineWidth=1;
      ctx.strokeRect(x,y,w,h);
      ctx.fillStyle='#2a3a55';ctx.fillRect(x,y,w,h);
      ctx.fillStyle='#3a5070';ctx.font='bold 11px sans-serif';
      ctx.textAlign='center';ctx.fillText(label,x+w/2,y+h/2+4);
      return;
    }

    // Background strip
    ctx.fillStyle='#1a2840';ctx.fillRect(x,y,w,h);
    ctx.strokeStyle=color+'44';ctx.lineWidth=1;ctx.strokeRect(x,y,w,h);

    let numLeds = Math.floor(w/8);
    let ledSpacing = w / numLeds;
    let step = Math.floor(animFrame / 3) % numLeds;

    for(let i = 0; i < numLeds; i++){
      let px = x + i * ledSpacing + ledSpacing/2;
      let py = y + h/2;

      // Which led is "lit" based on direction and proximity packing
      let distFromExit = (dir === 'left') ? i : (numLeds - 1 - i);
      let spacing = distFromExit <= 2 ? 1 : distFromExit <= 5 ? 2 : distFromExit <= 8 ? 3 : 4;

      let pos = (dir === 'left') ? (numLeds-1-step) : step;
      let lit = false;
      for(let t = 0; t < 3; t++){
        let tp = (dir === 'left') ? (pos + t) : (pos - t);
        if(tp === i){ lit = true; break; }
      }

      if(lit){
        ctx.beginPath();ctx.arc(px,py,4,0,Math.PI*2);
        ctx.fillStyle=color;ctx.fill();
        // Glow
        let grd = ctx.createRadialGradient(px,py,0,px,py,10);
        grd.addColorStop(0,color+'99');grd.addColorStop(1,'transparent');
        ctx.beginPath();ctx.arc(px,py,10,0,Math.PI*2);
        ctx.fillStyle=grd;ctx.fill();
      } else {
        ctx.beginPath();ctx.arc(px,py,2,0,Math.PI*2);
        ctx.fillStyle=color+'30';ctx.fill();
      }
    }

    // Label
    ctx.fillStyle='#ffffff99';ctx.font='10px sans-serif';ctx.textAlign='center';
    ctx.fillText(label,x+w/2,y+h-3);
  }

  let zW = Math.floor(W*0.32), zH = 32;
  let zA_color = getZoneColor(0);
  let zB_color = getZoneColor(1);
  let zC_color = getZoneColor(2);

  // Determine chase direction per zone from fire logic
  let fA = zones[0] && (cls(zones[0].status)==='fire'||cls(zones[0].status)==='critical');
  let fB = zones[1] && (cls(zones[1].status)==='fire'||cls(zones[1].status)==='critical');
  let fC = zones[2] && (cls(zones[2].status)==='fire'||cls(zones[2].status)==='critical');

  let dirA = fA ? 'right' : 'left';
  let dirC = fC ? 'left' : 'right';
  let dirB = (fA && !fC) ? 'right' : (!fA && fC) ? 'left' : 'split';

  // Draw top corridor: Zone A left, Zone C right
  drawLedZone(10, 30, zW, zH, zA_color, dirA, 'Zone A');
  drawLedZone(W-zW-10, 30, zW, zH, zC_color, dirC, 'Zone C');

  // Zone B vertical strip (junction)
  let zBx = Math.floor(W/2)-15, zBw = 30, zBy = 80, zBh = 80;
  ctx.fillStyle = zB_color ? '#1a2840' : '#141e30';
  ctx.fillRect(zBx, zBy, zBw, zBh);
  if(zB_color){
    ctx.strokeStyle = zB_color+'44';ctx.lineWidth=1;ctx.strokeRect(zBx,zBy,zBw,zBh);
    let numV = Math.floor(zBh/8);
    let stepV = Math.floor(animFrame/3) % numV;
    for(let i=0;i<numV;i++){
      let py2 = zBy + i*(zBh/numV) + (zBh/numV)/2;
      let px2 = zBx + zBw/2;
      let lit = (i===stepV)||(i===(stepV+1)%numV);
      ctx.beginPath();ctx.arc(px2,py2,lit?4:2,0,Math.PI*2);
      ctx.fillStyle = lit ? zB_color : zB_color+'30'; ctx.fill();
    }
  }

  // Labels
  ctx.fillStyle='#7ab8d8';ctx.font='bold 11px sans-serif';ctx.textAlign='left';
  ctx.fillText('Exit A',10,20);
  ctx.textAlign='right';
  ctx.fillText('Exit C',W-10,20);
  ctx.textAlign='center';
  ctx.fillText('Exit B',W/2,H-8);

  // Corridor connecting lines
  ctx.strokeStyle='#1e3050';ctx.lineWidth=2;ctx.setLineDash([4,4]);
  ctx.beginPath();ctx.moveTo(10+zW,46);ctx.lineTo(W-zW-10,46);ctx.stroke();
  ctx.beginPath();ctx.moveTo(W/2,46);ctx.lineTo(W/2,80);ctx.stroke();
  ctx.setLineDash([]);

  requestAnimationFrame(drawEscape);
}

// ── Main render ──────────────────────────────────────────────
function render(s){
  renderState = s;

  // Zone cards (warden + tech)
  let zh = zoneHtml(s.zones||[]);
  document.getElementById('wZones').innerHTML = zh;
  document.getElementById('tZones').innerHTML = zh;

  // Alert pills
  let ap = `
    <span class="pill ${s.alerts.fire?'on F':''}">🔴 Fire</span>
    <span class="pill ${s.alerts.gas?'on G':''}">🟡 Gas Leak</span>
    <span class="pill ${s.alerts.sos?'on S':''}">🔵 SOS</span>`;
  document.getElementById('wAlerts').innerHTML = ap;
  document.getElementById('irLbl').textContent = s.irTarget || 'not set';

  // SOS panel
  let sosCard = document.getElementById('sosCard');
  sosCard.style.borderColor = s.alerts.sos ? '#e53935' : '#77b6ff';
  sosCard.style.background  = s.alerts.sos ? 'linear-gradient(135deg,#ffe8e8,#ffd6d6)' : '';
  document.getElementById('sosHead').textContent = s.alerts.sos ? '🆘 SOS ACTIVE' : 'No SOS active';
  document.getElementById('sosLoc').textContent  = s.sosLocations || 'none';

  // Floor map zone strips
  let za = s.zones[0], zb = s.zones[1], zc = s.zones[2];
  function applyStrip(el, z){
    let [bg,col] = zBg(z.status);
    el.style.background = bg; el.style.color = col; el.textContent = 'Zone '+z.name;
  }
  applyStrip(document.getElementById('fZA'), za);
  applyStrip(document.getElementById('fZB'), zb);
  applyStrip(document.getElementById('fZC'), zc);

  // Exit labels
  let exits = s.exits || [];
  ['A','B','C'].forEach((x,i) => {
    let el = document.getElementById('fEx'+x);
    let cong = exits[i] && exits[i].congested;
    el.style.color = cong ? 'var(--orange)' : '#5a6f81';
    el.textContent = `Exit ${x}${cong?' ⚠':''}`;
  });

  // Route text
  let rh = (s.zones||[]).map(z => {
    let st = cls(z.status), fire = st==='fire'||st==='critical';
    let i = z.name.charCodeAt(0)-65;
    let cong = exits[i] && exits[i].congested;
    let away = z.name==='A'?'→ Exit B or C':z.name==='B'?'← A or → C':'← Exit A or B';
    return `<div class="routerow"><b>Zone ${z.name}:</b> ${fire?'⚠ Move away — '+away:'→ Use Exit '+z.name}${cong?' <span style="color:var(--orange)">(Exit '+z.name+' congested)</span>':''}</div>`;
  }).join('');
  document.getElementById('routeText').innerHTML = rh;

  // Node connection bar
  let nodes = s.nodes || [];
  let live = nodes.filter(n=>n.online).length;
  let active = nodes.filter(n=>n.active).length;
  document.getElementById('connSummary').textContent = `${live} live / ${active} active`;
  document.getElementById('connBar').innerHTML = nodes.map(n =>
    `<span class="cbadge ${n.online?'live':n.forced?'forced':'off'}">
      Node ${n.id}<br><small>${n.online?'LIVE':n.forced?'FORCED':'OFFLINE'}</small>
    </span>`).join('');

  // Node detail cards
  document.getElementById('nodeCards').innerHTML = nodes.map(n => `
    <div class="ncard ${n.active?'':'offline'}">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px">
        <b>Node ${n.id}</b>
        <small class="${n.online?'safe':n.forced?'warning':'fire'}">${n.online?'● LIVE':n.forced?'● FORCED':'● OFFLINE'}</small>
      </div>
      ${n.forced?'<div style="font-size:11px;color:var(--orange);margin-bottom:4px">⚠ Forced state</div>':''}
      <div class="nrow"><span>Score</span><b class="${n.score>=8?'critical':n.score>=5?'fire':n.score>=3?'warning':'safe'}">${n.score}/10</b></div>
      <div class="nrow"><span>CO ppm</span><b>${n.co}</b></div>
      <div class="nrow"><span>Temp</span><b>${n.temp}°C</b></div>
      <div class="nrow"><span>Humidity</span><b>${n.hum}%</b></div>
      <div class="nrow"><span>Flame</span><b class="${n.flame?'fire':'safe'}">${n.flame?'YES':'no'}</b></div>
      <div class="nrow"><span>Gas</span><b class="${n.gas?'warning':'safe'}">${n.gas?'YES':'no'}</b></div>
      <div class="nrow"><span>SOS</span><b class="${n.sos?'warning':'safe'}">${n.sos?'YES':'no'}</b></div>
      <div class="nrow"><span>Last seen</span><b>${n.age>=0?n.age+'s ago':'never'}</b></div>
    </div>`).join('');

  // System log
  let logArr = (s.log||[]).filter(Boolean);
  let sl = document.getElementById('sysLog');
  sl.innerHTML = logArr.map(l=>`<div>&gt; ${l}</div>`).join('');
  sl.scrollTop = sl.scrollHeight;

  // Chat box — append new log lines including [CHAT] ones
  updateChat(logArr);

  // Demo page — pending map
  let pm = s.pendingMap || 0;
  document.getElementById('pendingMap').textContent = pm > 0 ? 'Node ' + pm : 'none';
}

requestAnimationFrame(drawEscape);
connect();
</script>
</body>
</html>
)rawliteral";
}
