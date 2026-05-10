/*
  ARIA Sensor Node Firmware v6
  Hardware: ESP32 DevKit C v4

  Pins:
    GPIO 14 -> DHT22
    GPIO 25 -> flame stand-in digital sensor, LOW = flame
    GPIO 26 -> SOS button, INPUT_PULLUP
    GPIO 27 -> red strobe LED
    GPIO 19 -> relay 5V (DC fan + indicator LED in same circuit)
    GPIO 13 -> buzzer
    GPIO 23 -> SOS LED
    GPIO 33 -> MQ-7 analog
    GPIO 34 -> MQ-2 analog
    GPIO 35 -> MQ-135 analog

  Required libraries:
    WiFi.h, esp_now.h, DHT.h
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <DHT.h>

#define PIN_DHT      14
#define PIN_FLAME    25
#define PIN_SOS      26
#define PIN_STROBE   27
#define PIN_FAN      19
#define PIN_BUZZER   13
#define PIN_SOS_LED  23
#define PIN_MQ7      33
#define PIN_MQ2      34
#define PIN_MQ135    35

#define WIFI_CHANNEL 11

#define CALIB_SAMPLES       30
#define CALIB_INTERVAL_MS   100

#define MQ2_CLEAN_OFFSET    150
#define MQ2_SPIKE_OFFSET    400
#define MQ2_GASLEAK_OFFSET  200
#define MQ2_SPIKE_FLOOR     200
#define MQ2_GASLEAK_FLOOR   200

#define MQ135_CLEAN_OFFSET  200   // above baseline → slightly poor air
#define MQ135_POOR_OFFSET   500   // above baseline → poor air quality
#define MQ135_HAZARD_OFFSET 900   // above baseline → hazardous air
#define MQ135_POOR_FLOOR    500   // absolute floor for "poor" trigger
#define MQ135_HAZARD_FLOOR  900   // absolute floor for "hazardous" trigger

#define CO_PPM_SAFE         25
#define CO_PPM_CONCERN      35
#define CO_PPM_DANGEROUS    50
#define CO_PPM_FIRE         70
#define CO_PPM_CRITICAL     150

#define HUM_DROP_THRESHOLD  3.0f
#define HUM_RISE_THRESHOLD  3.0f
#define HUM_VENT_ON         75.0f
#define HUM_VENT_OFF        70.0f
#define TEMP_VENT_ON        30.0f
#define TEMP_VENT_OFF       28.5f

#define SCORE_AMBER         3
#define SCORE_FIRE          5
#define SCORE_CRITICAL      8

#define SENSOR_INTERVAL_MS  500
#define SEND_INTERVAL_MS    500
#define REPORT_INTERVAL_MS  2000
#define STROBE_INTERVAL_MS  125
#define BUTTON_DEBOUNCE_MS  40

#define SOS_DOT_MS          150
#define SOS_DASH_MS         450
#define SOS_SYMBOL_GAP_MS   150
#define SOS_GROUP_GAP_MS    450
#define SOS_REPEAT_GAP_MS   1500

#define GAS_BEEP_ON_MS      200
#define GAS_BEEP_OFF_MS     300

#define HISTORY_SIZE        8
#define FLAME_CONFIRM_READS 3
#define GAS_CONFIRM_READS   3
#define GAS_CLEAR_READS     3

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
} NodePacket;

typedef struct {
  uint8_t magic;
  uint8_t targetNodeID;
  char command[32];
} HubCommandPacket;

struct SensorData {
  int mq2Raw;
  int mq7Raw;
  int mq2Smooth;
  int coPpm;
  int mq135Raw;
  int mq135Smooth;
  float tempC;
  float humidity;
  bool flameConfirmed;
};

enum AlertPriority {
  ALERT_IDLE,
  ALERT_SOS,
  ALERT_GAS,
  ALERT_FIRE
};

DHT dht(PIN_DHT, DHT11);

uint8_t hubBroadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t nodeID = 0;
bool espNowReady = false;
bool calibrationActive = false;
bool forceFireMode = false;

int mq2Baseline = 400;
int mq7Baseline = 200;
int mq2SlightSmoke = 550;
int mq2Spike = 800;
int mq2GasLeak = 1200;

int mq135Baseline = 300;
int mq135SlightPoor = 500;
int mq135Poor = 800;
int mq135Hazard = 1200;

float mq2Ema = 400.0f;
float mq135Ema = 300.0f;

float humidityHistory[HISTORY_SIZE];
int coHistory[HISTORY_SIZE];
int histIdx = 0;

int fireScore = 0;
int prevScore = -1;
bool gasLeakMode = false;
bool fanOn = false;
bool strobeActive = false;
bool strobeState = false;
bool sosActive = false;

uint8_t flameLowCount = 0;
uint8_t flameHighCount = 0;
bool flameConfirmed = false;

uint8_t gasHitCount = 0;
uint8_t gasClearCount = 0;

unsigned long lastSensorTime = 0;
unsigned long lastSendTime = 0;
unsigned long lastReportTime = 0;
unsigned long lastStrobeTime = 0;
unsigned long lastCalibSampleTime = 0;
unsigned long lastGasMsgTime = 0;
unsigned long lastFireMsgTime = 0;

long calibMq2Sum = 0;
long calibMq7Sum = 0;
long calibMq135Sum = 0;
uint8_t calibSamples = 0;

bool sosButtonStable = HIGH;
bool sosButtonLastReading = HIGH;
unsigned long sosButtonLastChange = 0;

const uint16_t sosDurations[] = {
  SOS_DOT_MS, SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS, SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS, SOS_GROUP_GAP_MS,
  SOS_DASH_MS, SOS_SYMBOL_GAP_MS,
  SOS_DASH_MS, SOS_SYMBOL_GAP_MS,
  SOS_DASH_MS, SOS_GROUP_GAP_MS,
  SOS_DOT_MS, SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS, SOS_SYMBOL_GAP_MS,
  SOS_DOT_MS, SOS_REPEAT_GAP_MS
};
const bool sosOutputOn[] = {
  true, false, true, false, true, false,
  true, false, true, false, true, false,
  true, false, true, false, true, false
};
const uint8_t SOS_STEPS = sizeof(sosDurations) / sizeof(sosDurations[0]);
uint8_t sosStep = 0;
unsigned long sosStepStart = 0;

bool gasBuzzerOn = false;
unsigned long gasBuzzerStepStart = 0;

void beginCalibration();
void updateCalibration(unsigned long now);
void setupEspNow();
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
void applyRemoteCommand(const String &cmd);
void handleSerialCommands();
void handleSetNode(String cmd);
void printHelp();
void printNodeAssignment();
SensorData readSensors();
void updateHistory(const SensorData &d);
float humidityRate();
float coRate();
int convertMq7ToCoPpm(int mq7Raw);
bool updateGasLeak(const SensorData &d);
int calculateFireScore(const SensorData &d);
void updateLocalActions(const SensorData &d, unsigned long now);
void updateFan(const SensorData &d);
void updateStrobe(unsigned long now);
void updateSosButton(unsigned long now);
void updateSosMorse(unsigned long now, bool buzzerAllowed);
void updateBuzzer(unsigned long now);
AlertPriority currentPriority();
void sendPacket();
void printStatus(const SensorData &d);
const char *zoneForNode(uint8_t id);
const char *exitForNode(uint8_t id);

void setup() {
  Serial.begin(115200);

  pinMode(PIN_FLAME, INPUT);
  pinMode(PIN_SOS, INPUT_PULLUP);
  pinMode(PIN_STROBE, OUTPUT);
  pinMode(PIN_FAN, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_SOS_LED, OUTPUT);

  digitalWrite(PIN_STROBE, LOW);
  digitalWrite(PIN_FAN, HIGH);
  digitalWrite(PIN_SOS_LED, HIGH);
  noTone(PIN_BUZZER);

  dht.begin();

  mq2Ema   = (float)analogRead(PIN_MQ2);
  mq135Ema = (float)analogRead(PIN_MQ135);

  for (int i = 0; i < HISTORY_SIZE; i++) {
    humidityHistory[i] = 65.0f;
    coHistory[i] = 0;
  }

  Serial.println();
  Serial.println("ARIA Node v6 ready");
  Serial.println("Transmitting as unassigned (ID=0) - hub will map to Node 1 by default.");
  Serial.println("To assign a specific ID, type: SETNODE 1  (or 2-6)");
  Serial.println();

  setupEspNow();
  beginCalibration();
}

void loop() {
  unsigned long now = millis();

  handleSerialCommands();
  updateSosButton(now);
  updateCalibration(now);
  updateStrobe(now);
  updateBuzzer(now);

  if (!calibrationActive && now - lastSensorTime >= SENSOR_INTERVAL_MS) {
    lastSensorTime = now;
    SensorData d = readSensors();
    updateHistory(d);
    gasLeakMode = updateGasLeak(d);
    // Fire score is always calculated — gasLeakMode is a separate actuator flag,
    // not a reason to suppress fire detection (they can co-exist briefly during transition)
    fireScore = forceFireMode ? 7 : calculateFireScore(d);
    updateLocalActions(d, now);
    updateFan(d);

    if (now - lastReportTime >= REPORT_INTERVAL_MS) {
      lastReportTime = now;
      printStatus(d);
    }
  }

  if (!calibrationActive && now - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = now;
    sendPacket();
  }
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    espNowReady = false;
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, hubBroadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(hubBroadcastAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ESP-NOW] Broadcast peer add failed");
      espNowReady = false;
      return;
    }
  }

  esp_now_register_recv_cb(onEspNowRecv);
  espNowReady = true;
  Serial.print("[ESP-NOW] Ready. Node MAC: ");
  Serial.println(WiFi.macAddress());
}

void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(HubCommandPacket)) return;

  HubCommandPacket packet;
  memcpy(&packet, data, sizeof(packet));
  if (packet.magic != 0xA6) return;

  bool match = (packet.targetNodeID == nodeID);
  bool matchUnassigned = (packet.targetNodeID == 0 && nodeID == 0);
  bool matchAll = (packet.targetNodeID == 255);
  if (!(match || matchUnassigned || matchAll)) return;

  String cmd = String(packet.command);
  cmd.trim();
  if (!cmd.length()) return;
  applyRemoteCommand(cmd);
}

void applyRemoteCommand(const String &remoteCmd) {
  String cmd = remoteCmd;
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("SETNODE ")) {
    handleSetNode(cmd);
    return;
  }

  if (cmd == "FIRE ON") {
    forceFireMode = true;
    Serial.println("[RCMD] Fire forced ON - score 7");
    return;
  }
  if (cmd == "FIRE OFF") {
    forceFireMode = false;
    fireScore = 0;
    prevScore = -1;
    Serial.println("[RCMD] Fire forced OFF");
    return;
  }
  if (cmd == "SOS ON") {
    sosActive = true;
    sosStep = 0;
    sosStepStart = millis();
    Serial.println("[RCMD] SOS forced ON");
    return;
  }
  if (cmd == "SOS OFF") {
    sosActive = false;
    digitalWrite(PIN_SOS_LED, LOW);
    Serial.println("[RCMD] SOS forced OFF");
    return;
  }
  if (cmd == "CALIB") {
    beginCalibration();
    Serial.println("[RCMD] Calibration started");
    return;
  }
  if (cmd == "SCORE") {
    Serial.printf("[RCMD] Node: %u Score: %d Gas: %s SOS: %s Fan: %s\n",
      nodeID, fireScore, gasLeakMode ? "YES" : "no", sosActive ? "YES" : "no", fanOn ? "ON" : "off");
    return;
  }

  Serial.println("[RCMD] Unknown remote command");
}

void beginCalibration() {
  calibrationActive = true;
  calibMq2Sum = 0;
  calibMq7Sum = 0;
  calibMq135Sum = 0;
  calibSamples = 0;
  lastCalibSampleTime = 0;

  Serial.println("========================================");
  Serial.println("CALIBRATION - reading environment baseline");
  Serial.println("Please ensure no smoke or gas is present");
  Serial.println("----------------------------------------");
}

void updateCalibration(unsigned long now) {
  if (!calibrationActive) return;
  if (lastCalibSampleTime != 0 && now - lastCalibSampleTime < CALIB_INTERVAL_MS) return;

  lastCalibSampleTime = now;
  int m2   = analogRead(PIN_MQ2);
  int m7   = analogRead(PIN_MQ7);
  int m135 = analogRead(PIN_MQ135);
  calibMq2Sum   += m2;
  calibMq7Sum   += m7;
  calibMq135Sum += m135;
  calibSamples++;

  Serial.printf("  Sample %2u/%u  MQ2: %4d  MQ7: %4d  MQ135: %4d\n",
    calibSamples, CALIB_SAMPLES, m2, m7, m135);

  if (calibSamples < CALIB_SAMPLES) return;

  mq2Baseline = calibMq2Sum / CALIB_SAMPLES;
  mq7Baseline = max(1L, calibMq7Sum / CALIB_SAMPLES);
  mq135Baseline = calibMq135Sum / CALIB_SAMPLES;

  mq2SlightSmoke = mq2Baseline + MQ2_CLEAN_OFFSET;
  mq2Spike       = max(mq2Baseline + MQ2_SPIKE_OFFSET,   MQ2_SPIKE_FLOOR);
  mq2GasLeak     = max(mq2Baseline + MQ2_GASLEAK_OFFSET, MQ2_GASLEAK_FLOOR);
  mq2Ema         = mq2Baseline;

  mq135SlightPoor = mq135Baseline + MQ135_CLEAN_OFFSET;
  mq135Poor       = max(mq135Baseline + MQ135_POOR_OFFSET,   MQ135_POOR_FLOOR);
  mq135Hazard     = max(mq135Baseline + MQ135_HAZARD_OFFSET, MQ135_HAZARD_FLOOR);
  mq135Ema        = mq135Baseline;

  calibrationActive = false;

  Serial.println("----------------------------------------");
  Serial.printf("MQ-2  baseline    : %d\n", mq2Baseline);
  Serial.printf("MQ-2  slight smoke: %d\n", mq2SlightSmoke);
  Serial.printf("MQ-2  spike       : %d\n", mq2Spike);
  Serial.printf("MQ-2  gas leak    : %d\n", mq2GasLeak);
  Serial.printf("MQ-7  baseline    : %d\n", mq7Baseline);
  Serial.printf("MQ-135 baseline   : %d\n", mq135Baseline);
  Serial.printf("MQ-135 slight poor: %d\n", mq135SlightPoor);
  Serial.printf("MQ-135 poor       : %d\n", mq135Poor);
  Serial.printf("MQ-135 hazard     : %d\n", mq135Hazard);
  Serial.println("Calibration complete.");
  Serial.println("========================================");

  if (nodeID == 0) {
    Serial.println("Awaiting node ID. Type: SETNODE 1  (or 2-6)");
  }
}

SensorData readSensors() {
  SensorData d;
  d.mq2Raw = analogRead(PIN_MQ2);
  d.mq7Raw = analogRead(PIN_MQ7);
  d.mq135Raw = analogRead(PIN_MQ135);

  mq2Ema   = (0.3f * d.mq2Raw)   + (0.7f * mq2Ema);
  mq135Ema = (0.3f * d.mq135Raw) + (0.7f * mq135Ema);

  d.mq2Smooth   = (int)mq2Ema;
  d.mq135Smooth = (int)mq135Ema;
  d.coPpm = convertMq7ToCoPpm(d.mq7Raw);

  d.humidity = dht.readHumidity();
  d.tempC = dht.readTemperature();
  if (isnan(d.humidity)) d.humidity = humidityHistory[(histIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE];
  if (isnan(d.tempC)) d.tempC = 27.0f;

  bool flameRaw = (digitalRead(PIN_FLAME) == LOW);
  if (flameRaw) {
    flameLowCount = min((int)flameLowCount + 1, 10);
    flameHighCount = 0;
  } else {
    flameHighCount = min((int)flameHighCount + 1, 10);
    flameLowCount = 0;
  }

  if (flameLowCount >= FLAME_CONFIRM_READS) flameConfirmed = true;
  if (flameHighCount >= FLAME_CONFIRM_READS) flameConfirmed = false;
  d.flameConfirmed = flameConfirmed;

  return d;
}

int convertMq7ToCoPpm(int mq7Raw) {
  float ratio = (float)mq7Raw / max(1, mq7Baseline);

  if (ratio > 1.5f) return 0;
  if (ratio > 1.0f) return map((int)(ratio * 1000), 1500, 1000, 5, 20);
  if (ratio > 0.7f) return map((int)(ratio * 1000), 1000, 700, 20, 100);
  if (ratio > 0.5f) return map((int)(ratio * 1000), 700, 500, 100, 200);
  return constrain(map((int)(ratio * 1000), 500, 0, 200, 300), 200, 300);
}

void updateHistory(const SensorData &d) {
  humidityHistory[histIdx] = d.humidity;
  coHistory[histIdx] = d.coPpm;
  histIdx = (histIdx + 1) % HISTORY_SIZE;
}

float humidityRate() {
  int newest = (histIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  int older = (histIdx - 5 + HISTORY_SIZE) % HISTORY_SIZE;
  return humidityHistory[newest] - humidityHistory[older];
}

float coRate() {
  int newest = (histIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  int older = (histIdx - 5 + HISTORY_SIZE) % HISTORY_SIZE;
  return (float)(coHistory[newest] - coHistory[older]);
}

bool updateGasLeak(const SensorData &d) {
  // MQ-2 gas leak pattern (LPG/propane/smoke without combustion)
  bool mq2Pattern = d.mq2Smooth > mq2GasLeak;
  // MQ-135 standalone chemical hazard (NH3, benzene, alcohol vapour)
  bool mq135Pattern = d.mq135Smooth > mq135Hazard && d.mq2Smooth < mq2Spike;
  bool gasPattern =
    (mq2Pattern || mq135Pattern) &&
    d.coPpm < CO_PPM_SAFE &&
    !d.flameConfirmed &&
    humidityRate() > -1.0f;

  if (gasPattern) {
    gasHitCount = min((int)gasHitCount + 1, 10);
    gasClearCount = 0;
  } else {
    gasClearCount = min((int)gasClearCount + 1, 10);
    gasHitCount = 0;
  }

  if (gasHitCount >= GAS_CONFIRM_READS) return true;
  if (gasClearCount >= GAS_CLEAR_READS) return false;
  return gasLeakMode;
}

int calculateFireScore(const SensorData &d) {
  int score = 0;
  float hRate = humidityRate();
  float cRate = coRate();

  // Flame sensor: definitive physical evidence — guarantees SCORE_FIRE on its own
  if (d.flameConfirmed) score += 5;

  // MQ-2: smoke / combustible gas
  if (d.mq2Smooth > mq2Spike)         score += 2;
  else if (d.mq2Smooth > mq2SlightSmoke) score += 1;

  // MQ-7: carbon monoxide (fast rise is the strongest indicator)
  if (d.coPpm > CO_PPM_CONCERN && cRate > 5.0f) score += 2;
  else if (d.coPpm > CO_PPM_CONCERN)             score += 1;

  // DHT22: rapid humidity drop = fire drying the air
  if (hRate < -HUM_DROP_THRESHOLD) score += 1;

  // MQ-135: corroborates poor / hazardous air quality
  if (d.mq135Smooth > mq135Poor)   score += 1;
  if (d.mq135Smooth > mq135Hazard) score += 1;

  // Negative corrections — ONLY applied when there is NO confirmed flame.
  // Never penalise a reading that already has direct fire evidence.
  if (!d.flameConfirmed) {
    // MQ-2 spike but CO clean and MQ-135 clean → dust, steam, or sensor glitch
    if (d.mq2Smooth > mq2Spike && d.coPpm < CO_PPM_CONCERN && d.mq135Smooth < mq135SlightPoor) score -= 2;
    // Smoke + rising humidity → cooking or steam
    if (d.mq2Smooth > mq2SlightSmoke && hRate > HUM_RISE_THRESHOLD) score -= 2;
    // Slow CO rise with no smoke → benign background CO (ventilation issue)
    if (d.coPpm > CO_PPM_CONCERN && cRate < 3.0f && d.mq2Smooth < mq2Spike) score -= 1;
    // MQ-135 elevated but MQ-2 clean → chemical vapour, not combustion
    if (d.mq135Smooth > mq135Poor && d.mq2Smooth < mq2SlightSmoke) score -= 1;
  }

  return constrain(score, 0, 10);
}

void updateLocalActions(const SensorData &d, unsigned long now) {
  bool changed = (fireScore != prevScore);

  if (fireScore >= SCORE_FIRE) {
    strobeActive = true;
    if (changed || now - lastFireMsgTime >= 5000) {
      lastFireMsgTime = now;
      Serial.println(fireScore >= SCORE_CRITICAL ? "[FIRE] CRITICAL Tier 3" : "[FIRE] EMERGENCY Tier 2");
    }
  } else if (gasLeakMode) {
    strobeActive = true;
    if (now - lastGasMsgTime >= 10000) {
      lastGasMsgTime = now;
      Serial.println("[GAS] Leak detected. Ventilation open. Avoid ignition sources.");
    }
  } else {
    strobeActive = false;
    if (changed && fireScore >= SCORE_AMBER) Serial.println("[WARN] Pre-warning Tier 1");
    if (changed && prevScore >= SCORE_AMBER && fireScore < SCORE_AMBER) Serial.println("[STATUS] Returned to normal");
  }

  prevScore = fireScore;
}

void updateFan(const SensorData &d) {
  bool shouldRun = false;

  if (fireScore >= SCORE_FIRE) {
    shouldRun = false;  // fan off during fire — avoid spreading smoke
  } else if (gasLeakMode) {
    shouldRun = true;
  } else {
    bool smokeRun   = fanOn ? (d.mq2Smooth > mq2Baseline)       : (d.mq2Smooth > mq2SlightSmoke);
    bool coRun      = d.coPpm > CO_PPM_CONCERN;
    bool humRun     = fanOn ? (d.humidity > HUM_VENT_OFF)        : (d.humidity > HUM_VENT_ON);
    bool tempRun    = fanOn ? (d.tempC > TEMP_VENT_OFF)          : (d.tempC > TEMP_VENT_ON);
    bool airQualRun = fanOn ? (d.mq135Smooth > mq135SlightPoor)  : (d.mq135Smooth > mq135Poor);
    shouldRun = smokeRun || coRun || humRun || tempRun || airQualRun;
  }

  if (shouldRun != fanOn) {
    fanOn = shouldRun;
    digitalWrite(PIN_FAN, fanOn ? LOW : HIGH);  // LOW energises relay → NO closes → fan on
    Serial.println(fanOn ? "[FAN] ON" : "[FAN] OFF");
  }
}

void updateStrobe(unsigned long now) {
  if (!strobeActive) {
    strobeState = false;
    digitalWrite(PIN_STROBE, LOW);
    return;
  }

  if (now - lastStrobeTime >= STROBE_INTERVAL_MS) {
    lastStrobeTime = now;
    strobeState = !strobeState;
    digitalWrite(PIN_STROBE, strobeState ? HIGH : LOW);
  }
}

void updateSosButton(unsigned long now) {
  bool reading = digitalRead(PIN_SOS);

  if (reading != sosButtonLastReading) {
    sosButtonLastReading = reading;
    sosButtonLastChange = now;
  }

  if (now - sosButtonLastChange >= BUTTON_DEBOUNCE_MS && reading != sosButtonStable) {
    sosButtonStable = reading;
    if (sosButtonStable == LOW) {
      sosActive = !sosActive;
      sosStep = 0;
      sosStepStart = now;
      digitalWrite(PIN_SOS_LED, sosActive ? HIGH : LOW);
      Serial.println(sosActive ? "[SOS] Active" : "[SOS] Stopped");
    }
  }
}

AlertPriority currentPriority() {
  if (fireScore >= SCORE_FIRE) return ALERT_FIRE;
  if (gasLeakMode) return ALERT_GAS;
  if (sosActive) return ALERT_SOS;
  return ALERT_IDLE;
}

void updateSosMorse(unsigned long now, bool buzzerAllowed) {
  if (!sosActive) {
    digitalWrite(PIN_SOS_LED, LOW);
    if (buzzerAllowed) digitalWrite(PIN_BUZZER, LOW);
    return;
  }

  // Advance step only when duration elapsed
  if (now - sosStepStart >= sosDurations[sosStep]) {
    // Clean stop before advancing
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_SOS_LED, LOW);
    sosStepStart = now;
    sosStep = (sosStep + 1) % SOS_STEPS;
  }

  bool on = sosOutputOn[sosStep];
  digitalWrite(PIN_SOS_LED, on ? HIGH : LOW);
  if (buzzerAllowed) {
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  }
}

void updateBuzzer(unsigned long now) {
  AlertPriority p = currentPriority();

  // Active buzzer: HIGH = ON, LOW = OFF
  // No tone() needed — just digitalWrite

  if (p == ALERT_FIRE) {
    // Continuous ON — loudest signal
    digitalWrite(PIN_BUZZER, HIGH);
    updateSosMorse(now, false);
    return;
  }

  if (p == ALERT_GAS) {
    // Fast intermittent: 200ms on / 300ms off
    unsigned long phaseMs = gasBuzzerOn ? GAS_BEEP_ON_MS : GAS_BEEP_OFF_MS;
    if (now - gasBuzzerStepStart >= phaseMs) {
      gasBuzzerStepStart = now;
      gasBuzzerOn = !gasBuzzerOn;
    }
    digitalWrite(PIN_BUZZER, gasBuzzerOn ? HIGH : LOW);
    updateSosMorse(now, false);
    return;
  }

  if (p == ALERT_SOS) {
    // SOS morse controls buzzer directly
    updateSosMorse(now, true);
    return;
  }

  // Idle
  digitalWrite(PIN_BUZZER, LOW);
  updateSosMorse(now, false);
}

void sendPacket() {
  if (!espNowReady) return;

  NodePacket packet;
  packet.nodeID       = nodeID;
  packet.mq2Smooth    = (int)mq2Ema;
  packet.mq7Raw       = analogRead(PIN_MQ7);
  packet.coPpm        = convertMq7ToCoPpm(packet.mq7Raw);
  packet.mq135Smooth  = (int)mq135Ema;
  packet.fireScore    = fireScore;
  packet.flameConfirmed = flameConfirmed;
  packet.gasLeakMode  = gasLeakMode;
  packet.sosActive    = sosActive;
  packet.tempC        = dht.readTemperature();
  packet.humidity     = dht.readHumidity();
  if (isnan(packet.tempC)) packet.tempC = 27.0f;
  if (isnan(packet.humidity)) packet.humidity = humidityHistory[(histIdx - 1 + HISTORY_SIZE) % HISTORY_SIZE];

  esp_err_t result = esp_now_send(hubBroadcastAddress, (uint8_t *)&packet, sizeof(packet));
  if (result != ESP_OK) Serial.printf("[ESP-NOW] Send failed: %d\n", result);
}

void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.startsWith("SETNODE ")) {
    handleSetNode(cmd);
  } else if (cmd == "FIRE ON") {
    forceFireMode = true;
    Serial.println("[CMD] Fire forced ON - score 7");
  } else if (cmd == "FIRE OFF") {
    forceFireMode = false;
    fireScore = 0;
    prevScore = -1;
    Serial.println("[CMD] Fire forced OFF");
  } else if (cmd == "SCORE") {
    Serial.printf("[CMD] Node: %u  Score: %d  Gas: %s  SOS: %s  Fan: %s\n",
      nodeID, fireScore, gasLeakMode ? "YES" : "no", sosActive ? "YES" : "no", fanOn ? "ON" : "off");
  } else if (cmd == "CALIB") {
    beginCalibration();
  } else if (cmd == "HELP") {
    printHelp();
  } else if (cmd.length() > 0) {
    Serial.println("[CMD] Unknown command. Type HELP.");
  }
}

void handleSetNode(String cmd) {
  int id = cmd.substring(8).toInt();
  if (id < 0 || id > 6) {
    Serial.println("[CMD] Invalid node ID. Use SETNODE 0 (unassign) or 1-6.");
    return;
  }

  nodeID = (uint8_t)id;
  printNodeAssignment();
}

void printNodeAssignment() {
  Serial.printf("[CMD] This node is now NODE %u\n", nodeID);
  Serial.printf("[CMD] Zone: %s\n", zoneForNode(nodeID));
  Serial.printf("[CMD] Nearest exit: %s\n", exitForNode(nodeID));
  Serial.println("[CMD] Transmitting to hub every 500ms");
}

void printHelp() {
  Serial.println("========================================");
  Serial.println("Commands:");
  Serial.println("  SETNODE <1-6>  - assign node ID");
  Serial.println("  FIRE ON        - force score 7");
  Serial.println("  FIRE OFF       - clear forced fire");
  Serial.println("  SCORE          - print state");
  Serial.println("  CALIB          - recalibrate MQ sensors");
  Serial.println("  HELP           - show commands");
  Serial.println("========================================");
}

void printStatus(const SensorData &d) {
  Serial.println("========================================");
  Serial.printf("Node ID      : %u%s\n", nodeID, nodeID == 0 ? " (not assigned)" : "");
  Serial.printf("MQ-2  smooth : %d  baseline:%d slight:%d spike:%d gas:%d\n",
    d.mq2Smooth, mq2Baseline, mq2SlightSmoke, mq2Spike, mq2GasLeak);
  Serial.printf("MQ-7  raw    : %d  CO ppm:%d\n", d.mq7Raw, d.coPpm);
  Serial.printf("MQ-135 smooth: %d  baseline:%d slight:%d poor:%d hazard:%d\n",
    d.mq135Smooth, mq135Baseline, mq135SlightPoor, mq135Poor, mq135Hazard);
  Serial.printf("Temp/Hum     : %.1f C / %.1f %%\n", d.tempC, d.humidity);
  Serial.printf("Rates        : CO %.1f / Hum %.1f\n", coRate(), humidityRate());
  Serial.printf("Flame        : %s\n", d.flameConfirmed ? "YES" : "no");
  Serial.printf("Fire score   : %d / 10\n", fireScore);
  Serial.printf("Gas leak     : %s\n", gasLeakMode ? "YES" : "no");
  Serial.printf("SOS          : %s\n", sosActive ? "YES" : "no");
  Serial.printf("Fan          : %s\n", fanOn ? "ON" : "off");
  Serial.println("========================================");
}

const char *zoneForNode(uint8_t id) {
  if (id == 1 || id == 5) return "A (Node 1 + Node 5)";
  if (id == 2 || id == 3) return "B (Node 2 + Node 3)";
  if (id == 4 || id == 6) return "C (Node 4 + Node 6)";
  return "unassigned";
}

const char *exitForNode(uint8_t id) {
  if (id == 1 || id == 5) return "Exit A";
  if (id == 2 || id == 3) return "Exit B";
  if (id == 4 || id == 6) return "Exit C";
  return "unassigned";
}
