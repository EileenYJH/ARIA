/*
  ARIA Hub Firmware v6
  Hardware: ESP32 DevKit C v4

  Pins:
    GPIO 5  -> WS2812B DIN strip 1, 30 pixels (Zone A=10, B=10, C=10)
    GPIO 17 -> WS2812B DIN strip 2, 40 pixels (Zone A=17, B=8 static, C=15)
    GPIO 16 -> hub buzzer
    GPIO 34 -> LDR + laser beam for Exit A (10 kΩ pull-down)
    GPIO 35 -> LDR + laser beam for Exit B (10 kb  Ω pull-down)
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
#define FASTLED_ALLOW_INTERRUPTS 0
#define FASTLED_INTERRUPT_RETRY_COUNT 1
#include <FastLED.h>
#include "html_page.h"

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#define WIFI_CHANNEL      6
#define AP_SSID           "ARIA-HUB"
#define AP_PASSWORD       "aria1234"
#define DNS_PORT          53
// Router MUST be set to the channel your wifi is working on for both Telegram and ESP-NOW to work together.


#define STA_SSID          "YourWiFiName"
#define STA_PASSWORD      "YourWiFiPassword"
#define TELEGRAM_BOT_TOKEN "your_bot_token_here"
#define TELEGRAM_CHAT_ID   "your_chat_id_here"

#define PIN_LED_STRIP     5
#define PIN_LED_STRIP2    17
#define PIN_BUZZER        16
#define PIN_LDR_A         34
#define PIN_LDR_B         35
#define PIN_LDR_C         32

#define NUM_LEDS          30
#define ZONE_LEN          10

// Second strip: Zone A (17) + Zone B (8, static) + Zone C (15)
#define NUM_LEDS2         40
#define ZONE_A2_LEN       17
#define ZONE_B2_LEN       8
#define ZONE_C2_LEN       15

#define SCORE_AMBER       3
#define SCORE_FIRE        6
#define SCORE_CRITICAL    8

#define NODE_TIMEOUT_MS   5000
#define ANIM_INTERVAL_MS  160
#define WS_PUSH_MS        1000
#define STATUS_MS         3000
#define ALERT_NOTIFY_COOLDOWN_MS 60000


// LDR laser-break congestion parameters
#define LDR_BEAM_THRESHOLD  2000   // default; use LDRTHRESH <n> at runtime to tune per-install
#define LDR_OBSTRUCT_MS     8000   // beam blocked > 8s = obstruction emergency
#define LDR_WINDOW_MS       30000  // 30-second person-count window
#define LDR_CONGESTION_COUNT 23    // > 23 persons in 30s = congested (~46 /min)
#define GAS_BEEP_ON_MS    200
#define GAS_BEEP_OFF_MS   300
#define SOS_DOT_MS        150
#define SOS_DASH_MS       450
#define SOS_SYMBOL_GAP_MS 150
#define SOS_GROUP_GAP_MS  450
#define SOS_REPEAT_GAP_MS 1500

#pragma pack(1)
typedef struct {
  uint8_t nodeID;
  int     mq2Smooth;
  int     mq7Raw;
  int     coPpm;
  int     mq135Smooth;
  int     fireScore;
  bool    flameConfirmed;
  bool    gasLeakMode;
  bool    sosActive;
  float   tempC;
  float   humidity;
} NodePacket;          // 26 bytes, no padding

typedef struct {
  uint8_t magic;
  uint8_t targetNodeID;
  char command[32];
} HubCommandPacket;    // 34 bytes, no padding
#pragma pack()

struct NodeState {
  int fireScore;
  int coPpm;
  int mq2Smooth;
  int mq7Raw;
  int mq135Smooth;
  bool flameConfirmed;
  bool gasLeakMode;
  bool sosActive;
  float tempC;
  float humidity;
  unsigned long lastSeen;
  bool forced;
  bool waitingForFirstPacket;
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
CRGB leds2[NUM_LEDS2];
DNSServer dnsServer;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

uint8_t chaseStep = 0;
uint8_t pendingNodeMap = 0;
int     ldrRaw[3]      = {0, 0, 0};   // last ADC reading per exit (A=0, B=1, C=2)
int     ldrThreshold   = LDR_BEAM_THRESHOLD;  // runtime-tunable via LDRTHRESH command
unsigned long lastWsPush = 0;
unsigned long lastStatusPrint = 0;
unsigned long lastChRelock = 0;
static const unsigned long CH_RELOCK_MS = 30000;
volatile bool needsImmediatePush = false;
uint32_t espnowPktRx[7] = {0};   // packets received per node slot (index = nodeID 1-6)
unsigned long buzzerStepStart = 0;
bool buzzerOn = false;

uint8_t sosMorseStep = 0;
unsigned long sosMorseStepStart = 0;
static const uint16_t SOS_DURATIONS[] = {
  SOS_DOT_MS,  SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS,  SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS,  SOS_GROUP_GAP_MS,
  SOS_DASH_MS, SOS_SYMBOL_GAP_MS,
  SOS_DASH_MS, SOS_SYMBOL_GAP_MS,
  SOS_DASH_MS, SOS_GROUP_GAP_MS,
  SOS_DOT_MS,  SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS,  SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS,  SOS_REPEAT_GAP_MS
};
static const bool SOS_OUTPUT_ON[] = {
  true, false, true, false, true, false,
  true, false, true, false, true, false,
  true, false, true, false, true, false
};
static const uint8_t SOS_STEPS = sizeof(SOS_DURATIONS) / sizeof(SOS_DURATIONS[0]);

int lastAlertTierSent = 0;
unsigned long lastAlertNotifyTime = 0;

char logLines[8][72];
uint8_t logIndex = 0;
static const uint8_t CHAT_BUF = 12;
char chatLines[CHAT_BUF][96];
uint8_t chatWriteIdx = 0;
uint32_t chatTotal = 0;

void setupEspNow();
void sendNodeCommand(uint8_t targetNodeID, const String &command);

const char *espNowErrName(int code);
void setupDashboard();
void updateExitCongestion(unsigned long now);
void updateLdrExit(uint8_t idx, unsigned long now);
void updateHubBuzzer(unsigned long now);
void updateAlertNotifications(unsigned long now);
void sendTelegramAlert(const String &text);
void renderLedRouting();
void computeZoneScores(int &scoreA, int &scoreB, int &scoreC);
void computeZoneRender(ZoneRender render[3]);
void drawZone(uint8_t zone, ZoneRender zr);
void drawChaseRange(uint8_t start, uint8_t end, Direction dir, CRGB color);
void drawPulseZone(uint8_t zone, CRGB color);
void drawZone2(uint8_t zone, ZoneRender zr);
void drawChaseRangeBuf(CRGB* buf, uint8_t start, uint8_t end, Direction dir, CRGB color);
void drawChaseRangeBuf2(CRGB* buf, uint8_t start, uint8_t end, Direction dir, CRGB color);
void drawPulseZoneBuf(CRGB* buf, uint8_t start, uint8_t end, CRGB color);
CRGB colorFor(ZoneColor c);
bool exitCongestedForZone(uint8_t zone);
void handleSerialCommands();
void runCommand(String cmd, bool fromWeb);
void printStatus();
void clearAllState();
void pushDashboard();
const String& dashboardJson();
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
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] TX failed — node out of range or wrong channel");
  }
}

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

  // If hub has a pending assignment, all unassigned packets go to that node until
  // the user sets a new one. If nothing is pending, fall back to Node 1.
  if (packet.nodeID == 0) {
    uint8_t target = (pendingNodeMap >= 1 && pendingNodeMap <= 6) ? pendingNodeMap : 1;
    if (nodes[target].lastSeen == 0) {
      addLog(String("[MAP] Unassigned node → Node ") + String(target));
    }
    packet.nodeID = target;
  }

  if (packet.nodeID < 1 || packet.nodeID > 6) return;

  NodeState &n = nodes[packet.nodeID];
  // Always update raw sensor readings regardless of forced state
  n.coPpm       = packet.coPpm;
  n.mq2Smooth   = packet.mq2Smooth;
  n.mq7Raw      = packet.mq7Raw;
  n.mq135Smooth = packet.mq135Smooth;
  n.tempC       = packet.tempC;
  n.humidity    = packet.humidity;
  n.lastSeen    = millis();
  n.waitingForFirstPacket = false;

  // Only overwrite fire/gas/sos state when not manually forced from hub
  if (!n.forced) {
    n.fireScore      = constrain(packet.fireScore, 0, 10);
    n.flameConfirmed = packet.flameConfirmed;
    n.gasLeakMode    = packet.gasLeakMode;
    n.sosActive      = packet.sosActive;
  }

  // Heartbeat counter — prints to Serial every 20 packets per node so you can
  // confirm ESP-NOW traffic is flowing without flooding the log.
  espnowPktRx[packet.nodeID]++;
  if (espnowPktRx[packet.nodeID] % 20 == 1) {
    Serial.printf("[ESP-NOW] Node %u alive — %u pkts rx, score %d\n",
                  packet.nodeID, espnowPktRx[packet.nodeID], n.fireScore);
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

  FastLED.addLeds<WS2812B, PIN_LED_STRIP, GRB>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, PIN_LED_STRIP2, GRB>(leds2, NUM_LEDS2)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(200);
  FastLED.setMaxRefreshRate(120);
  FastLED.clear(true);

  clearAllState();
  setupEspNow();
  // Disable WiFi power-save: keeps radio always-on and eliminates the burst of
  // periodic wake-up interrupts that corrupt WS2812B timing in AP+STA mode.
  esp_wifi_set_ps(WIFI_PS_NONE);
  setupDashboard();

  if (String(STA_SSID).length() > 0) {
    WiFi.begin(STA_SSID, STA_PASSWORD);
    addLog(String("[NET] Connecting STA to ") + STA_SSID + " (router must be on ch6)");
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

  ws.cleanupClients(8);
  dnsServer.processNextRequest();
  handleSerialCommands();
  updateExitCongestion(now);
  updateHubBuzzer(now);
  updateAlertNotifications(now);
  renderLedRouting();

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
    if (String(STA_SSID).length() > 0) {
      wl_status_t wlSt = WiFi.status();
      // Only retry when definitively failed/dropped — not while already connecting
      if (wlSt == WL_DISCONNECTED || wlSt == WL_CONNECT_FAILED ||
          wlSt == WL_CONNECTION_LOST || wlSt == WL_NO_SSID_AVAIL) {
        WiFi.begin(STA_SSID, STA_PASSWORD);
        addLog("[NET] STA reconnecting...");
      }
    }
  }
}

void setupEspNow() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_CHANNEL, 0, 8);
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
  esp_now_register_send_cb(onDataSent);
  Serial.print("[ESP-NOW] Ready. Hub MAC: ");
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
    request->send_P(200, "text/html", HTML_PAGE);
  });
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", HTML_PAGE);
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

  int  adcVal = analogRead(e.pin);
  ldrRaw[idx] = adcVal;
  bool cut = (adcVal < ldrThreshold);
  bool wasCut = e.lastBeamCut;
  e.lastBeamCut = cut;
  e.beamCut = cut;

  // Falling edge: beam just broken — start blocking timer
  if (cut && !wasCut) {
    e.blockStart = now;
  }

  // Rising edge: beam restored — person has fully passed through
  if (!cut && wasCut && e.blockStart > 0) {
    unsigned long dur = now - e.blockStart;
    // 50 ms minimum debounce (reject noise); must clear before 8-s obstruction threshold
    if (dur >= 50 && dur < LDR_OBSTRUCT_MS) {
      e.personCount++;
      e.totalPersons++;
      addLog(String("[LDR] Exit ") + char('A'+idx) +
             " person #" + String(e.totalPersons) +
             " (" + String(e.personCount) + " this window)");
      // Trigger congestion immediately the instant the threshold is crossed
      // — don't wait for the 30-s boundary; the window is used to CLEAR congestion
      if (!e.congested && !e.obstruction && e.personCount > LDR_CONGESTION_COUNT) {
        e.congested = true;
        needsImmediatePush = true;
        addLog(String("[LDR] Exit ") + char('A'+idx) +
               " CONGESTED (" + String(e.personCount) + " persons this window)");
      }
    }
    e.blockStart = 0;
    if (e.obstruction) {
      e.obstruction = false;
      needsImmediatePush = true;
      addLog(String("[LDR] Exit ") + char('A'+idx) + " obstruction cleared");
    }
  }

  // Prolonged blockage → obstruction emergency
  if (cut && e.blockStart > 0 && !e.obstruction && (now - e.blockStart >= LDR_OBSTRUCT_MS)) {
    e.obstruction = true;
    e.congested = true;
    needsImmediatePush = true;
    addLog(String("[LDR] Exit ") + char('A'+idx) + " OBSTRUCTION — beam blocked >8s — EMERGENCY");
  }

  // 30-second window: re-evaluate to CLEAR congestion if rate has dropped, then reset counter
  if (now - e.windowStart >= LDR_WINDOW_MS) {
    if (!e.obstruction) {
      bool prev = e.congested;
      e.congested = (e.personCount > LDR_CONGESTION_COUNT);
      if (e.congested != prev) {
        needsImmediatePush = true;
        addLog(String("[LDR] Exit ") + char('A'+idx) +
               (e.congested ? " CONGESTED" : " clear") +
               " (" + String(e.personCount) + " persons/30s)");
      }
    }
    e.personCount = 0;
    e.windowStart = now;
  }
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

  if (anySos()) {
    if (now - sosMorseStepStart >= SOS_DURATIONS[sosMorseStep]) {
      digitalWrite(PIN_BUZZER, LOW);
      sosMorseStepStart = now;
      sosMorseStep = (sosMorseStep + 1) % SOS_STEPS;
    }
    digitalWrite(PIN_BUZZER, SOS_OUTPUT_ON[sosMorseStep] ? HIGH : LOW);
    return;
  }

  sosMorseStep = 0;
  sosMorseStepStart = now;
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

  auto ns = [](uint8_t id) -> String {
    return nodeOnline(id) ? String(nodes[id].fireScore) : String("-");
  };

  String msg = "ARIA FIRE ALERT\n";
  if (tier == 3) msg += "CRITICAL (Tier 3) — Warden contact BOMBA 999\n";
  else if (tier == 2) msg += "EMERGENCY (Tier 2) — Evacuate now\n";
  else msg += "PRE-WARNING (Tier 1) — Investigate\n";

  msg += "\nZone A: " + String(scoreA) + "/10 [" + statusForScore(scoreA) + "]  N1:" + ns(1) + " N5:" + ns(5) + "\n";
  msg += "Zone B: " + String(scoreB) + "/10 [" + statusForScore(scoreB) + "]  N2:" + ns(2) + " N3:" + ns(3) + "\n";
  msg += "Zone C: " + String(scoreC) + "/10 [" + statusForScore(scoreC) + "]  N4:" + ns(4) + " N6:" + ns(6) + "\n";

  msg += "\nExit A: " + String(exits[0].congested ? "CONGESTED" : "clear") + "\n";
  msg += "Exit B: " + String(exits[1].congested ? "CONGESTED" : "clear") + "\n";
  msg += "Exit C: " + String(exits[2].congested ? "CONGESTED" : "clear");
  if (anySos())  msg += "\n\nSOS: " + sosLocations();
  if (anyGas())  msg += "\nGas: " + gasLocations();

  msg += "\n\nStay calm. Follow green exit signs. Do not use lifts. Assist those who need help.";
  msg += "\nCall BOMBA: 999";

  sendTelegramAlert(msg);
  lastAlertTierSent = tier;
  lastAlertNotifyTime = now;
}

// Telegram is sent on core 0 so loop() (core 1) is never blocked by the HTTPS call
static volatile bool telegramBusy = false;

struct TelegramJob {
  char body[900];
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
    render[1] = {COLOR_YELLOW, DIR_RIGHT};
    render[2] = {COLOR_YELLOW, DIR_RIGHT};
    return;
  }

  if (warningOnly) {
    for (uint8_t i = 0; i < 3; i++) render[i] = {COLOR_ORANGE, DIR_PULSE};
    return;
  }

  if (!fireA && !fireB && !fireC) return;

  if (fireA && fireB && fireC) {
    render[0] = {COLOR_RED, DIR_LEFT};    // Zone A → try Exit A
    render[1] = {COLOR_RED, DIR_RIGHT};   // Zone B → try Exit B
    render[2] = {COLOR_RED, DIR_RIGHT};   // Zone C → try Exit C
  } else if (fireA && fireC) {
    render[0] = {COLOR_RED, DIR_RIGHT};   // Zone A → corridor → Exit B
    render[1] = {COLOR_GREEN, DIR_RIGHT}; // Zone B → Exit B (only safe exit)
    render[2] = {COLOR_RED, DIR_LEFT};    // Zone C → corridor → Exit B
  } else if (fireA && fireB) {
    render[0] = {COLOR_RED, DIR_RIGHT};   // Zone A → corridor → Exit C
    render[1] = {COLOR_RED, DIR_LEFT};    // Zone B → back to corridor → Exit C
    render[2] = {COLOR_GREEN, DIR_RIGHT}; // Zone C → Exit C
  } else if (fireB && fireC) {
    render[0] = {COLOR_GREEN, DIR_LEFT};  // Zone A → Exit A
    render[1] = {COLOR_RED, DIR_LEFT};    // Zone B → back to corridor → Exit A
    render[2] = {COLOR_RED, DIR_LEFT};    // Zone C → corridor → Exit A
  } else if (fireA) {
    render[0] = {COLOR_RED, DIR_RIGHT};   // Zone A → corridor → Exit B or C
    render[1] = {COLOR_GREEN, DIR_RIGHT}; // Zone B → Exit B (safe)
    render[2] = {COLOR_GREEN, DIR_RIGHT}; // Zone C → Exit C (safe)
  } else if (fireB) {
    render[0] = {COLOR_GREEN, DIR_LEFT};  // Zone A → Exit A (safe)
    render[1] = {COLOR_RED, DIR_LEFT};    // Zone B → back to corridor → Exit A or C
    render[2] = {COLOR_GREEN, DIR_RIGHT}; // Zone C → Exit C (safe)
  } else if (fireC) {
    render[0] = {COLOR_GREEN, DIR_LEFT};  // Zone A → Exit A (safe)
    render[1] = {COLOR_GREEN, DIR_RIGHT}; // Zone B → Exit B (safe)
    render[2] = {COLOR_RED, DIR_LEFT};    // Zone C → corridor → Exit A or B
  }

  for (uint8_t z = 0; z < 3; z++) {
    if (render[z].color == COLOR_GREEN && exitCongestedForZone(z)) {
      render[z].color = COLOR_ORANGE;
    }
  }
}

void renderLedRouting() {
  EVERY_N_MILLISECONDS(ANIM_INTERVAL_MS) {
    chaseStep++;

    int scoreA, scoreB, scoreC;
    computeZoneScores(scoreA, scoreB, scoreC);
    if (anyGas() && max3(scoreA, scoreB, scoreC) < SCORE_AMBER) {
      fill_solid(leds,  NUM_LEDS,  CRGB(255, 255, 255));
      fill_solid(leds2, NUM_LEDS2, CRGB(255, 255, 255));
    } else {
      ZoneRender render[3];
      computeZoneRender(render);
      fill_solid(leds,  NUM_LEDS,  CRGB::Black);
      fill_solid(leds2, NUM_LEDS2, CRGB::Black);
      for (uint8_t z = 0; z < 3; z++) drawZone(z, render[z]);
      for (uint8_t z = 0; z < 3; z++) drawZone2(z, render[z]);
    }

    // Only push to the strips if the pixel data actually changed.
    // Skipping FastLED.show() when nothing moved eliminates the 800 kHz
    // bit-bang window that blocks ISRs, reducing interference with
    // AsyncWebSocket and ESP-NOW during idle / gas-hold / all-off states.
    static CRGB prevLeds[NUM_LEDS];
    static CRGB prevLeds2[NUM_LEDS2];
    bool changed = memcmp(leds,  prevLeds,  NUM_LEDS  * sizeof(CRGB)) != 0
                || memcmp(leds2, prevLeds2, NUM_LEDS2 * sizeof(CRGB)) != 0;
    if (changed) {
      memcpy(prevLeds,  leds,  NUM_LEDS  * sizeof(CRGB));
      memcpy(prevLeds2, leds2, NUM_LEDS2 * sizeof(CRGB));
      FastLED.show();
    }
  }
}

void drawZone(uint8_t zone, ZoneRender zr) {
  if (zr.color == COLOR_OFF || zr.direction == DIR_OFF) return;
  CRGB c = colorFor(zr.color);

  if (zr.direction == DIR_PULSE) {
    drawPulseZone(zone, c);
    return;
  }

  uint8_t start = zone * ZONE_LEN;
  uint8_t end = start + ZONE_LEN - 1;
  drawChaseRange(start, end, zr.direction, c);
}

void drawChaseRange(uint8_t start, uint8_t end, Direction dir, CRGB color) {
  int len = (int)end - (int)start + 1;
  if (len <= 0) return;

  int progress = chaseStep % len;
  int lead = (dir == DIR_LEFT)
    ? ((int)end - progress)
    : ((int)start + progress);

  int exitPos    = (dir == DIR_LEFT) ? (int)start : (int)end;
  int distToExit = abs(lead - exitPos);

  // Packet size: 1 LED when far from exit, grows to 2 then 3 as it closes in
  int pktSize = (distToExit <= 2) ? 3 : (distToExit <= 4) ? 2 : 1;

  // All pixels in the packet are full brightness — trailing pixels sit behind the lead
  for (int p = 0; p < pktSize; p++) {
    int pos = (dir == DIR_LEFT) ? (lead + p) : (lead - p);
    if (pos >= (int)start && pos <= (int)end) leds[pos] = color;
  }
}

void drawPulseZone(uint8_t zone, CRGB color) {
  uint8_t start = zone * ZONE_LEN;
  uint8_t end = start + ZONE_LEN - 1;
  uint8_t brightness = beatsin8(30, 45, 255);
  CRGB pulse = color;
  pulse.nscale8_video(brightness);
  for (uint8_t i = start; i <= end; i++) leds[i] = pulse;
}

// Buffer-generic variants used by strip 2
void drawChaseRangeBuf(CRGB* buf, uint8_t start, uint8_t end, Direction dir, CRGB color) {
  int len = (int)end - (int)start + 1;
  if (len <= 0) return;
  int progress = chaseStep % len;
  int lead = (dir == DIR_LEFT)
    ? ((int)end - progress)
    : ((int)start + progress);
  int exitPos    = (dir == DIR_LEFT) ? (int)start : (int)end;
  int distToExit = abs(lead - exitPos);
  int pktSize = (distToExit <= 2) ? 3 : (distToExit <= 4) ? 2 : 1;
  for (int p = 0; p < pktSize; p++) {
    int pos = (dir == DIR_LEFT) ? (lead + p) : (lead - p);
    if (pos >= (int)start && pos <= (int)end) buf[pos] = color;
  }
}

void drawPulseZoneBuf(CRGB* buf, uint8_t start, uint8_t end, CRGB color) {
  uint8_t brightness = beatsin8(30, 45, 255);
  CRGB pulse = color;
  pulse.nscale8_video(brightness);
  for (uint8_t i = start; i <= end; i++) buf[i] = pulse;
}

// Strip 2: Zone A (0–16) chase | Zone B (17–24) static colour | Zone C (25–39) chase
void drawZone2(uint8_t zone, ZoneRender zr) {
  if (zr.color == COLOR_OFF || zr.direction == DIR_OFF) return;

  if (zone == 0) {
    uint8_t s = 0, e = ZONE_A2_LEN - 1;
    if (zr.direction == DIR_PULSE) drawPulseZoneBuf(leds2, s, e, colorFor(zr.color));
    else                           drawChaseRangeBuf2(leds2, s, e, zr.direction, colorFor(zr.color));

  } else if (zone == 1) {
    // Zone B on strip 2: slow-pulsing status colour so it reads as a separate indicator,
    // not a stray LED bleeding into Zone A or C.  red / yellow / green only.
    CRGB sc = (zr.color == COLOR_RED)    ? CRGB(220, 0,   0) :
              (zr.color == COLOR_GREEN)  ? CRGB(0,   180, 0) :
              (zr.color == COLOR_YELLOW) ? CRGB(255, 255, 255) :  // gas leak → white
                                           CRGB(255, 140, 0);     // warning/amber → orange
    uint8_t bri = beatsin8(18, 60, 255);   // gentle 18 BPM breathe
    sc.nscale8_video(bri);
    fill_solid(leds2 + ZONE_A2_LEN, ZONE_B2_LEN, sc);

  } else if (zone == 2) {
    uint8_t s = ZONE_A2_LEN + ZONE_B2_LEN;
    uint8_t e = s + ZONE_C2_LEN - 1;
    if (zr.direction == DIR_PULSE) drawPulseZoneBuf(leds2, s, e, colorFor(zr.color));
    else                           drawChaseRangeBuf2(leds2, s, e, zr.direction, colorFor(zr.color));
  }
}

// Strip 2 chase: base packet 3 LEDs, grows to 5 near the exit
void drawChaseRangeBuf2(CRGB* buf, uint8_t start, uint8_t end, Direction dir, CRGB color) {
  int len = (int)end - (int)start + 1;
  if (len <= 0) return;
  int progress = chaseStep % len;
  int lead = (dir == DIR_LEFT) ? ((int)end - progress) : ((int)start + progress);
  int exitPos    = (dir == DIR_LEFT) ? (int)start : (int)end;
  int distToExit = abs(lead - exitPos);
  int pktSize    = (distToExit <= 2) ? 5 : (distToExit <= 5) ? 4 : 3;
  for (int p = 0; p < pktSize; p++) {
    int pos = (dir == DIR_LEFT) ? (lead + p) : (lead - p);
    if (pos >= (int)start && pos <= (int)end) buf[pos] = color;
  }
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
      int newId = rest.toInt();
      if (newId >= 1 && newId <= 6) {
        // Reset the node currently holding the old ID so it goes back to ID=0,
        // then it will be picked up by the new pendingNodeMap on its next packet.
        if (pendingNodeMap >= 1 && pendingNodeMap <= 6 && pendingNodeMap != (uint8_t)newId) {
          for (uint8_t r = 0; r < 3; r++) sendNodeCommand(pendingNodeMap, "SETNODE 0");
        }
        pendingNodeMap = (uint8_t)newId;
        // Clear stale values from the target slot immediately — prevents "last seen jump"
        // on the dashboard and forces nodeOnline() to return false right away.
        nodes[newId] = NodeState{0, 0, 0, 0, 0, false, false, false, 0.0f, 0.0f, 0, false, true};
        needsImmediatePush = true;
        // Send 3× so a single dropped ESP-NOW packet doesn't block the mapping
        for (uint8_t r = 0; r < 3; r++) sendNodeCommand(0, String("SETNODE ") + String(newId));
        addLog(String("[MAP] Mapping → Node ") + String(newId));
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
  } else if (cmd.startsWith("CLEAR OBSTRUCTION ")) {
    char e = cmd.charAt(18);
    if (e >= 'A' && e <= 'C') {
      exits[e - 'A'].obstruction = false;
      exits[e - 'A'].congested   = false;
      exits[e - 'A'].blockStart  = 0;
      needsImmediatePush = true;
      addLog(String("[CMD] Exit ") + e + " obstruction cleared");
    }
  } else if (cmd.startsWith("LDRTHRESH ")) {
    int val = cmd.substring(10).toInt();
    if (val > 0 && val <= 4095) {
      ldrThreshold = val;
      addLog(String("[LDR] Threshold → ") + String(val) +
             "  (A:" + String(ldrRaw[0]) + " B:" + String(ldrRaw[1]) + " C:" + String(ldrRaw[2]) + ")");
    } else {
      addLog("[LDR] Use LDRTHRESH <1-4095>. Current raw — A:" + String(ldrRaw[0]) +
             " B:" + String(ldrRaw[1]) + " C:" + String(ldrRaw[2]));
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
    Serial.println("  CLEAR OBSTRUCTION A/B/C");
    Serial.println("  LDRTHRESH <0-4095> set beam-break ADC threshold (default 2000)");
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
      !cmd.startsWith("CONGEST ") && !cmd.startsWith("CLEAR ") &&
      cmd != "CLEAR" && cmd != "STATUS") {
    addLog(String("[CHAT] ") + original);
  }
  pushDashboard();
}


void clearAllState() {
  for (uint8_t i = 0; i < 7; i++) {
    nodes[i] = NodeState{0, 0, 0, 0, 0, false, false, false, 0.0f, 0.0f, 0, false, false};
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
    Serial.printf("Node %u: score=%d co=%d mq2=%d mq135=%d gas=%s sos=%s online=%s active=%s forced=%s\n",
      i, nodes[i].fireScore, nodes[i].coPpm, nodes[i].mq2Smooth, nodes[i].mq135Smooth,
      nodes[i].gasLeakMode ? "YES" : "no",
      nodes[i].sosActive ? "YES" : "no",
      nodeOnline(i) ? "YES" : "no",
      nodeActive(i) ? "YES" : "no",
      nodes[i].forced ? "YES" : "no");
  }
  Serial.printf("LDR raw — A:%d  B:%d  C:%d  threshold:%d\n",
    ldrRaw[0], ldrRaw[1], ldrRaw[2], ldrThreshold);
  for (uint8_t i = 0; i < 3; i++) {
    Serial.printf("Exit %c: %-9s%s%s  beam=%s  ldr=%d  persons/30s=%u  total=%u\n",
      'A' + i,
      exits[i].congested ? "CONGESTED" : "clear",
      exits[i].forced      ? " forced"      : "",
      exits[i].obstruction ? " OBSTRUCTION" : "",
      exits[i].beamCut  ? "CUT"    : "OK",
      ldrRaw[i],
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
  line.toCharArray(logLines[logIndex], sizeof(logLines[0]));
  logIndex = (logIndex + 1) % 8;
  Serial.println(line);
  if (line.startsWith("[CHAT]")) {
    line.toCharArray(chatLines[chatWriteIdx], sizeof(chatLines[0]));
    chatWriteIdx = (chatWriteIdx + 1) % CHAT_BUF;
    chatTotal++;
  }
}

const String& dashboardJson() {
  static String json;
  json.reserve(1500);
  json = "";

  int scoreA, scoreB, scoreC;
  computeZoneScores(scoreA, scoreB, scoreC);

  json = "{\"zn\":[";
  json += "{\"name\":\"A\",\"score\":"; json += scoreA; json += ",\"status\":\""; json += statusForScore(scoreA); json += "\"},";
  json += "{\"name\":\"B\",\"score\":"; json += scoreB; json += ",\"status\":\""; json += statusForScore(scoreB); json += "\"},";
  json += "{\"name\":\"C\",\"score\":"; json += scoreC; json += ",\"status\":\""; json += statusForScore(scoreC); json += "\"}],";

  json += "\"nd\":[";
  for (uint8_t i = 1; i <= 6; i++) {
    if (i > 1) json += ',';
    json += "{\"id\":"; json += i;
    json += ",\"score\":"; json += nodes[i].fireScore;
    json += ",\"co\":";   json += nodes[i].coPpm;
    json += ",\"mq2\":";  json += nodes[i].mq2Smooth;
    json += ",\"mq7\":";  json += nodes[i].mq7Raw;
    json += ",\"mq135\":"; json += nodes[i].mq135Smooth;
    json += ",\"temp\":"; json += String(nodes[i].tempC, 1);
    json += ",\"hum\":";  json += String(nodes[i].humidity, 1);
    json += ",\"fl\":";   json += nodes[i].flameConfirmed ? "true" : "false";
    json += ",\"gas\":";  json += nodes[i].gasLeakMode    ? "true" : "false";
    json += ",\"sos\":";  json += nodes[i].sosActive       ? "true" : "false";
    json += ",\"on\":";   json += nodeOnline(i)             ? "true" : "false";
    json += ",\"av\":";   json += nodeActive(i)             ? "true" : "false";
    json += ",\"fc\":";   json += nodes[i].forced           ? "true" : "false";
    json += ",\"age\":";
    if (nodes[i].lastSeen > 0) json += (millis() - nodes[i].lastSeen) / 1000;
    else json += "-1";
    json += ",\"pk\":"; json += espnowPktRx[i];
    json += ",\"wt\":"; json += nodes[i].waitingForFirstPacket ? "true" : "false";
    json += '}';
  }
  json += "],";

  json += "\"ex\":[";
  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) json += ',';
    json += "{\"name\":\""; json += (char)('A' + i);
    json += "\",\"bc\":";   json += exits[i].beamCut     ? "true" : "false";
    json += ",\"cg\":";     json += exits[i].congested   ? "true" : "false";
    json += ",\"ob\":";     json += exits[i].obstruction ? "true" : "false";
    json += ",\"pc\":";     json += exits[i].personCount;
    json += ",\"fc\":";     json += exits[i].forced      ? "true" : "false";
    json += ",\"ldr\":";    json += ldrRaw[i];
    json += '}';
  }
  json += "],";

  json += "\"al\":{\"fire\":"; json += anyFire() ? "true" : "false";
  json += ",\"gas\":";          json += anyGas()  ? "true" : "false";
  json += ",\"sos\":";          json += anySos()  ? "true" : "false";
  json += "},";

  json += "\"sosL\":\""; json += sosLocations(); json += "\",";
  json += "\"gasL\":\""; json += gasLocations(); json += "\",";

  json += "\"pm\":"; json += pendingNodeMap; json += ',';

  json += "\"lg\":[";
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t idx = (logIndex + i) % 8;
    if (i > 0) json += ',';
    json += '"';
    for (const char *p = logLines[idx]; *p; p++) {
      if (*p == '"') json += '\\';
      json += *p;
    }
    json += '"';
  }
  json += "],";

  uint8_t stored   = chatTotal < CHAT_BUF ? (uint8_t)chatTotal : CHAT_BUF;
  uint8_t startIdx = chatTotal < CHAT_BUF ? 0 : chatWriteIdx;
  json += "\"ct\":"; json += chatTotal; json += ",\"ch\":[";
  for (uint8_t i = 0; i < stored; i++) {
    uint8_t idx = (startIdx + i) % CHAT_BUF;
    if (i > 0) json += ',';
    json += '"';
    for (const char *p = chatLines[idx]; *p; p++) {
      if (*p == '"') json += '\\';
      json += *p;
    }
    json += '"';
  }
  json += "]}";
  return json;
}

void pushDashboard() {
  ws.textAll(dashboardJson());
}



