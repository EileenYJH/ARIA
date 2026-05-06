# ARIA — Adaptive Room Intelligence & Alert
## Complete System Specification v3
### Makerthon 2025/2026

---

## 1. System Overview

ARIA is a 6-node ESP32 wireless sensor network designed for residential fire detection, indoor air quality monitoring, and intelligent evacuation guidance. The system uses ESP-NOW peer-to-peer communication between sensor nodes and a central hub, with a WS2812B LED strip providing real-time floor-level evacuation routing and a web dashboard for live monitoring.

**Core capabilities:**
- Multi-sensor fire discrimination — rejects cooking, haze, fog false alarms
- Dynamic LED evacuation routing adapted to fire location and exit congestion
- Continuous IAQ monitoring with automatic ventilation control
- Tiered alert system (Tier 1 admin → Tier 2 evacuate → Tier 3 warden contacts BOMBA)
- Gas leak detection with opposite ventilation response to fire
- SOS trapped-person alert with international morse pattern
- ESP-NOW wireless mesh — no router required, 200m range

---

## 2. Floor Plan and Zone Mapping

### 2.1 Physical Layout

```
[Node1][Node2][Node3][Node4]
|─────────────────────────────|
|  [Node5]  junction  [Node6] |
Exit A          |              Exit C
                |
             Exit B
```

### 2.2 Zone Assignment

| Zone | Nodes | Physical location | Nearest exit |
|---|---|---|---|
| Zone A | Node 1, Node 5 | Left side — top-left + bottom-left | Exit A (left staircase) |
| Zone B | Node 2, Node 3 | Centre — top-centre units near junction | Exit B (centre via junction) |
| Zone C | Node 4, Node 6 | Right side — top-right + bottom-right | Exit C (right staircase) |

### 2.3 Exit Physical Locations

| Exit | Location | Ground floor node nearby |
|---|---|---|
| Exit A | Far left end of corridor | Node 5 |
| Exit B | Centre, reached via junction between Node 5 and Node 6 | Junction (no sensor) |
| Exit C | Far right end of corridor | Node 6 |

### 2.4 Exit Blocking Rules

Only ground-floor nodes directly adjacent to exits physically threaten exit access. Upper-floor node fires affect zone routing colour but do not block the corridor exits.

| Condition | Exit physically blocked |
|---|---|
| Node 5 fire (score ≥ 5) | Exit A more directly threatened |
| Node 6 fire (score ≥ 5) | Exit C more directly threatened |
| Zone B fire (Node 2 or 3) | Exit B path compromised |
| All zones fire + all exits congested | Shelter in place — human decision |

---

## 3. Hardware Architecture

### 3.1 Sensor Node (×6 ESP32 DevKit C v4)

All 6 nodes run identical firmware. Node ID is assigned via serial command after boot.

#### Sensors:

| Component | GPIO | Type | Role |
|---|---|---|---|
| MQ-2 | 34 | Analog ADC1 | Smoke / combustible gas |
| MQ-7 | 33 | Analog ADC1 | Carbon monoxide |
| DHT22 | 14 | Digital | Temperature + humidity |
| LDR4 (flame stand-in) | 25 | Digital DO | Flame detection (LOW = flame) |

#### Outputs:

| Component | GPIO | Role |
|---|---|---|
| Buzzer | 13 | Fire siren + SOS morse pattern |
| Red LED (strobe) | 27 | 4Hz visual flash during fire/gas |
| Green LED (vent) | 12 | Ventilation system active indicator |
| SOS LED | 23 | Blinks in exact sync with SOS morse |

#### Inputs:

| Component | GPIO | Role |
|---|---|---|
| SOS button | 26 | INPUT_PULLUP — toggle SOS on/off |
| EN button | EN pin | Hardware reset — restarts + recalibrates |

#### Node responsibilities:
- ✔ Read and filter sensors
- ✔ Compute fire score
- ✔ Detect gas leak
- ✔ Control local outputs (buzzer, LEDs, vent)
- ✔ Transmit ESP-NOW packet to hub every 500ms
- ✗ No LED strip routing logic
- ✗ No evacuation decisions

---

### 3.2 Hub (×1 ESP32 DevKit C v4)

#### Outputs:

| Component | GPIO | Role |
|---|---|---|
| WS2812B LED strip | 5 | 30 pixels — 3 strips × 10px daisy-chained |
| Fan relay LED (red) | 18 | Fan relay stand-in |
| Vent indicator LED (blue) | 19 | Ventilation status indicator |

#### Inputs (exit monitoring):

| Component | GPIO | Exit monitored | Notes |
|---|---|---|---|
| IR motion sensor | 23 | Exit A | Detects presence at exit |
| IR motion sensor | 15 | Exit B | Detects presence at exit |
| IR motion sensor | 26 | Exit C | Detects presence at exit |

**Note:** IR motion sensors used for prototype. Motion detected = person at exit. Congestion determined by sustained motion and occupancy timing.

#### Hub responsibilities:
- ✔ Receive ESP-NOW packets from all 6 nodes
- ✔ Maintain node state table
- ✔ Compute zone fire risk
- ✔ Determine evacuation routing
- ✔ Drive WS2812B LED strip with chase animation
- ✔ Monitor exit congestion via IR motion sensors
- ✔ Host web dashboard (Wi-Fi AP mode)
- ✗ No sensor reading
- ✗ No fire scoring

---

## 4. Network Communication

### 4.1 ESP-NOW Packet Structure

```cpp
typedef struct {
  uint8_t nodeID;          // 1–6 (assigned via SETNODE command)
  int     mq2Smooth;       // EMA-filtered MQ-2 ADC value
  int     mq7Raw;          // raw MQ-7 ADC value
  int     coPpm;           // calibrated nonlinear CO ppm
  int     fireScore;       // 0–10
  bool    flameConfirmed;  // debounced flame detection
  bool    gasLeakMode;     // gas leak detected
  float   tempC;           // temperature Celsius
  float   humidity;        // relative humidity %
} NodePacket;
```

### 4.2 Transmission Rules

- Node will NOT transmit until Node ID is assigned via `SETNODE` command
- Transmit every 500ms after ID is set
- Sends filtered/computed values — no raw noise
- Hub is single receiver
- Hub stores latest packet per node in `NodeState nodes[7]` (index 1–6)

---

## 5. Node ID Assignment

### 5.1 Method — Serial Command (SETNODE)

All 6 nodes run identical firmware. Before the node begins transmitting, the operator types the node ID in the Serial Monitor. This allows the same firmware image to be flashed to all 6 boards with no modification.

**Startup message before ID assigned:**
```
ARIA Node v6 — awaiting node ID assignment
Type: SETNODE 1  (or 2, 3, 4, 5, 6)
Node will not transmit until ID is set.
```

**After SETNODE command:**
```
[CMD] This node is now NODE 3
[CMD] Zone: B (Node 2 + Node 3)
[CMD] Nearest exit: Exit B (centre junction)
[CMD] Transmitting to hub every 500ms
```

**Command format:**
```
SETNODE 1    → assigns Node ID 1 (Zone A)
SETNODE 2    → assigns Node ID 2 (Zone B)
SETNODE 3    → assigns Node ID 3 (Zone B)
SETNODE 4    → assigns Node ID 4 (Zone C)
SETNODE 5    → assigns Node ID 5 (Zone A)
SETNODE 6    → assigns Node ID 6 (Zone C)
```

### 5.2 Node ID to Zone Reference

| Node ID | Zone | Physical location | Exit |
|---|---|---|---|
| 1 | A | Top-left unit | Exit A |
| 2 | B | Top-centre-left unit | Exit B |
| 3 | B | Top-centre-right unit | Exit B |
| 4 | C | Top-right unit | Exit C |
| 5 | A | Bottom-left unit | Exit A |
| 6 | C | Bottom-right unit | Exit C |

### 5.3 Why Serial Command is Best for Demo

- Single firmware image flashed to all 6 boards — no per-board modification
- Node ID can be changed live during demo without reflashing
- Demonstrates scalability — same hardware, different role via software
- Warden or operator can reassign nodes if one fails

---

## 6. Node Firmware

### 6.1 Startup Calibration

On every boot (including EN button reset), reads MQ-2 and MQ-7 thirty times over 3 seconds and calculates environment baseline. Thresholds are derived from baseline + absolute offsets (not multipliers).

```
MQ-2 baseline = average of 30 samples
MQ-7 baseline = average of 30 samples (used for ppm conversion only)
```

Serial monitor prints every sample and final thresholds after calibration.

### 6.2 Sensor Filtering

**MQ-2 — Exponential Moving Average:**
```
filtered = (0.3 × newValue) + (0.7 × previousFiltered)
```
Reduces noise spikes. Does not permanently store past events — decays over time.

**MQ-7 — Nonlinear CO ppm conversion:**

MQ-7 response follows a logarithmic curve. A piecewise ratio-based approximation is used rather than linear mapping:

```
ratio = mq7Raw / mq7Baseline

ratio > 1.5   → CO 0–5 ppm     (clean air)
ratio 1.0–1.5 → CO 5–20 ppm    (slight elevation)
ratio 0.7–1.0 → CO 20–100 ppm  (elevated)
ratio 0.5–0.7 → CO 100–200 ppm (dangerous)
ratio < 0.5   → CO 200–300 ppm (immediately dangerous)
```

**Flame sensor — debounce:**
Requires 3 consecutive LOW readings before `flameConfirmed = true`.

**Gas leak — debounce:**
Requires 3 consecutive gas-pattern readings before `gasLeakMode = true`.

### 6.3 MQ Sensor Thresholds

Thresholds are NOT derived from multiplying the baseline. They use calibration-aware absolute offsets combined with hard floor values based on known concentration standards.

#### MQ-2 Thresholds (ADC-based):

```cpp
// Offset above calibrated baseline
#define MQ2_CLEAN_OFFSET    150   // slight smoke above clean air
#define MQ2_SPIKE_OFFSET    400   // significant smoke — fire scoring
#define MQ2_GASLEAK_OFFSET  700   // heavy gas — leak detection

// Hard floor — never trigger below known safe minimum ADC
// Prevents false triggers when baseline is unusually high
#define MQ2_SPIKE_FLOOR     800   // absolute minimum for spike
#define MQ2_GASLEAK_FLOOR   1200  // absolute minimum for gas leak

// Applied after calibration in setup():
int mq2SlightSmoke = mq2Baseline + MQ2_CLEAN_OFFSET;
int mq2Spike       = max(mq2Baseline + MQ2_SPIKE_OFFSET,  MQ2_SPIKE_FLOOR);
int mq2GasLeak     = max(mq2Baseline + MQ2_GASLEAK_OFFSET, MQ2_GASLEAK_FLOOR);
```

#### MQ-7 / CO Thresholds (ppm-based, absolute standards):

CO thresholds are based on WHO and OSHA international health standards — not derived from baseline.

```cpp
#define CO_PPM_SAFE       9    // WHO 8-hour average limit
#define CO_PPM_CONCERN    15   // ARIA pre-warning threshold
#define CO_PPM_DANGEROUS  35   // OSHA 8-hour permissible limit
#define CO_PPM_FIRE       50   // active combustion likely
#define CO_PPM_CRITICAL   100  // immediately dangerous to health
```

MQ-7 baseline is used ONLY for the ppm conversion ratio (Section 6.2) — not for triggering thresholds.

#### Why not multipliers:

MQ sensors have nonlinear response curves. A fixed multiplier (e.g. ×1.5) produces different absolute concentration thresholds depending on temperature, humidity, and warm-up state. Using calibration offset + hard floor gives consistent triggering at known concentration levels regardless of environmental baseline variation.

### 6.4 Fire Score Algorithm

| Condition | Points | Notes |
|---|---|---|
| MQ-2 smooth above spike threshold | +1 | Smoke present |
| CO > 15ppm AND rising rate > 10 | +3 | Active combustion indicator |
| CO sustained above 30ppm | +2 | Confirmed elevated CO |
| CO moderately elevated 15–30ppm | +1 | Early stage |
| Humidity dropping > 3% | +1 | Fire drying air |
| Flame confirmed (3 readings) | +3 | Direct flame evidence |
| Smoke + CO < 15ppm | −2 | Cooking / fog / aerosol |
| Smoke + humidity rising | −2 | Steam / fog machine |
| CO elevated + slow rise + no flame | −1 | Malaysian haze pattern |

**Score thresholds:**

| Score | Alert level | Tier |
|---|---|---|
| 0–2 | Normal | IAQ monitoring only |
| 3–4 | Pre-warning | Tier 1 — admin Telegram |
| 5–7 | Emergency | Tier 2 — evacuate |
| 8–10 | Critical | Tier 3 — warden contacts BOMBA 999 |

### 6.5 Gas Leak Detection

Signature: MQ-2 heavy smoke + CO near zero + no flame + humidity stable

Response is **opposite to fire** — vent OPENS to disperse gas.

```
gasLeakMode = true when (sustained 3 readings):
  mq2Smooth > mq2GasLeak  AND
  coPpm < CO_PPM_SAFE     AND
  flameConfirmed == false  AND
  humidityRate > -1.0
```

### 6.6 Ventilation Logic (Green LED)

| Condition | Ventilation | Priority |
|---|---|---|
| Fire score ≥ 5 | OFF — starve fire | 1 highest |
| Gas leak active | ON — disperse gas | 2 |
| MQ-2 above mq2SlightSmoke | ON | 3 |
| CO > CO_PPM_CONCERN (15ppm) | ON | 3 |
| Humidity > 75% | ON | 3 |
| Temperature > 30°C | ON | 3 |
| All normal | OFF | — |

Hysteresis buffers prevent noise cycling:
- Humidity: ON at 75%, OFF below 70%
- Temperature: ON at 30°C, OFF below 28.5°C

### 6.7 SOS System

**Toggle: 1st press = start, 2nd press = stop immediately**

**Morse pattern (· · · — — — · · ·):**

| Symbol | Duration |
|---|---|
| Dot (·) | 150ms ON |
| Dash (—) | 450ms ON |
| Gap between symbols | 150ms OFF |
| Gap between groups | 450ms OFF |
| Gap before repeat | 1500ms OFF |

SOS LED (GPIO 23) blinks in exact sync with buzzer — ON during beep, OFF during silence.
Fully non-blocking — implemented as millis() state machine. Zero delay() calls.

**Buzzer priority:**

| Priority | Condition | Behaviour |
|---|---|---|
| 1 | Fire score ≥ 5 | Continuous 1000Hz siren — overrides all |
| 2 | Gas leak | Intermittent 1500Hz 2Hz pattern |
| 3 | SOS active | Morse 2500Hz |
| 4 | Idle | Silent |

### 6.8 Strobe LED (Red, GPIO 27)

- Active during fire and gas leak
- 4Hz flash — 125ms on, 125ms off
- Fully non-blocking via millis()

### 6.9 Node Serial Commands

| Command | Effect |
|---|---|
| `SETNODE <1-6>` | Assign node ID — required before transmit |
| `FIRE ON` | Force fire mode score 7 for demo |
| `FIRE OFF` | Reset fire mode |
| `SCORE` | Print current score and state |
| `CALIB` | Re-run sensor calibration without reset |
| `HELP` | List all commands |

---

## 7. Hub Firmware

### 7.1 Node State Table

```cpp
struct NodeState {
  int     fireScore;
  int     coPpm;
  int     mq2Smooth;
  bool    flameConfirmed;
  bool    gasLeakMode;
  float   tempC;
  float   humidity;
  unsigned long lastSeen;  // millis() of last packet received
};

NodeState nodes[7];  // index 1–6, index 0 unused
```

### 7.2 Zone Risk Calculation

```cpp
int scoreA = max(nodes[1].fireScore, nodes[5].fireScore);
int scoreB = max(nodes[2].fireScore, nodes[3].fireScore);
int scoreC = max(nodes[4].fireScore, nodes[6].fireScore);
int overall = max({scoreA, scoreB, scoreC});

bool fireInA = (scoreA >= SCORE_FIRE);
bool fireInB = (scoreB >= SCORE_FIRE);
bool fireInC = (scoreC >= SCORE_FIRE);
```

### 7.3 LED Strip Physical Layout

Three 10-pixel WS2812B strips daisy-chained from GPIO 5:

```
strip1 (10px horizontal) = Zone A → Exit A left
strip2 (10px vertical)   = Zone B → Exit B down via junction
strip3 (10px horizontal) = Zone C → Exit C right

Index 0–9   = Zone A
Index 10–19 = Zone B
Index 20–29 = Zone C
```

### 7.4 LED Behaviour — Complete Rules

**No fire anywhere:**
```
All LEDs OFF
```

**Zone A fire only:**
```
Zone A → RED chase pointing RIGHT (away from A, toward B/C)
Zone B → GREEN chase toward Exit B
Zone C → GREEN chase toward Exit C
```

**Zone B fire only:**
```
Zone A → GREEN chase toward Exit A
Zone B → RED chase SPLIT — left half chases left toward A,
                           right half chases right toward C
Zone C → GREEN chase toward Exit C
```

**Zone C fire only:**
```
Zone A → GREEN chase toward Exit A
Zone B → GREEN chase toward Exit B
Zone C → RED chase pointing LEFT (away from C, toward A/B)
```

**Zone A + Zone C fire:**
```
Zone A → RED chase pointing RIGHT (away from A toward B)
Zone B → GREEN bright pulse (Exit B is only safe route)
Zone C → RED chase pointing LEFT (away from C toward B)
```

**Zone A + Zone B fire:**
```
Zone A → RED chase pointing RIGHT (away from A toward C)
Zone B → RED chase pointing RIGHT (away from B toward C)
Zone C → GREEN chase toward Exit C
```

**Zone B + Zone C fire:**
```
Zone A → GREEN chase toward Exit A
Zone B → RED chase pointing LEFT (away from B toward A)
Zone C → RED chase pointing LEFT (away from C toward A)
```

**ALL zones fire:**
```
Zone A → RED chase toward Exit A (human decides)
Zone B → RED chase toward Exit B (human decides)
Zone C → RED chase toward Exit C (human decides)
```

**Pre-warning (score 3–4, no confirmed fire):**
```
All zones → AMBER pulse 1Hz
```

**Congested exit:**
```
Same chase direction and packing — colour changes GREEN → AMBER only
Direction is never removed, even when congested
```

### 7.5 Chase Direction Reference

| Zone | Normal (no fire) | Away from fire |
|---|---|---|
| Zone A (LED 0–9) | Leftward → index 0 (Exit A) | Rightward → index 9 |
| Zone B (LED 10–19) | Split — left half leftward, right half rightward | Depends on which side is safe |
| Zone C (LED 20–29) | Rightward → index 29 (Exit C) | Leftward → index 20 |

### 7.6 Proximity Packing Chase Animation

LEDs become denser approaching the target exit. Applies to GREEN, RED, and AMBER — only colour differs.

```
Distance from target exit | Pixel spacing
0 — at exit               | every pixel (spacing 1) — densest
1–3 — near                | every 2nd pixel
4–6 — mid                 | every 3rd pixel
7–9 — far                 | every 4th pixel — sparsest
```

Visual effect: occupants see progressively denser LEDs confirming correct direction.

### 7.7 LED Colour Summary

| Colour | Meaning |
|---|---|
| GREEN chase | Safe zone — follow toward exit |
| RED chase | Fire zone — still guides AWAY from fire toward alternative exit |
| AMBER chase | Congested exit — still usable, same direction as green |
| AMBER pulse 1Hz | Pre-warning — not yet confirmed fire |
| All zones OFF | Normal operation — no fire |
| All RED chase | All zones fire — human decides |

### 7.8 IR Motion Sensor Congestion Detection

IR motion sensors at each exit detect presence of people passing through.

| Layer | Method | Trigger |
|---|---|---|
| 1 — Real-time | Motion detected continuously 2s+ | Congested immediately |
| 2 — Window occupancy | Motion >30% of 10s window | Congested |
| 3 — Rate | >5 motion events per minute equivalent | Congested |

Congestion clears when: no motion 3s+ AND rate below threshold.
Congested exit: GREEN chase → AMBER chase (direction unchanged).

### 7.9 Hub Serial Commands

| Command | Effect |
|---|---|
| `NODE <id> <score>` | Set fire score for node e.g. `NODE 1 7` |
| `NODE <score>` | Set score without node ID |
| `CONGEST A/B/C` | Force congestion at exit |
| `CLEAR` | Reset all state |
| `STATUS` | Print all node scores and exit state |
| `HELP` | List commands |

---

## 8. Web Dashboard

### 8.1 Tech Stack

- ESP32 AsyncWebServer
- HTML + CSS + JavaScript
- WebSocket live updates every 1 second
- Wi-Fi Access Point — SSID: `ARIA-HUB`, Password: `aria1234`

### 8.2 Dashboard Layout

```
╔══════════════════════════════════════════════════╗
║              ARIA HUB DASHBOARD                  ║
╠══════════════════════════════════════════════════╣
║  ZONE STATUS                                     ║
║  Zone A [ SAFE / WARNING / FIRE ]                ║
║  Zone B [ SAFE / WARNING / FIRE ]                ║
║  Zone C [ SAFE / WARNING / FIRE ]                ║
╠══════════════════════════════════════════════════╣
║  NODE STATUS GRID                                ║
║  ┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌─────┐║
║  │Node 1││Node 2││Node 3││Node 4││Node 5││Node6│║
║  │Score ││Score ││Score ││Score ││Score ││Score│║
║  │Temp  ││Temp  ││Temp  ││Temp  ││Temp  ││Temp │║
║  │Hum % ││Hum % ││Hum % ││Hum % ││Hum % ││Hum %│║
║  │CO ppm││CO ppm││CO ppm││CO ppm││CO ppm││CO   │║
║  └──────┘└──────┘└──────┘└──────┘└──────┘└─────┘║
╠══════════════════════════════════════════════════╣
║  EVACUATION MAP                                  ║
║  ExitA ←[ZONE A][ZONE B][ZONE C]→ ExitC         ║
║                   ↓                              ║
║                Exit B                            ║
║  (animated LED simulation — updates live)        ║
╠══════════════════════════════════════════════════╣
║  ALERT PANEL                                     ║
║  [🔴 FIRE ALERT] [🟡 GAS ALERT] [🔵 SOS ALERT]   ║
╠══════════════════════════════════════════════════╣
║  SYSTEM LOG + CHAT                               ║
║  > [HUB] Node 1 score 7 — EMERGENCY              ║
║  > [LED] Zone A RED chase right — away from fire ║
║  > [IR] Exit A congested — amber routing         ║
║  > [VENT] ON — humidity high                     ║
║  ┌────────────────────────────────────────────┐  ║
║  │ Type command or message...        [Send]   │  ║
║  └────────────────────────────────────────────┘  ║
╚══════════════════════════════════════════════════╝
```

### 8.3 Chat Box Functionality

| Function | Details |
|---|---|
| Warden commands | Type any serial command — `NODE 1 7`, `CONGEST A`, `CLEAR` |
| Broadcast | Free text visible to all connected dashboard viewers |
| Auto log | All ESP-NOW packets, LED decisions, congestion events, alert changes |

---

## 9. Alert Tier System

| Tier | Score | Recipients | Channel | Action |
|---|---|---|---|---|
| Tier 1 | 3–4 | Admin / warden | Telegram | Investigate — check for false alarm |
| Tier 2 | 5–7 | All residents + warden | Telegram | Evacuate now — LED routing activates |
| Tier 3 | 8–10 | Warden only | Telegram + dashboard | Warden calls 999 / MERS 999 to brief BOMBA |

ARIA does not contact BOMBA directly. At Tier 3 the warden receives full fire location data (which nodes, which zone, which exits available) to brief BOMBA when calling 999. This follows standard Malaysian building fire response under UBBL 1984.

---

## 10. Priority System

| Priority | Condition | Node behaviour | Hub LED |
|---|---|---|---|
| 1 — Fire | Score ≥ 5 | Siren + strobe, vent OFF | RED/GREEN chase routing |
| 2 — Gas | Gas leak | Intermittent buzz + strobe, vent ON | Yellow dashboard alert |
| 3 — SOS | Button pressed | Morse buzzer + SOS LED | Blue dashboard alert |
| 4 — Normal | All clear | Vent conditional on IAQ | All LEDs OFF |

---

## 11. Power Supply

System power supplied via building existing emergency power infrastructure in compliance with UBBL 1984 and MS 1745. No additional battery required at node level.

For prototype/demo: USB power banks provide equivalent battery backup.

---

## 12. Required Libraries

### Node:
| Library | Purpose |
|---|---|
| `WiFi.h` | ESP-NOW dependency |
| `esp_now.h` | Wireless node-to-hub communication |
| `DHT.h` (Adafruit) | Temperature + humidity |

### Hub:
| Library | Purpose |
|---|---|
| `WiFi.h` | AP mode for dashboard |
| `esp_now.h` | Receive node packets |
| `AsyncTCP.h` | WebSocket support |
| `ESPAsyncWebServer.h` | Web dashboard server |
| `FastLED.h` | WS2812B LED strip control |

---

## 13. Simulation (Wokwi)

Two separate Wokwi projects — node and hub. ESP-NOW simulated via manual Serial Monitor bridge.

**Node project startup:**
```
ARIA Node v6 — awaiting node ID
Type: SETNODE 1  (or 2–6)
```

**Hub demo command sequence:**

```
NODE 1 7           → Zone A fire — Zone A RED right, B+C green
NODE 5 7           → Zone A fire — Exit A threatened (Node 5)
NODE 2 7           → Zone B fire — Zone B RED split, A+C green
NODE 4 7           → Zone C fire — Zone C RED left, A+B green
NODE 1 7           →
NODE 4 7           → Zone A+C fire — both RED toward B, Zone B green pulse
NODE 1 7 +
NODE 2 7 +
NODE 4 7           → All fire — all RED chase own exit, human decides
CONGEST A          → Exit A amber chase (direction unchanged)
CLEAR              → reset all
STATUS             → print all node scores and exit state
```

---

## 14. Budget

### Prototype (1 node + 1 hub)

| Component | Qty | Unit (RM) | Total (RM) |
|---|---|---|---|
| ESP32 DevKit C v4 | 2 | 15.00 | 30.00 |
| MQ-2 sensor module | 1 | 5.00 | 5.00 |
| MQ-7 sensor module | 1 | 6.00 | 6.00 |
| DHT22 module | 1 | 8.00 | 8.00 |
| LDR photoresistor module ×4 | 4 | 3.00 | 12.00 |
| WS2812B LED strip 1m | 1 | 10.00 | 10.00 |
| IR motion sensor ×3 (hub) | 3 | 5.00 | 15.00 |
| Buzzer | 1 | 3.00 | 3.00 |
| LEDs (red, green, blue) + resistors | 1 set | 3.00 | 3.00 |
| SOS + EN pushbuttons | 2 | 1.00 | 2.00 |
| Breadboard + jumper wires | 1 set | 13.00 | 13.00 |
| Foam board demo base | 1 | 3.00 | 3.00 |
| USB power supply | 1 | 10.00 | 10.00 |
| **Prototype total** | | | **RM120.00** |

### Full Floor Deployment (6 nodes + 1 hub)

| Item | Cost (RM) |
|---|---|
| 6 sensor nodes (ESP32 + sensors + LEDs + buzzer) | 402.00 |
| Hub (ESP32 + 5m LED strip + IR sensors ×3 + relay + power supply) | 234.00 |
| **Full floor total** | **RM636.00** |

---

## 15. System Objectives

ARIA addresses two simultaneous threats in residential buildings — daily indoor air quality degradation and emergency fire evacuation failure.

The system is built to:

1. Accurately detect real fire while rejecting false alarms from cooking, haze, and fog using a weighted multi-sensor fusion algorithm across distributed sensor nodes
2. Enable wireless communication between sensor nodes and a central hub using ESP-NOW protocol — a lightweight peer-to-peer radio frequency link requiring no Wi-Fi router, with up to 200m range through walls
3. Guide occupants to the nearest safe exit through dynamic floor-level LED routing that adapts to fire location and exit congestion — fire zones show red chase pointing away from danger, safe zones show green chase toward exits
4. Monitor indoor air quality continuously and activate ventilation automatically when air quality degrades, preventing Sick Building Syndrome
5. Deliver tiered Telegram alerts to residents and wardens, providing complete fire location data to brief BOMBA via 999 at critical tier
6. Detect gas leaks and apply opposite ventilation response to fire, preventing ignition

---

*End of ARIA System Specification v3 — Makerthon 2025/2026*
