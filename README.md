# ARIA — Adaptive Room Intelligence & Alert
### Makerthon 2025/2026 | Eileen Yeoh

---

## Project Structure

```
ARIA_Project/
├── README.md                    ← This file
├── ARIA_SYSTEM_SPEC_v3.md       ← Full system specification
│
├── ARIA_Node_v6/
│   └── ARIA_Node_v6.ino         ← Sensor node firmware (flash to all 6 nodes)
│
├── ARIA_Hub_v6/
│   └── ARIA_Hub_v6.ino          ← Hub firmware (flash to hub ESP32)
│
├── Wokwi_Node/
│   ├── diagram.json             ← Paste into Wokwi node project
│   └── libraries.txt            ← Libraries to install in Wokwi
│
└── Wokwi_Hub/
    ├── diagram.json             ← Paste into Wokwi hub project
    └── libraries.txt            ← Libraries to install in Wokwi
```

---

## Quick Start

### Real Hardware

**Node (×6 ESP32 DevKit C v4):**
1. Open `ARIA_Node_v6/ARIA_Node_v6.ino` in Arduino IDE
2. Install library: `DHT sensor library` by Adafruit
3. Flash to all 6 ESP32 boards — same firmware for all
4. Open Serial Monitor at 115200 baud
5. Type `SETNODE 1` (or 2–6) to assign node ID before transmitting

**Hub (×1 ESP32 DevKit C v4):**
1. Open `ARIA_Hub_v6/ARIA_Hub_v6.ino` in Arduino IDE
2. Install libraries: `AsyncTCP`, `ESPAsyncWebServer`, `FastLED`
3. Flash to hub ESP32
4. Connect to Wi-Fi network `ARIA-HUB` (password: `aria1234`)
5. Open browser → `http://192.168.4.1`

---

### Wokwi Simulation

Two separate Wokwi projects required — node and hub cannot share one project.

**Node project:**
1. Go to [wokwi.com](https://wokwi.com) → New Project → ESP32
2. Replace `diagram.json` with contents of `Wokwi_Node/diagram.json`
3. Replace `sketch.ino` with contents of `ARIA_Node_v6.ino`
4. Install library: `DHT sensor library`
5. Press Play
6. Type `SETNODE 1` in Serial Monitor

**Hub project:**
1. Go to [wokwi.com](https://wokwi.com) → New Project → ESP32
2. Replace `diagram.json` with contents of `Wokwi_Hub/diagram.json`
3. Replace `sketch.ino` with contents of `ARIA_Hub_v6.ino`

> ⚠️ Hub firmware uses `AsyncTCP`, `ESPAsyncWebServer`, and `FastLED` which
> are not fully supported in Wokwi. For Wokwi simulation use the hub-wokwi
> version with Adafruit NeoPixel instead (see Wokwi section below).

---

## Pin Maps

### Node ESP32 (GPIO)

| GPIO | Component | Role |
|------|-----------|------|
| 14 | DHT22 | Temperature + humidity |
| 25 | LDR4 / Flame sensor | Flame detection (LOW = flame) |
| 26 | SOS button | INPUT_PULLUP — toggle SOS |
| 27 | Red LED (strobe) | 4Hz flash during fire/gas |
| 23 | Blue LED (SOS) | Synced with SOS morse pattern |
| 12 | Green LED (vent) | Ventilation active indicator |
| 13 | Buzzer | Fire siren + SOS morse |
| 33 | MQ-2 smoke | Analog ADC1 |
| 34 | MQ-7 CO | Analog ADC1 |
| EN | EN button | Hardware reset + recalibration |

### Hub ESP32 (GPIO)

| GPIO | Component | Role |
|------|-----------|------|
| 5 | WS2812B LED strip DIN | 30 pixels — 3 strips × 10px |
| 16 | Buzzer | Emergency/critical alarm |
| 18 | Fan relay LED (red) | Fan relay stand-in |
| 19 | Vent indicator LED (blue) | Ventilation status |
| 34 | IR motion sensor | Exit congestion detection |

---

## Zone Mapping

```
[Node1][Node2][Node3][Node4]
|─────────────────────────────|
|  [Node5]  junction  [Node6] |
Exit A          |              Exit C
                |
             Exit B
```

| Zone | Nodes | Exit |
|------|-------|------|
| Zone A | Node 1, Node 5 | Exit A (left staircase) |
| Zone B | Node 2, Node 3 | Exit B (centre via junction) |
| Zone C | Node 4, Node 6 | Exit C (right staircase) |

### LED Strip Layout (30 pixels)

```
strip1 (10px horizontal) → Zone A → Exit A
strip2 (10px vertical)   → Zone B → Exit B (junction branch down)
strip3 (10px horizontal) → Zone C → Exit C
```

---

## Node ID Assignment

All 6 nodes run **identical firmware**. ID is assigned via Serial Monitor after boot.

```
Startup message:
  ARIA Node v6 - awaiting node ID assignment
  Type: SETNODE 1  (or 2, 3, 4, 5, 6)

Commands:
  SETNODE 1    → Node 1, Zone A, Exit A
  SETNODE 2    → Node 2, Zone B, Exit B
  SETNODE 3    → Node 3, Zone B, Exit B
  SETNODE 4    → Node 4, Zone C, Exit C
  SETNODE 5    → Node 5, Zone A, Exit A
  SETNODE 6    → Node 6, Zone C, Exit C
```

> Pressing the EN button performs hardware reset and re-runs sensor
> calibration automatically. Node ID must be re-assigned after EN reset.

---

## Startup Calibration

On every boot the node reads MQ-2 and MQ-7 **30 times over 3 seconds**
and calculates the environment baseline. All thresholds are derived from
this baseline automatically. Serial Monitor prints every sample.

```
Thresholds set after calibration:
  mq2SlightSmoke = baseline + 150
  mq2Spike       = max(baseline + 400, 200)
  mq2GasLeak     = max(baseline + 700, 200)
```

Type `CALIB` in Serial Monitor to recalibrate without hardware reset.

---

## Fire Score Algorithm

| Condition | Points |
|-----------|--------|
| MQ-2 above spike threshold | +1 |
| CO > threshold AND rising rate > 10 | +3 |
| CO sustained above 30ppm | +2 |
| CO moderately elevated | +1 |
| Humidity dropping > 3% | +1 |
| Flame confirmed (3 consecutive reads) | +3 |
| Smoke + low CO (cooking/fog) | −2 |
| Smoke + humidity rising (steam) | −2 |
| CO slow rise, no flame (haze) | −1 |

| Score | Alert | Tier |
|-------|-------|------|
| 0–2 | Normal | IAQ monitoring |
| 3–4 | Pre-warning | Tier 1 — admin notified |
| 5–7 | Emergency | Tier 2 — evacuate |
| 8–10 | Critical | Tier 3 — warden contacts BOMBA 999 |

---

## LED Behaviour

| Situation | Zone A | Zone B | Zone C |
|-----------|--------|--------|--------|
| No fire | OFF | OFF | OFF |
| Zone A fire | RED chase → right (away from A) | GREEN chase → Exit B | GREEN chase → Exit C |
| Zone B fire | GREEN chase → Exit A | RED chase split left+right | GREEN chase → Exit C |
| Zone C fire | GREEN chase → Exit A | GREEN chase → Exit B | RED chase → left (away from C) |
| Zone A + C fire | RED chase → right toward B | GREEN pulse (Exit B only) | RED chase → left toward B |
| All zones fire | RED chase → Exit A | RED chase → Exit B | RED chase → Exit C |
| Pre-warning | AMBER pulse 1Hz | AMBER pulse 1Hz | AMBER pulse 1Hz |
| Congested exit | Amber chase (same direction as green) | Amber chase | Amber chase |

Proximity packing: LEDs get denser approaching the exit, sparser when far away.
This applies to both green and amber — congestion only changes colour, never direction.

---

## SOS System

- **1st press** → SOS activates, morse begins
- **2nd press** → SOS stops immediately

**Morse pattern:** `· · · — — — · · ·`

| Symbol | Duration |
|--------|----------|
| Dot (·) | 150ms ON |
| Dash (—) | 450ms ON |
| Symbol gap | 150ms OFF |
| Group gap | 450ms OFF |
| Repeat gap | 1500ms OFF |

SOS LED (GPIO 23) blinks in exact sync with buzzer.

**Buzzer priority:**

| Priority | Condition | Sound |
|----------|-----------|-------|
| 1 | Fire emergency | Alternating 880/1100Hz every 500ms |
| 1 | Fire critical | Triple-beep 1500Hz fast pattern |
| 2 | Gas leak | Intermittent 1500Hz 2Hz pattern |
| 3 | SOS | Morse 2500Hz clean beeps |
| 4 | Idle | Silent |

---

## Hub Serial Commands

| Command | Effect |
|---------|--------|
| `NODE <id> <score>` | Set fire score e.g. `NODE 1 7` |
| `SETSCORE <id> <0-10>` | Same as NODE but explicit |
| `GAS <id> ON/OFF` | Force gas leak on node |
| `SOS <id> ON/OFF` | Force SOS on node |
| `FLAME <id> ON/OFF` | Force flame flag on node |
| `NODE <id> CLEAR` | Clear forced state on node |
| `NODECMD <id> <cmd>` | Send command to physical node e.g. `NODECMD 1 FIRE ON` |
| `SETNODE 0 <id>` | Assign ID to unassigned node wirelessly |
| `SETIR A/B/C/ALL` | Assign IR motion sensor to exit |
| `CONGEST A/B/C` | Force congestion at exit |
| `CLEAR CONGEST A/B/C` | Clear forced congestion |
| `CLEAR` | Reset all state |
| `STATUS` | Print all node and exit state |
| `HELP` | List all commands |

---

## Node Serial Commands

| Command | Effect |
|---------|--------|
| `SETNODE <1-6>` | Assign node ID — required before transmit |
| `FIRE ON` | Force fire mode score 7 |
| `FIRE OFF` | Reset fire mode |
| `SCORE` | Print current score and state |
| `CALIB` | Re-run sensor calibration |
| `HELP` | List commands |

---

## Web Dashboard

Connect to `ARIA-HUB` Wi-Fi → open `http://192.168.4.1`

### Warden View (default page)
- Live evacuation map showing zone colours and exit status
- Alert pills — Fire / Gas / SOS / Vent
- SOS panel with location
- Warden chat box — send messages and commands

### Technical View
- Node selector dropdown — assign node ID from dashboard
- All 6 node data cards — score, CO, temp, humidity, flame, gas, SOS
- System log — all events timestamped
- Quick action buttons — Node 1–6 fire, Congest A/B/C, Clear
- Full command input

---

## Wokwi Demo Commands (hub Serial Monitor)

```
NODE 1 7           → Zone A fire — Zone A RED right, B+C green
NODE 2 7           → Zone B fire — Zone B RED split, A+C green
NODE 4 7           → Zone C fire — Zone C RED left, A+B green
NODE 1 7 + NODE 4 7 → A+C fire — Zone B green pulse only
CONGEST A          → Exit A amber (direction unchanged)
CONGEST B          → Exit B amber
CLEAR              → reset all
STATUS             → print state
```

**LDR beam simulation (hub):**
- Drag light slider to 0 = beam blocked = person present
- Release = person cleared = count +1
- Hold at 0 for 2s+ = congested immediately

---

## Budget

### Prototype (1 node + 1 hub) — RM120

| Component | Qty | Unit (RM) | Total (RM) |
|-----------|-----|-----------|------------|
| ESP32 DevKit C v4 | 2 | 15.00 | 30.00 |
| MQ-2 module | 1 | 5.00 | 5.00 |
| MQ-7 module | 1 | 6.00 | 6.00 |
| DHT22 module | 1 | 8.00 | 8.00 |
| LDR photoresistor module ×4 | 4 | 3.00 | 12.00 |
| WS2812B LED strip 1m | 1 | 10.00 | 10.00 |
| IR motion sensor ×3 | 3 | 5.00 | 15.00 |
| Buzzer | 1 | 3.00 | 3.00 |
| LEDs + resistors | 1 set | 3.00 | 3.00 |
| Pushbuttons (SOS + EN) | 2 | 1.00 | 2.00 |
| Breadboard + jumper wires | 1 set | 13.00 | 13.00 |
| Foam board demo base | 1 | 3.00 | 3.00 |
| USB power supply | 1 | 10.00 | 10.00 |
| **Total** | | | **RM120.00** |

### Full Floor Deployment (6 nodes + 1 hub) — RM636

| Item | Cost (RM) |
|------|-----------|
| 6 sensor nodes | 402.00 |
| Hub with full corridor strip + IR sensors | 234.00 |
| **Total** | **RM636.00** |

---

## Power Supply

System designed to connect to building existing emergency power
infrastructure under UBBL 1984 and MS 1745.

For prototype/demo: USB power banks (one per ESP32).

---

## Libraries

### Node (Arduino IDE)
- `DHT sensor library` by Adafruit
- `WiFi.h` (built-in ESP32)
- `esp_now.h` (built-in ESP32)

### Hub (Arduino IDE)
- `AsyncTCP`
- `ESPAsyncWebServer`
- `FastLED`
- `WiFi.h` (built-in)
- `esp_now.h` (built-in)
- `DNSServer.h` (built-in)

---

## Known Issues / Notes

- Hub firmware uses `WIFI_AP_STA` mode. STA credentials are empty by default
  (AP-only). To enable Telegram alerts, fill in `STA_SSID`, `STA_PASSWORD`,
  `TELEGRAM_BOT_TOKEN`, and `TELEGRAM_CHAT_ID` in hub firmware, and ensure
  your router is set to **channel 1** to avoid ESP-NOW conflicts.

- EN button resets node ID — must re-run `SETNODE` after EN reset.

- In Wokwi, ESP-NOW between two projects is not supported. Bridge data
  manually by watching node Serial Monitor and typing `NODE <id> <score>`
  in hub Serial Monitor.

- Hub dashboard node selector sends `SETNODE 0 <id>` which targets any
  unassigned node (ID 0). If all nodes already have IDs assigned, use
  `NODECMD <targetID> SETNODE <newID>` to reassign a specific node.

---

*ARIA — Adaptive Room Intelligence & Alert*
*Makerthon 2025/2026 | Eileen Yeoh*
