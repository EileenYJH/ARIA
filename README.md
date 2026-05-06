# ARIA — Autonomous Response & Intelligence Architecture

> ESP32-based distributed fire detection and evacuation guidance system.  
> One Hub, up to six Sensor Nodes, real-time LED exit routing, and a live web dashboard — all wireless over ESP-NOW.

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Pin Mapping](#pin-mapping)
4. [Flashing the Firmware](#flashing-the-firmware)
5. [First-Time Setup](#first-time-setup)
6. [Dashboard Guide](#dashboard-guide)
7. [Serial Command Reference](#serial-command-reference)
8. [Technical Breakdown](#technical-breakdown)
9. [Alert Tiers & Scoring](#alert-tiers--scoring)
10. [LED Strip Behaviour](#led-strip-behaviour)
11. [Telegram Alerts](#telegram-alerts)
12. [Zone & Node Assignment](#zone--node-assignment)
13. [Troubleshooting](#troubleshooting)

---

## System Overview

ARIA is a two-tier wireless fire-safety system built on the ESP32 microcontroller platform.

```
┌─────────────────────────────────────────────────────┐
│                      ARIA HUB                       │
│  • Collects data from up to 6 sensor nodes          │
│  • Drives a 30-pixel WS2812B LED exit-route strip   │
│  • Serves a live web dashboard over Wi-Fi (AP mode) │
│  • Sends Telegram alerts when a fire tier triggers  │
│  • Detects exit congestion via IR motion sensor     │
└───────────────────┬─────────────────────────────────┘
                    │  ESP-NOW  (802.11, channel 11)
        ┌───────────┼───────────┐
        │           │           │
   [Node 1]    [Node 2]    [Node 3]   …up to Node 6
   Zone A      Zone B      Zone B
   Exit A      Exit B      Exit B

Each Node monitors:
  • Smoke / LPG / combustibles  (MQ-2)
  • Carbon monoxide              (MQ-7 → ppm estimate)
  • Flame presence               (digital flame sensor)
  • Temperature & Humidity       (DHT22)
  • SOS button                   (manual panic trigger)
  • Ventilation LED              (auto on/off by sensor data)
  • Red strobe LED + buzzer      (fire / gas / SOS alert)
```

Communication is **one-way from nodes to hub** for sensor data, and **hub-to-node** for remote commands — all over ESP-NOW broadcast (no router required).

---

## Hardware Requirements

### Hub (×1)

| Component | Specification |
|-----------|--------------|
| Microcontroller | ESP32 DevKit C v4 |
| LED Strip | WS2812B, 30 pixels, 5V |
| Buzzer | Active buzzer (3.3V or 5V) |
| IR Motion Sensor | HC-SR501 or equivalent |

### Node (×1 to ×6)

| Component | Specification |
|-----------|--------------|
| Microcontroller | ESP32 DevKit C v4 |
| Smoke/Gas sensor | MQ-2 (analog) |
| CO sensor | MQ-7 (analog) |
| Temperature/Humidity | DHT22 (or DHT11) |
| Flame sensor | Digital output, LOW = flame detected |
| SOS button | Momentary push-button |
| Red strobe LED | + 220Ω resistor |
| Green ventilation LED | + 220Ω resistor |
| SOS indicator LED | + 220Ω resistor |
| Active buzzer | 3.3V/5V active type |

---

## Pin Mapping

### Hub Pins

| GPIO | Function |
|------|----------|
| 5 | WS2812B LED strip data (DIN) |
| 16 | Hub buzzer |
| 34 | IR motion sensor (input) |

### Node Pins

| GPIO | Function |
|------|----------|
| 14 | DHT22 data |
| 25 | Flame sensor (LOW = flame) |
| 26 | SOS button (INPUT_PULLUP) |
| 27 | Red strobe LED |
| 12 | Green ventilation LED |
| 13 | Active buzzer |
| 23 | SOS indicator LED |
| 33 | MQ-7 analog input |
| 34 | MQ-2 analog input |

---

## Flashing the Firmware

### Prerequisites

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or PlatformIO
- ESP32 board package (`espressif/arduino-esp32` v2.x or v3.x)

### Required Libraries

Install via Arduino Library Manager:

| Library | Purpose |
|---------|---------|
| `AsyncTCP` | Async TCP for web server (Hub only) |
| `ESPAsyncWebServer` | Async HTTP + WebSocket server (Hub only) |
| `FastLED` | WS2812B LED strip control (Hub only) |
| `DHT sensor library` | DHT22 temperature/humidity (Node only) |

`WiFi`, `esp_now`, `HTTPClient`, `DNSServer` are included with the ESP32 board package.

### Flash Steps

1. Open `ARIA_Hub_v6/ARIA_Hub_v6.ino` **or** `ARIA_Node_v6/ARIA_Node_v6.ino` in Arduino IDE.
2. Select board: **ESP32 Dev Module**.
3. Set partition scheme: **Default 4MB with spiffs** (or any scheme with ≥1.5MB app).
4. Select the correct COM port.
5. Click **Upload**.
6. Flash the Hub first, then each Node individually.

> **Note:** Hub and Node must be flashed with separate sketches — they are different programs.

---

## First-Time Setup

### Step 1 — Hub

1. Power on the Hub ESP32.
2. Connect your phone or laptop to the Wi-Fi network:
   - **SSID:** `ARIA-HUB`
   - **Password:** `aria1234`
3. Open a browser and navigate to `http://192.168.4.1` (the dashboard loads automatically).
4. Open the Arduino Serial Monitor (115200 baud) on the Hub.
5. Assign the IR motion sensor to an exit (required before congestion detection works):
   ```
   SETIR A
   ```
   Replace `A` with `B` or `C` depending on which exit the sensor monitors.  
   Use `SETIR ALL` to apply the one sensor to all three exits simultaneously.

### Step 2 — Nodes

1. Power on a Node ESP32.
2. Open the Serial Monitor (115200 baud) for that Node.
3. Wait for the calibration to complete (~3 seconds, 30 samples).
4. Assign a node ID:
   ```
   SETNODE 1
   ```
   Use IDs 1–6. Each ID maps to a zone and exit (see [Zone & Node Assignment](#zone--node-assignment)).
5. The node begins transmitting to the hub every 500 ms.

> **Alternative (hub-side assignment):** On the hub dashboard, go to **Demo / Test → Hub Node Mapping**, select the target node ID, and click **Apply Mapping**. The hub will assign the next incoming unassigned packet to that node. Unassigned packets default to Node 1 automatically.

### Step 3 — Telegram (Optional)

Edit these lines in `ARIA_Hub_v6.ino` before flashing:

```cpp
#define STA_SSID          "YourWiFiName"
#define STA_PASSWORD      "YourWiFiPassword"
#define TELEGRAM_BOT_TOKEN "your_bot_token_here"
#define TELEGRAM_CHAT_ID   "your_chat_id_here"
```

See [Telegram Alerts](#telegram-alerts) for how to create the bot and find your chat ID.

---

## Dashboard Guide

Connect to `ARIA-HUB` Wi-Fi, then open `http://192.168.4.1` in any browser.

### Warden Tab

The primary view for fire wardens during an incident.

| Element | Description |
|---------|-------------|
| Zone cards (A / B / C) | Score out of 10, colour-coded status badge |
| Alert pills | Live fire / gas leak / SOS flags |
| SOS Status panel | Shows which nodes have active SOS and their zone |
| Live Floor Map | Colour-coded zone strips + exit labels (⚠ = congested) |
| Live Escape Route | Animated LED canvas showing chase direction per zone |
| Route guidance | Text instructions per zone (evacuate or use exit) |
| Warden Chat | Send messages to all connected dashboard clients |

**Legend:**
- 🟢 Green = Safe exit route — proceed
- 🔴 Red = Fire in this zone — route away
- 🟠 Orange = Exit congested — use alternate exit
- 🟡 Yellow = Gas leak detected — avoid ignition sources

### Technical Tab

For engineers and system operators.

| Element | Description |
|---------|-------------|
| Zone cards | Same as Warden tab |
| Node Connections | Live / Forced / Offline badge per node |
| Node Detail | Score, CO ppm, temperature, humidity, flame, gas, SOS per node |
| System Log | Dark terminal showing all hub events in real time |

### Demo / Test Tab

For commissioning and pre-deployment testing.

| Control | Description |
|---------|-------------|
| Hub Node Mapping | Assign the next unassigned node to a specific ID |
| Fire Scenarios | Force fire scores (7 = Fire, 9 = Critical) on individual nodes |
| Exit Congestion | Manually force/clear congestion on exits A, B, C |
| Gas / SOS | Force gas or SOS flags on Node 1 |
| Custom Command | Send any serial command directly from the browser |
| Clear All State | Reset all forced states and scores |

---

## Serial Command Reference

### Hub Commands

| Command | Description |
|---------|-------------|
| `SETIR A/B/C/ALL` | Assign D34 IR sensor to exit |
| `NODE <id> <score>` | Set node fire score (0–10) |
| `NODE <id> CLEAR` | Clear forced state on one node |
| `SETSCORE <id> <0-10>` | Same as NODE, explicit syntax |
| `GAS <id> ON/OFF` | Force gas leak flag on a node |
| `SOS <id> ON/OFF` | Force SOS flag on a node |
| `FLAME <id> ON/OFF` | Force flame flag on a node |
| `NODECMD <id> <cmd>` | Send a command wirelessly to a node |
| `SETNODE <id>` | Set hub-side pending node map |
| `CONGEST A/B/C` | Force exit congestion |
| `CLEAR CONGEST A/B/C` | Clear forced congestion |
| `CLEAR` | Reset all hub state |
| `STATUS` | Print hub + node status to serial |
| `HELP` | List all commands |

### Node Commands

| Command | Description |
|---------|-------------|
| `SETNODE <1-6>` | Assign node ID |
| `FIRE ON/OFF` | Force fire mode on/off |
| `SOS ON/OFF` | Force SOS active/inactive |
| `CALIB` | Re-run MQ sensor calibration |
| `SCORE` | Print current node state |
| `HELP` | List all commands |

Commands can be sent to nodes remotely from the hub using:
```
NODECMD <nodeID> <command>
```
Example: `NODECMD 3 CALIB` — runs calibration on Node 3.

---

## Technical Breakdown

### Communication — ESP-NOW

- Protocol: ESP-NOW over 802.11 (Wi-Fi layer), **channel 11**
- No router required; nodes broadcast to hub MAC `FF:FF:FF:FF:FF:FF`
- Hub also broadcasts commands back to all nodes (targeted by `targetNodeID` field)
- Packet size: 14 bytes (NodePacket), 34 bytes (HubCommandPacket)
- Transmit interval: **500 ms** per node
- Timeout: node considered offline after **5 seconds** of no packet

### Hub Network Modes

The hub runs `WIFI_AP_STA` — two simultaneous Wi-Fi roles:

| Role | SSID | Purpose |
|------|------|---------|
| Access Point (AP) | `ARIA-HUB` | Dashboard clients connect here |
| Station (STA) | Your router | Internet access for Telegram alerts |

> Both roles **must use the same channel** (channel 11). If your router is on a different channel, ESP-NOW to nodes will fail. Set your router to channel 11 or disable STA/Telegram.

### Web Server

- Framework: `ESPAsyncWebServer` (non-blocking)
- Real-time data: WebSocket at `/ws` (push every **1 second**)
- Captive portal: DNS intercepts all domains → redirects to dashboard
- Dashboard is a single self-contained HTML page (~160KB with embedded logo)

### Fire Scoring Algorithm (Node-side)

Each node calculates a **fire score (0–10)** every 500 ms:

| Condition | Points |
|-----------|--------|
| MQ-2 EMA above spike threshold | +1 |
| CO > concern level AND rising fast | +3 |
| CO > 30 ppm | +2 |
| CO > concern level | +1 |
| Humidity dropping rapidly (>3%/sample window) | +1 |
| Flame confirmed (3 consecutive LOW reads) | +3 |
| MQ-2 spike but CO normal (steam/dust false positive) | −2 |
| MQ-2 slight + humidity rising (steam/cooking) | −2 |
| CO slightly high but stable, no flame, no spike | −1 |

Score is clamped to 0–10. A gas leak pattern (MQ-2 very high, CO safe, no flame, humidity stable) **bypasses fire scoring** and enters gas-leak mode instead.

### MQ Sensor Calibration

On startup, nodes sample MQ-2 and MQ-7 **30 times over 3 seconds** in clean air. The average becomes the baseline. Thresholds are computed as offsets:

| Threshold | Offset above baseline |
|-----------|----------------------|
| Slight smoke | +150 ADC counts |
| Spike (fire-like) | +400 ADC counts (min 200) |
| Gas leak | +700 ADC counts (min 200) |

MQ-2 uses an **Exponential Moving Average (EMA, α=0.3)** to smooth readings.

### CO PPM Estimation (MQ-7)

The MQ-7 raw ADC reading is converted to a CO ppm estimate using a piecewise ratio model:

```
ratio = mq7Raw / mq7Baseline

ratio > 1.5  →  0 ppm   (clean air)
1.0–1.5      →  5–20 ppm
0.7–1.0      →  20–100 ppm
0.5–0.7      →  100–200 ppm
< 0.5        →  200–300 ppm
```

### Ventilation Logic (Node-side)

The green ventilation LED turns on if any of:
- MQ-2 EMA above slight-smoke threshold
- CO ppm above concern level (35 ppm)
- Humidity > 75% (hysteresis: off at 70%)
- Temperature > 30°C (hysteresis: off at 28.5°C)
- Gas leak mode active

Ventilation is **disabled** when fire score ≥ 5 (open vents would feed a fire).

### Buzzer Priority (Node-side)

| Priority | Condition | Pattern |
|----------|-----------|---------|
| 1 (highest) | Fire (score ≥ 5) | Continuous ON |
| 2 | Gas leak | 200ms ON / 300ms OFF |
| 3 | SOS active | SOS Morse (· · · — — — · · ·) |
| 4 (idle) | Normal | Silent |

### Hub Buzzer

| Condition | Pattern |
|-----------|---------|
| Critical (score ≥ 8) | 100ms ON / 100ms OFF |
| Emergency (score ≥ 5) | 500ms ON / 500ms OFF |
| Gas leak | 200ms ON / 300ms OFF |
| SOS | 150ms ON / 150ms OFF |
| Normal | Silent |

### Congestion Detection (Hub IR)

A single HC-SR501 IR sensor on GPIO 34 monitors one or all exits. Congestion is flagged when any of:

- Continuous motion for **≥ 2 seconds**
- Motion occupies **> 30%** of a 10-second rolling window
- **≥ 5** motion events in the last 60-second rate window
- **≥ 4** events total in the rate window

Once flagged, congestion is held for **8 seconds** after the last trigger. Congested exits are shown in orange on the dashboard and LED strip.

---

## Alert Tiers & Scoring

Zones are formed by taking the **maximum score** across their assigned nodes.

| Tier | Zone Score | LED Colour | Hub Buzzer | Telegram |
|------|-----------|------------|------------|----------|
| 0 — Normal | 0–2 | Off | Silent | — |
| 1 — Pre-warning | 3–4 | Orange pulse | Silent | Yes (once) |
| 2 — Emergency | 5–7 | Red chase | 500ms alarm | Yes |
| 3 — Critical | 8–10 | Red chase (fast) | 100ms rapid | Yes |

Telegram alerts include: tier label, all zone scores, exit congestion states, SOS locations, and gas locations. Alerts are rate-limited to **once per minute** per tier.

---

## LED Strip Behaviour

The 30-pixel WS2812B strip is divided into three 10-pixel zones:

```
Pixels 0–9   → Zone A (Exit A)
Pixels 10–19 → Zone B (Exit B)
Pixels 20–29 → Zone C (Exit C)
```

### Colour Meanings

| Colour | Meaning |
|--------|---------|
| Green | Safe zone — chase animation points toward exit |
| Red | Fire zone — animation points **away** from exit |
| Orange | Safe zone, but exit is congested |
| Yellow | Gas leak — all zones pulse yellow |
| Off | No alert |

### Chase Animation

A single bright "lead" pixel moves across each zone. The direction indicates evacuation flow:

- **Left (toward Exit A/B)** = move this way
- **Right (toward Exit C/B)** = move this way
- **Split (Zone B)** = two sub-strips chase outward to Exit A and Exit C

Trail pixels behind the lead fade from 60% → 35% → 15% brightness and **pack more densely near the exit** to signal proximity urgency.

### Routing Logic

| Fire pattern | Zone A | Zone B | Zone C |
|-------------|--------|--------|--------|
| No fire | Off | Off | Off |
| Zone A only | Red→right | Green split | Green→right |
| Zone B only | Green→left | Red split | Green→right |
| Zone C only | Green→left | Green split | Red→left |
| Zone A + C | Red→right | Green pulse | Red→left |
| Zone A + B | Red→right | Red→right | Green→right |
| Zone B + C | Green→left | Red→left | Red→left |
| All zones | Red→right | Red split | Red→left |

---

## Telegram Alerts

### Create a Bot

1. Open Telegram and message **@BotFather**.
2. Send `/newbot`, follow the prompts, copy the **API token**.

### Get Your Chat ID

1. Start a conversation with your new bot.
2. Send any message.
3. Visit: `https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates`
4. Find `"chat":{"id": <number>}` — that is your Chat ID.

### Configure in Firmware

```cpp
#define STA_SSID          "YourRouter"
#define STA_PASSWORD      "YourPassword"
#define TELEGRAM_BOT_TOKEN "123456789:ABCdef..."
#define TELEGRAM_CHAT_ID   "987654321"
```

> The hub must connect to your router (STA mode) to reach the Telegram API. The router must be on **channel 11** to avoid ESP-NOW conflicts.

---

## Zone & Node Assignment

| Node ID | Zone | Nearest Exit |
|---------|------|-------------|
| 1 | A | Exit A |
| 2 | B | Exit B |
| 3 | B | Exit B |
| 4 | C | Exit C |
| 5 | A | Exit A |
| 6 | C | Exit C |

Zone score = `max(node score in zone)`.  
If a node is offline and not forced, its score is treated as 0.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Node not appearing on dashboard | Node ID not assigned | Run `SETNODE <1-6>` on the node, or use hub-side mapping |
| ESP-NOW send failed (CHAN error) | Router on wrong channel | Set router to channel 11, or leave STA unconfigured |
| MQ sensors always alarming | Calibration ran in contaminated air | Run `CALIB` in clean air |
| DHT22 reading NaN | Wiring issue or sensor fault | Check GPIO 14 connection; system uses last valid reading as fallback |
| Dashboard not loading | Not connected to ARIA-HUB | Connect to `ARIA-HUB` / `aria1234` Wi-Fi first |
| LED strip shows wrong colours | FastLED colour order | Confirm strip is GRB (most WS2812B clones); change to `RGB` in `FastLED.addLeds` if needed |
| Telegram not sending | STA not connected or wrong channel | Check `STA_SSID`, `STA_PASSWORD`, router channel = 11 |
| Flame false positives | Ambient IR / sunlight | Shield flame sensor from direct sunlight; increase `FLAME_CONFIRM_READS` |

---

## License

This project is developed for educational and research purposes.

---

*ARIA v6 — Hub + Node firmware for ESP32*
