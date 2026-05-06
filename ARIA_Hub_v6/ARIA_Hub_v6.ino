/*
  ARIA Hub Firmware v6
  Hardware: ESP32 DevKit C v4

  Pins:
    GPIO 5  -> WS2812B DIN, 30 pixels
    GPIO 16 -> hub buzzer
    GPIO 34 -> LDR + laser beam for Exit A (10 kΩ pull-down)
    GPIO 35 -> LDR + laser beam for Exit B (10 kΩ pull-down)
    GPIO 32 -> LDR + laser beam for Exit C (10 kΩ pull-down)

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
// Router MUST be set to channel 11 for both Telegram and ESP-NOW to work together.
// Firmware re-locks channel 11 after STA connect and every 30 s in loop().
// Fill in your credentials below on your local copy — do not commit them to a public repo.
#define STA_SSID          "Eileen"
#define STA_PASSWORD      "eileenyeoh"
#define TELEGRAM_BOT_TOKEN "8594989645:AAGVPETGPWmitPrbUhuToOWrzLNpp5Byswo"
#define TELEGRAM_CHAT_ID   "1487637387"

#define PIN_LED_STRIP     5
#define PIN_BUZZER        16
#define PIN_LDR_A         34
#define PIN_LDR_B         35
#define PIN_LDR_C         32

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


// LDR laser-break congestion parameters
#define LDR_BEAM_THRESHOLD  2000   // ADC < this = beam broken (laser not reaching LDR)
#define LDR_OBSTRUCT_MS     8000   // beam blocked > 8s = obstruction emergency
#define LDR_WINDOW_MS       30000  // 30-second person-count window
#define LDR_CONGESTION_COUNT 23    // > 23 persons in 30s = congested (~46 /min)
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
  bool beamCut;          // true = laser currently blocked
  bool congested;
  bool forced;
  bool obstruction;      // beam blocked > LDR_OBSTRUCT_MS → emergency
  unsigned long blockStart;   // when beam was first cut; 0 = not tracking
  unsigned long windowStart;  // start of current 30-s counting window
  uint16_t personCount;       // persons detected this window
  uint16_t totalPersons;      // total since boot
  bool lastBeamCut;           // previous sample for edge detection
};

enum Direction {
  DIR_LEFT,
  DIR_RIGHT,
  DIR_SPLIT,
  DIR_CONVERGE,
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

NodeState nodes[7];
ExitState exits[3] = {
  {PIN_LDR_A, false, false, false, false, 0, 0, 0, 0, false},
  {PIN_LDR_B, false, false, false, false, 0, 0, 0, 0, false},
  {PIN_LDR_C, false, false, false, false, 0, 0, 0, 0, false}
};

CRGB leds[NUM_LEDS];
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint8_t chaseStep = 0;
uint8_t pendingNodeMap = 0;
unsigned long lastAnimTime = 0;
unsigned long lastWsPush = 0;
unsigned long lastStatusPrint = 0;
unsigned long lastChRelock = 0;
static const unsigned long CH_RELOCK_MS = 30000;
volatile bool needsImmediatePush = false;
unsigned long buzzerStepStart = 0;
bool buzzerOn = false;
int lastAlertTierSent = 0;
unsigned long lastAlertNotifyTime = 0;

String logLines[12];
uint8_t logIndex = 0;
static const uint8_t CHAT_BUF = 30;
String chatLines[CHAT_BUF];
uint8_t chatWriteIdx = 0;
uint32_t chatTotal = 0;

void setupEspNow();
void sendNodeCommand(uint8_t targetNodeID, const String &command);
const char *espNowErrName(int code);
void setupDashboard();
void updateExitCongestion(unsigned long now);
void updateLdrExit(uint8_t idx, unsigned long now);
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
  } else if (packet.nodeID == 0) {
    packet.nodeID = 1;
    addLog("[MAP] Unassigned node defaulted to Node 1");
  }

  if (packet.nodeID < 1 || packet.nodeID > 6) return;

  NodeState &n = nodes[packet.nodeID];
  // Always update raw sensor readings regardless of forced state
  n.coPpm       = packet.coPpm;
  n.mq2Smooth   = packet.mq2Smooth;
  n.mq7Raw      = packet.mq7Raw;
  n.tempC       = packet.tempC;
  n.humidity    = packet.humidity;
  n.lastSeen    = millis();

  // Only overwrite fire/gas/sos state when not manually forced from hub
  if (!n.forced) {
    n.fireScore      = constrain(packet.fireScore, 0, 10);
    n.flameConfirmed = packet.flameConfirmed;
    n.gasLeakMode    = packet.gasLeakMode;
    n.sosActive      = packet.sosActive;
  }

  static unsigned long lastPacketLog[7] = {0};
  if (millis() - lastPacketLog[packet.nodeID] > 5000 || n.fireScore >= SCORE_AMBER || n.gasLeakMode || n.sosActive) {
    lastPacketLog[packet.nodeID] = millis();
    addLog(String("[NODE] Node ") + String(packet.nodeID) + " score " + String(n.fireScore) +
           (n.gasLeakMode ? " GAS" : "") + (n.sosActive ? " SOS" : ""));
  }

  needsImmediatePush = true;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LDR_A, INPUT);
  pinMode(PIN_LDR_B, INPUT);
  pinMode(PIN_LDR_C, INPUT);

  digitalWrite(PIN_BUZZER, LOW);

  FastLED.addLeds<WS2812B, PIN_LED_STRIP, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(200);
  FastLED.clear(true);

  clearAllState();
  setupEspNow();
  setupDashboard();

  if (String(STA_SSID).length() > 0) {
    WiFi.begin(STA_SSID, STA_PASSWORD);
    addLog(String("[NET] Connecting STA to ") + STA_SSID + " (router must be on ch11)");
    unsigned long staT = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - staT < 10000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
      addLog(String("[NET] STA connected, channel re-locked to ") + String(WIFI_CHANNEL));
    } else {
      addLog("[NET] STA connect timeout — ESP-NOW channel preserved");
    }
  } else {
    addLog("[NET] STA not configured — Telegram disabled");
  }

  Serial.println();
  Serial.println("ARIA Hub v6 ready");
  Serial.println("AP SSID: ARIA-HUB");
  Serial.println("AP password: aria1234");
  Serial.print("Dashboard: http://");
  Serial.println(WiFi.softAPIP());
  Serial.println("Type HELP for serial commands.");
  Serial.println("LDR laser sensors: Exit A=GPIO34  Exit B=GPIO35  Exit C=GPIO32");
  addLog(String("[HUB] Ready at http://") + WiFi.softAPIP().toString());
  addLog("[LDR] Laser-break sensors active on GPIO 34/35/32");
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

  if (needsImmediatePush) {
    needsImmediatePush = false;
    pushDashboard();
    lastWsPush = now;
  } else if (now - lastWsPush >= WS_PUSH_MS) {
    lastWsPush = now;
    pushDashboard();
  }

  if (now - lastStatusPrint >= STATUS_MS) {
    lastStatusPrint = now;
    if (anyFire() || anyGas() || anySos()) printStatus();
  }

  if (now - lastChRelock >= CH_RELOCK_MS) {
    lastChRelock = now;
    esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
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
    request->send(200, "text/html; charset=utf-8", htmlPage());
  });
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", htmlPage());
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
  for (uint8_t i = 0; i < 3; i++) {
    if (!exits[i].forced) updateLdrExit(i, now);
  }
}

void updateLdrExit(uint8_t idx, unsigned long now) {
  ExitState &e = exits[idx];
  if (e.windowStart == 0) e.windowStart = now;

  // Below threshold = laser blocked (person or object interrupting beam)
  bool cut = (analogRead(e.pin) < LDR_BEAM_THRESHOLD);
  bool wasCut = e.lastBeamCut;
  e.lastBeamCut = cut;
  e.beamCut = cut;

  // Falling edge: beam just broken — start blocking timer
  if (cut && !wasCut) {
    e.blockStart = now;
  }

  // Rising edge: beam restored — person has fully passed through
  if (!cut && wasCut) {
    if (e.blockStart > 0 && (now - e.blockStart) < LDR_OBSTRUCT_MS) {
      e.personCount++;
      e.totalPersons++;
      char ex = 'A' + idx;
      addLog(String("[LDR] Exit ") + ex + " person #" + String(e.totalPersons) + " (" + String(e.personCount) + " this window)");
    }
    e.blockStart = 0;
    if (e.obstruction) {
      e.obstruction = false;
      addLog(String("[LDR] Exit ") + char('A' + idx) + " obstruction cleared");
    }
  }

  // Prolonged blockage → obstruction emergency
  if (cut && e.blockStart > 0 && !e.obstruction && (now - e.blockStart >= LDR_OBSTRUCT_MS)) {
    e.obstruction = true;
    e.congested = true;
    addLog(String("[LDR] Exit ") + char('A' + idx) + " OBSTRUCTION — beam blocked >8s — EMERGENCY");
  }

  // 30-second window: evaluate congestion then reset counter
  if (now - e.windowStart >= LDR_WINDOW_MS) {
    if (!e.obstruction) {
      bool prev = e.congested;
      e.congested = (e.personCount > LDR_CONGESTION_COUNT);
      if (e.congested != prev) {
        addLog(String("[LDR] Exit ") + char('A' + idx) +
               (e.congested ? " CONGESTED" : " clear") +
               " (" + String(e.personCount) + " persons/30s)");
      }
    }
    e.personCount = 0;
    e.windowStart = now;
  }
}

void updateOutputs() {
  bool fire = anyFire();
  // Fan relay and ventilation handled at node level only
  // Hub focuses on exit routing and LED guidance
}

inline void buzzerTick(unsigned long now, unsigned long onMs, unsigned long offMs) {
  unsigned long phase = buzzerOn ? onMs : offMs;
  if (now - buzzerStepStart >= phase) { buzzerStepStart = now; buzzerOn = !buzzerOn; }
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
}

void updateHubBuzzer(unsigned long now) {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);
  int overall = max3(scoreA, scoreB, scoreC);
  if (overall >= SCORE_CRITICAL) { buzzerTick(now, 100, 100); return; }
  if (overall >= SCORE_FIRE)     { buzzerTick(now, 500, 500); return; }
  if (anyGas())                  { buzzerTick(now, GAS_BEEP_ON_MS, GAS_BEEP_OFF_MS); return; }
  if (anySos())                  { buzzerTick(now, SOS_BEEP_ON_MS, SOS_BEEP_OFF_MS); return; }
  buzzerOn = false;
  digitalWrite(PIN_BUZZER, LOW);
}

void updateAlertNotifications(unsigned long now) {
  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);
  int overall = max3(scoreA, scoreB, scoreC);

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

// Telegram is sent on core 0 so loop() (core 1) is never blocked by the HTTPS call
static volatile bool telegramBusy = false;

struct TelegramJob {
  char body[640];
};

static void telegramTask(void *param) {
  TelegramJob *job = (TelegramJob *)param;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendMessage";

  if (http.begin(client, url)) {
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(job->body);
    if (code == 200)     addLog("[TELEGRAM] Alert sent OK");
    else if (code > 0)   addLog(String("[TELEGRAM] Code ") + code);
    else                 addLog(String("[TELEGRAM] Failed: ") + http.errorToString(code));
    http.end();
  } else {
    addLog("[TELEGRAM] HTTP begin failed");
  }

  free(job);
  telegramBusy = false;
  vTaskDelete(NULL);
}

void sendTelegramAlert(const String &text) {
  if (String(TELEGRAM_BOT_TOKEN).length() == 0 || String(TELEGRAM_CHAT_ID).length() == 0) {
    addLog("[TELEGRAM] Not configured");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    addLog("[TELEGRAM] No STA internet connection");
    return;
  }
  if (telegramBusy) {
    addLog("[TELEGRAM] Previous send in progress, skipped");
    return;
  }

  String escaped = text;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");

  String body = "{\"chat_id\":\"";
  body += TELEGRAM_CHAT_ID;
  body += "\",\"text\":\"";
  body += escaped;
  body += "\"}";

  TelegramJob *job = (TelegramJob *)malloc(sizeof(TelegramJob));
  if (!job) { addLog("[TELEGRAM] OOM — skipped"); return; }
  body.toCharArray(job->body, sizeof(job->body));

  telegramBusy = true;
  if (xTaskCreatePinnedToCore(telegramTask, "telegram", 8192, job, 1, NULL, 0) != pdPASS) {
    addLog("[TELEGRAM] Task spawn failed");
    free(job);
    telegramBusy = false;
  } else {
    addLog("[TELEGRAM] Sending alert...");
  }
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
    render[0] = {COLOR_RED, DIR_LEFT};      // Zone A → Exit A (chase left)
    render[1] = {COLOR_RED, DIR_CONVERGE};  // Zone B → Exit B (chase inward to centre = downward)
    render[2] = {COLOR_RED, DIR_RIGHT};     // Zone C → Exit C (chase right)
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
  if (zone == 1 && zr.direction == DIR_CONVERGE) {
    drawConvergeZoneB(c);
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

void drawConvergeZoneB(CRGB color) {
  drawChaseRange(10, 14, DIR_RIGHT, color);  // left half chases right toward centre
  drawChaseRange(15, 19, DIR_LEFT, color);   // right half chases left toward centre
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
      !cmd.startsWith("CONGEST ") &&
      cmd != "CLEAR" && cmd != "STATUS") {
    addLog(String("[CHAT] ") + original);
  }
  pushDashboard();
}


void clearAllState() {
  for (uint8_t i = 0; i < 7; i++) {
    nodes[i] = NodeState{0, 0, 0, 0, false, false, false, 0.0f, 0.0f, 0, false};
  }
  for (uint8_t i = 0; i < 3; i++) {
    exits[i].beamCut     = false;
    exits[i].congested   = false;
    exits[i].forced      = false;
    exits[i].obstruction = false;
    exits[i].blockStart  = 0;
    exits[i].windowStart = millis();
    exits[i].personCount = 0;
    exits[i].lastBeamCut = false;
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
  for (uint8_t i = 0; i < 3; i++) {
    Serial.printf("Exit %c: %-9s%s%s  persons/30s=%u  total=%u\n",
      'A' + i,
      exits[i].congested ? "CONGESTED" : "clear",
      exits[i].forced      ? " forced"      : "",
      exits[i].obstruction ? " OBSTRUCTION" : "",
      exits[i].personCount,
      exits[i].totalPersons);
  }
  Serial.println("========================================");
}

bool nodeOnline(uint8_t id) {
  if (id < 1 || id > 6) return false;
  return nodes[id].lastSeen > 0 && millis() - nodes[id].lastSeen <= NODE_TIMEOUT_MS;
}

bool nodeActive(uint8_t id) {
  if (id < 1 || id > 6) return false;
  // Active if ever seen (last data is current data) OR manually forced
  return nodes[id].lastSeen > 0 || nodes[id].forced;
}

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
  if (line.startsWith("[CHAT]")) {
    chatLines[chatWriteIdx] = line;
    chatWriteIdx = (chatWriteIdx + 1) % CHAT_BUF;
    chatTotal++;
  }
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
    json += "\",\"beamCut\":";
    json += exits[i].beamCut ? "true" : "false";
    json += ",\"congested\":";
    json += exits[i].congested ? "true" : "false";
    json += ",\"obstruction\":";
    json += exits[i].obstruction ? "true" : "false";
    json += ",\"personCount\":";
    json += String(exits[i].personCount);
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
  json += "],";

  uint8_t stored = chatTotal < CHAT_BUF ? (uint8_t)chatTotal : CHAT_BUF;
  uint8_t startIdx = chatTotal < CHAT_BUF ? 0 : chatWriteIdx;
  json += "\"chatTotal\":";
  json += String(chatTotal);
  json += ",\"chat\":[";
  for (uint8_t i = 0; i < stored; i++) {
    uint8_t idx = (startIdx + i) % CHAT_BUF;
    if (i > 0) json += ",";
    String line = chatLines[idx];
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
    .ncard-hdr.live{background:#f0fdf8}.ncard-hdr.forced{background:#fffbeb}.ncard-hdr.stale{background:#fefce8}.ncard-hdr.offline{background:#fff1f2}
    .nstatus{font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.04em}
    .nstatus.live{color:var(--green)}.nstatus.forced{color:var(--orange)}.nstatus.stale{color:#ca8a04}.nstatus.offline{color:var(--red)}
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
    /* â”€â”€ MAP CANVAS â”€â”€ */
    #mapCanvas{width:100%;display:block;border-radius:10px;border:1px solid var(--border)}

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
    .cbadge.stale{background:#fef9c3;color:#a16207}
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
  <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 108" width="42" height="42" aria-label="ARIA">
  <circle cx="50" cy="62" r="43" fill="#eef1f6"/>
  <g stroke="#3d7ec9" stroke-width="1.8" fill="none">
    <line x1="50" y1="22" x2="78" y2="38"/><line x1="78" y1="38" x2="78" y2="70"/>
    <line x1="78" y1="70" x2="50" y2="86"/><line x1="50" y1="86" x2="22" y2="70"/>
    <line x1="22" y1="70" x2="22" y2="38"/><line x1="22" y1="38" x2="50" y2="22"/>
    <line x1="50" y1="22" x2="50" y2="54"/><line x1="78" y1="38" x2="50" y2="54"/>
    <line x1="78" y1="70" x2="50" y2="54"/><line x1="50" y1="86" x2="50" y2="54"/>
    <line x1="22" y1="70" x2="50" y2="54"/><line x1="22" y1="38" x2="50" y2="54"/>
  </g>
  <circle cx="22" cy="38" r="5.5" fill="#3d7ec9"/>
  <circle cx="78" cy="70" r="5.5" fill="#3d7ec9"/>
  <circle cx="50" cy="86" r="5.5" fill="#3d7ec9"/>
  <circle cx="22" cy="70" r="5.5" fill="#3d7ec9"/>
  <circle cx="50" cy="22" r="5.5" fill="#f07828"/>
  <circle cx="78" cy="38" r="5.5" fill="#f07828"/>
  <circle cx="50" cy="54" r="7"   fill="#f07828"/>
  <text x="50" y="10" text-anchor="middle" font-family="Arial,sans-serif" font-weight="900" font-size="13" fill="#f07828" letter-spacing="4">ARIA</text>
</svg>
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
  </div>

  <div class="card" id="sosCard">
    <div class="card-title">SOS Status</div>
    <b id="sosHead" style="font-size:15px">No SOS active</b>
    <div style="font-size:13px;color:var(--muted);margin-top:5px">Location: <span id="sosLoc">none</span></div>
  </div>

  <div class="card">
    <div class="card-title">Live Floor Map &amp; Escape Route</div>
    <canvas id="mapCanvas"></canvas>
    <div id="routeText" style="margin-top:10px"></div>
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

let shownChatTotal = 0;
function updateChat(chatArr, chatTotal){
  if(!chatArr) return;
  let box = document.getElementById('chatBox');
  if(chatTotal < shownChatTotal){
    box.innerHTML = '';
    shownChatTotal = 0;
  }
  if(chatTotal > shownChatTotal){
    let newCount = chatTotal - shownChatTotal;
    let startAt = Math.max(0, chatArr.length - newCount);
    for(let i = startAt; i < chatArr.length; i++){
      let line = chatArr[i];
      if(line.startsWith('[CHAT]')){
        let div = document.createElement('div');
        div.className = 'chat';
        div.textContent = '\u{1F4AC} ' + line.substring(7);
        box.appendChild(div);
        box.scrollTop = box.scrollHeight;
      }
    }
    shownChatTotal = chatTotal;
  }
}

let animFrame = 0;
let renderState = null;

function drawMap(){
  animFrame++;
  let cvs = document.getElementById('mapCanvas');
  if(!cvs || !renderState){ requestAnimationFrame(drawMap); return; }

  let s = renderState;
  let zones = s.zones || [];
  let exits = s.exits || [];

  let dpr = window.devicePixelRatio || 1;
  let W = cvs.offsetWidth;
  let H = 370;
  if(cvs.width !== Math.round(W*dpr) || cvs.height !== Math.round(H*dpr)){
    cvs.width = Math.round(W*dpr); cvs.height = Math.round(H*dpr);
    cvs.style.height = H+'px';
  }
  let ctx = cvs.getContext('2d');
  ctx.setTransform(dpr,0,0,dpr,0,0);

  ctx.fillStyle='#f8fafc'; ctx.fillRect(0,0,W,H);

  // ── Layout ────────────────────────────────────────────────────────────────
  // Exit A (left) and Exit C (right) are flush with the sides of the corridor.
  // 4 top rooms span the inner width between the exits.
  // A vertical corridor drops from the centre of the horizontal corridor to Exit B.
  // N5 (Zone A) and N6 (Zone C) sit either side of the vertical section.
  let exSideW=Math.max(62,Math.min(80,Math.floor(W*0.13)));
  let exSideH=52;
  let innerX=exSideW;
  let innerW=W-2*exSideW;
  let roomW=Math.floor(innerW/4);
  let roomH=78, roomY=16;
  let corrH=54;
  let corrY=roomY+roomH;
  let corrMidY=corrY+Math.floor(corrH/2);
  let vertW=54;
  let vertX=Math.floor(W/2-vertW/2);
  let vertY=corrY+corrH;
  let vertH=86;
  let exBW=70, exBH=36;
  let exBX=Math.floor(W/2-exBW/2), exBY=vertY+vertH;

  // Zone colours
  function zoneCol(idx){
    let z=zones[idx];
    if(!z) return {fill:'#f1f5f9',bdr:'#cbd5e1',txt:'#94a3b8',ptc:'#94a3b8',lbl:'STANDBY'};
    let st=(z.status||'').toLowerCase();
    let score=z.score!==undefined?z.score:'–';
    if(st==='fire'||st==='critical') return {fill:'#fef2f2',bdr:'#f87171',txt:'#b91c1c',ptc:'#ef4444',lbl:'FIRE ⚠',score};
    if(st==='warning') return {fill:'#fffbeb',bdr:'#fbbf24',txt:'#92400e',ptc:'#f59e0b',lbl:'WARN',score};
    if(st==='safe')    return {fill:'#f0fdf4',bdr:'#4ade80',txt:'#15803d',ptc:'#22c55e',lbl:'SAFE ✓',score};
    return {fill:'#f8fafc',bdr:'#94a3b8',txt:'#64748b',ptc:'#94a3b8',lbl:'STANDBY',score};
  }
  let zA=zoneCol(0), zB=zoneCol(1), zC=zoneCol(2);

  function rr(x,y,w,h,r){
    ctx.beginPath();
    ctx.moveTo(x+r,y);ctx.lineTo(x+w-r,y);ctx.arcTo(x+w,y,x+w,y+r,r);
    ctx.lineTo(x+w,y+h-r);ctx.arcTo(x+w,y+h,x+w-r,y+h,r);
    ctx.lineTo(x+r,y+h);ctx.arcTo(x,y+h,x,y+h-r,r);
    ctx.lineTo(x,y+r);ctx.arcTo(x,y,x+r,y,r);
    ctx.closePath();
  }

  // ── Corridors ─────────────────────────────────────────────────────────────
  ctx.fillStyle='#e2e8f0';
  ctx.fillRect(0, corrY, W, corrH);           // full-width horizontal corridor
  ctx.fillRect(vertX, vertY, vertW, vertH);   // vertical section down to Exit B

  // ── Top rooms: N1 ZoneA, N2 ZoneB, N3 ZoneB, N4 ZoneC ───────────────────
  let topRooms=[
    {x:innerX,         col:zA, lbl:'N1', zone:'A'},
    {x:innerX+roomW,   col:zB, lbl:'N2', zone:'B'},
    {x:innerX+2*roomW, col:zB, lbl:'N3', zone:'B'},
    {x:innerX+3*roomW, col:zC, lbl:'N4', zone:'C'},
  ];
  topRooms.forEach(rm=>{
    ctx.fillStyle=rm.col.fill; rr(rm.x,roomY,roomW,roomH,8); ctx.fill();
    ctx.strokeStyle=rm.col.bdr; ctx.lineWidth=2; ctx.stroke();
    ctx.textAlign='center'; ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif'; ctx.fillText(rm.lbl, rm.x+roomW/2, roomY+28);
    ctx.font='11px system-ui,sans-serif'; ctx.fillText('Zone '+rm.zone, rm.x+roomW/2, roomY+46);
    if(rm.col.score!==undefined){
      rr(rm.x+roomW/2-20,roomY+52,40,20,4); ctx.fillStyle=rm.col.bdr; ctx.fill();
      ctx.fillStyle='#fff'; ctx.font='bold 11px system-ui,sans-serif';
      ctx.fillText(rm.col.score+'/10', rm.x+roomW/2, roomY+66);
    }
  });

  // ── Lower rooms: N5 ZoneA (left of vertical), N6 ZoneC (right) ───────────
  let lRoomW=Math.min(86, Math.floor((vertX-innerX)*0.62));
  let lRoomH=66, lRoomY=corrY+corrH+10;
  let n5X=innerX+Math.floor((vertX-innerX-lRoomW)/2);
  let n6X=vertX+vertW+Math.floor((W-exSideW-vertX-vertW-lRoomW)/2);
  [{x:n5X,col:zA,lbl:'N5',zone:'A'},{x:n6X,col:zC,lbl:'N6',zone:'C'}].forEach(rm=>{
    ctx.fillStyle=rm.col.fill; rr(rm.x,lRoomY,lRoomW,lRoomH,8); ctx.fill();
    ctx.strokeStyle=rm.col.bdr; ctx.lineWidth=2; ctx.stroke();
    ctx.textAlign='center'; ctx.fillStyle=rm.col.txt;
    ctx.font='bold 15px system-ui,sans-serif'; ctx.fillText(rm.lbl, rm.x+lRoomW/2, lRoomY+26);
    ctx.font='11px system-ui,sans-serif'; ctx.fillText('Zone '+rm.zone, rm.x+lRoomW/2, lRoomY+44);
    if(rm.col.score!==undefined){
      rr(rm.x+lRoomW/2-18,lRoomY+49,36,17,4); ctx.fillStyle=rm.col.bdr; ctx.fill();
      ctx.fillStyle='#fff'; ctx.font='bold 10px system-ui,sans-serif';
      ctx.fillText(rm.col.score+'/10', rm.x+lRoomW/2, lRoomY+61);
    }
  });

  // ── Exit boxes ────────────────────────────────────────────────────────────
  let congA=exits[0]&&exits[0].congested;
  let congB=exits[1]&&exits[1].congested;
  let congC=exits[2]&&exits[2].congested;
  let fA=zones[0]&&(zones[0].status||'').match(/fire|critical/i);
  let fB=zones[1]&&(zones[1].status||'').match(/fire|critical/i);
  let fC=zones[2]&&(zones[2].status||'').match(/fire|critical/i);

  function exitBox(x,y,w,h,lbl,cong){
    ctx.fillStyle=cong?'#fef3c7':'#dcfce7';
    ctx.strokeStyle=cong?'#f59e0b':'#22c55e'; ctx.lineWidth=1.5;
    rr(x,y,w,h,6); ctx.fill(); ctx.stroke();
    ctx.fillStyle=cong?'#92400e':'#14532d'; ctx.textAlign='center';
    ctx.font='bold 10px system-ui,sans-serif'; ctx.fillText(lbl, x+w/2, y+h/2-3);
    ctx.font='9px system-ui,sans-serif'; ctx.fillText(cong?'⚠ CONGESTED':'● OPEN', x+w/2, y+h/2+9);
  }
  exitBox(0, corrMidY-exSideH/2, exSideW, exSideH, 'Exit A', congA);
  exitBox(W-exSideW, corrMidY-exSideH/2, exSideW, exSideH, 'Exit C', congC);
  exitBox(exBX, exBY, exBW, exBH, 'Exit B', congB);

  // ── Particle system: path-length-based, uniform speed ─────────────────────
  const SPEED=1.2;
  function _sL(a,b){let dx=b[0]-a[0],dy=b[1]-a[1];return Math.sqrt(dx*dx+dy*dy);}
  function _pL(p){let l=0;for(let i=0;i<p.length-1;i++)l+=_sL(p[i],p[i+1]);return l;}
  function _pt(p,dist){
    let r=dist;
    for(let i=0;i<p.length-1;i++){
      let s=_sL(p[i],p[i+1]);
      if(r<=s){let t=s>0?r/s:0;return[p[i][0]+(p[i+1][0]-p[i][0])*t,p[i][1]+(p[i+1][1]-p[i][1])*t];}
      r-=s;
    }
    return[p[p.length-1][0],p[p.length-1][1]];
  }
  function drawParticles(path,color,count){
    let total=_pL(path);if(total<1)return;
    let head=(animFrame*SPEED)%total;
    for(let i=0;i<count;i++){
      let d=(head+i*(total/count))%total;
      let dte=total-d;
      let[px,py]=_pt(path,d);
      let g=ctx.createRadialGradient(px,py,0,px,py,9);
      g.addColorStop(0,color+'cc');g.addColorStop(1,color+'00');
      ctx.fillStyle=g;ctx.beginPath();ctx.arc(px,py,9,0,Math.PI*2);ctx.fill();
      ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,3.5,0,Math.PI*2);ctx.fill();
      let tc=dte<total*0.15?4:dte<total*0.3?2:1;
      let ts=dte<total*0.15?5:8;
      for(let t=1;t<=tc;t++){
        let td=d-t*ts;if(td<0)break;
        let[tx,ty]=_pt(path,td);
        let a=Math.floor((1-t/(tc+1))*170).toString(16).padStart(2,'0');
        ctx.fillStyle=color+a;ctx.beginPath();ctx.arc(tx,ty,2.5,0,Math.PI*2);ctx.fill();
      }
    }
  }

  // ── Path waypoints ─────────────────────────────────────────────────────────
  // All paths run along the corridor centreline (corrMidY) or vertical centreline (W/2).
  // Zone A column centre in corridor: innerX + roomW/2
  // Zone B column centre: W/2 (also the vertical corridor centre)
  // Zone C column centre: W - exSideW - roomW/2
  let zA_cx = innerX + roomW/2;
  let zC_cx = W - exSideW - roomW/2;
  let midX   = W/2;                    // Zone B / vertical corridor centre x
  let exAcx  = exSideW/2;             // Exit A centre x
  let exCcx  = W - exSideW/2;         // Exit C centre x
  let exBcy  = exBY + exBH/2;         // Exit B centre y

  // Normal routes
  let pA  = [[zA_cx,corrMidY],[exAcx,corrMidY]];                               // ZoneA → left → ExitA
  let pB  = [[midX,corrMidY],[midX,exBcy]];                                    // ZoneB → down → ExitB
  let pC  = [[zC_cx,corrMidY],[exCcx,corrMidY]];                               // ZoneC → right → ExitC
  // Redirect routes
  let pAB = [[zA_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                   // ZoneA → right → down → ExitB
  let pAC = [[zA_cx,corrMidY],[exCcx,corrMidY]];                               // ZoneA → right → ExitC
  let pBA = [[midX,corrMidY],[exAcx,corrMidY]];                                // ZoneB → left → ExitA
  let pBC = [[midX,corrMidY],[exCcx,corrMidY]];                                // ZoneB → right → ExitC
  let pCB = [[zC_cx,corrMidY],[midX,corrMidY],[midX,exBcy]];                   // ZoneC → left → down → ExitB
  let pCA = [[zC_cx,corrMidY],[exAcx,corrMidY]];                               // ZoneC → left → ExitA

  let gcA=congA?'#f59e0b':'#22c55e';
  let gcB=congB?'#f59e0b':'#22c55e';
  let gcC=congC?'#f59e0b':'#22c55e';
  let allF=fA&&fB&&fC;

  // Zone A
  if(!fA)       drawParticles(pA,  gcA,       3);
  else if(allF) drawParticles(pA,  '#ef4444', 3);
  else if(!fB)  drawParticles(pAB, '#ef4444', 2);
  else          drawParticles(pAC, '#ef4444', 2);
  // Zone B
  if(!fB)       drawParticles(pB,  gcB,       3);
  else if(allF) drawParticles(pB,  '#ef4444', 3);
  else if(!fA)  drawParticles(pBA, '#ef4444', 2);
  else          drawParticles(pBC, '#ef4444', 2);
  // Zone C
  if(!fC)       drawParticles(pC,  gcC,       3);
  else if(allF) drawParticles(pC,  '#ef4444', 3);
  else if(!fB)  drawParticles(pCB, '#ef4444', 2);
  else          drawParticles(pCA, '#ef4444', 2);

  requestAnimationFrame(drawMap);
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

  let sosCard = document.getElementById('sosCard');
  sosCard.style.borderColor = s.alerts.sos ? '#c0001a' : '#93c5fd';
  sosCard.style.background  = s.alerts.sos ? 'linear-gradient(135deg,#ffe0e3,#ffd0d4)' : '';
  document.getElementById('sosHead').textContent = s.alerts.sos ? 'SOS ACTIVE' : 'No SOS active';
  document.getElementById('sosLoc').textContent  = s.sosLocations || 'none';

  let exits = s.exits||[];
  let rh = (s.zones||[]).map(z => {
    let st=cls(z.status), fire=st==='fire'||st==='critical';
    let i=z.name.charCodeAt(0)-65;
    let cong=exits[i]&&exits[i].congested;
    let own=z.name==='A'?'Exit A (left)':z.name==='B'?'Exit B (down)':'Exit C (right)';
    let away=z.name==='A'?'Exit B (down) or Exit C (right)':z.name==='B'?'Exit A (left) or Exit C (right)':'Exit A (left) or Exit B (down)';
    return '<div class="routerow"><b>Zone '+z.name+':</b> '+(fire?'Move away &mdash; '+away:'Proceed to '+own)
      +(cong?' <span style="color:var(--orange)">('+own+' congested)</span>':'')+'</div>';
  }).join('');
  document.getElementById('routeText').innerHTML = rh;

  let nodes = s.nodes||[];
  let live = nodes.filter(n=>n.online).length;
  document.getElementById('connSummary').textContent = live+' node(s) currently transmitting';
  document.getElementById('connBar').innerHTML = nodes.map(n => {
    let connected = n.online;
    let bc = connected ? 'live' : n.forced ? 'forced' : 'off';
    let bt = connected ? 'CONNECTED' : 'ONLINE';
    return '<span class="cbadge '+bc+'">Node '+n.id+'<br><small>'+bt+'</small></span>';
  }).join('');

  document.getElementById('nodeCards').innerHTML = nodes.map(n => {
    let everSeen = n.age >= 0;
    let hCls, sTxt;
    if(n.online)       { hCls = n.forced ? 'forced' : 'live';    sTxt = n.forced ? 'FORCED' : 'LIVE'; }
    else if(n.forced)  { hCls = 'forced';  sTxt = 'FORCED'; }
    else if(everSeen)  { hCls = 'stale';   sTxt = 'LAST KNOWN'; }
    else               { hCls = 'offline'; sTxt = 'NO DATA'; }
    let ageStr = n.online ? '' : everSeen ? (n.age<60 ? n.age+'s ago' : Math.floor(n.age/60)+'m ago') : '';
    let sCls = n.score>=8?'danger':n.score>=5?'danger':n.score>=3?'warn':'ok';
    let dash = !everSeen && !n.forced;
    return '<div class="ncard">'
      +'<div class="ncard-hdr '+hCls+'">'
      +'<span>Node '+n.id+(ageStr?' <small style="font-weight:400;opacity:.7">('+ageStr+')</small>':'')+'</span>'
      +'<span class="nstatus '+hCls+'">'+sTxt+'</span>'
      +'</div>'
      +'<div class="ncard-body">'
      +(dash
        ?'<div class="nrow" style="color:var(--muted);font-size:13px">Waiting for first packet&hellip;</div>'
        :'<div class="nrow"><span class="nlabel">Score</span><span class="nval '+sCls+'">'+n.score+'/10</span></div>'
        +'<div class="nrow"><span class="nlabel">CO</span><span class="nval">'+n.co+' ppm</span></div>'
        +'<div class="ndivider"></div>'
        +'<div class="nrow"><span class="nlabel">Temp</span><span class="nval">'+n.temp+'&deg;C</span></div>'
        +'<div class="nrow"><span class="nlabel">Humidity</span><span class="nval">'+n.hum+'%</span></div>'
        +'<div class="nrow"><span class="nlabel">Flame</span><span class="nval '+(n.flame?'danger':'ok')+'">'+(n.flame?'YES':'No')+'</span></div>'
        +'<div class="nrow"><span class="nlabel">Gas</span><span class="nval '+(n.gas?'warn':'ok')+'">'+(n.gas?'YES':'No')+'</span></div>'
      )
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

  updateChat(s.chat||[], s.chatTotal||0);

  let pm = s.pendingMap||0;
  document.getElementById('pendingMap').textContent = pm>0 ? 'Node '+pm : 'none';
}

requestAnimationFrame(drawMap);
connect();
</script>
</body>
</html>
)rawliteral";
}