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
┌────────────────────────────────────────────────────────────┐
│                         ARIA HUB                           │
│  • Collects data from up to 6 sensor nodes                 │
│  • Drives two WS2812B LED strips for exit-route guidance   │
│  • Serves a live web dashboard over Wi-Fi (AP mode)        │
│  • Sends Telegram alerts when a fire tier triggers         │
│  • Detects exit congestion via 3× LDR laser-beam sensors   │
└────────────────────────┬───────────────────────────────────┘
                         │  ESP-NOW  (802.11, channel 6)
             ┌───────────┼───────────┐
             │           │           │
        [Node 1]    [Node 2]    [Node 3]   …up to Node 6
        Zone A      Zone B      Zone B
        Exit A      Exit B      Exit B

Each Node monitors:
  • Smoke / LPG / combustibles   (MQ-2)
  • Carbon monoxide              (MQ-7 → ppm estimate)
  • Chemical air quality         (MQ-135)
  • Flame presence               (digital flame sensor)
  • Temperature & Humidity       (DHT11/DHT22)
  • SOS button                   (manual panic trigger)
  • Fan relay                    (auto ventilation)
  • Red strobe LED + buzzer      (fire / gas / SOS alert)
```

Communication is **node → hub** for sensor data and **hub → node** for remote commands — all over ESP-NOW broadcast (no router required for core operation).

---

## Hardware Requirements

### Hub (×1)

| Component | Specification |
|-----------|--------------|
| Microcontroller | ESP32 DevKit C v4 |
| LED Strip 1 | WS2812B, 30 pixels, 5V (Zone A/B/C chase) |
| LED Strip 2 | WS2812B, 40 pixels, 5V (Zone A/B/C status) |
| Buzzer | Active buzzer (3.3V or 5V) |
| LDR + laser module | ×3 — one per exit (A, B, C), with 10 kΩ pull-down |

### Node (×1 to ×6)

| Component | Specification |
|-----------|--------------|
| Microcontroller | ESP32 DevKit C v4 |
| Smoke/Gas sensor | MQ-2 (analog) |
| CO sensor | MQ-7 (analog) |
| Air quality sensor | MQ-135 (analog) |
| Temperature/Humidity | DHT11 or DHT22 |
| Flame sensor | Digital output, LOW = flame detected |
| SOS button | Momentary push-button |
| Red strobe LED | + 220 Ω resistor |
| Fan relay module | 5V relay, NO contact |
| SOS indicator LED | + 220 Ω resistor |
| Active buzzer | 3.3V/5V active type |

---

## Pin Mapping

### Hub Pins

| GPIO | Function |
|------|----------|
| 5 | WS2812B Strip 1 DIN — 30 pixels (Zone A=10, B=10, C=10) |
| 17 | WS2812B Strip 2 DIN — 40 pixels (Zone A=17, B=8, C=15) |
| 16 | Hub buzzer |
| 34 | LDR + laser beam, Exit A (10 kΩ pull-down) |
| 35 | LDR + laser beam, Exit B (10 kΩ pull-down) |
| 32 | LDR + laser beam, Exit C (10 kΩ pull-down) |

> GPIO 34, 35, 32 are input-only ADC pins — do not use pull-up/OUTPUT modes on these pins.

### Node Pins

| GPIO | Function |
|------|----------|
| 14 | DHT11/DHT22 data |
| 25 | Flame sensor (LOW = flame) |
| 26 | SOS button (INPUT_PULLUP) |
| 27 | Red strobe LED |
| 19 | Fan relay (LOW = relay energised → fan ON) |
| 13 | Active buzzer |
| 23 | SOS indicator LED |
| 33 | MQ-7 analog input |
| 34 | MQ-2 analog input |
| 35 | MQ-135 analog input |

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
| `DHT sensor library` | DHT11/DHT22 temperature/humidity (Node only) |

`WiFi`, `esp_now`, `HTTPClient`, `DNSServer` are included with the ESP32 board package.

> The Hub sketch also requires `html_page.h` — this file must be in the same folder as `ARIA_Hub_v6.ino`. It is included in this repository.

### Flash Steps

1. Open `ARIA_Hub_v6/ARIA_Hub_v6.ino` **or** `ARIA_Node_v6/ARIA_Node_v6.ino` in Arduino IDE.
2. Select board: **ESP32 Dev Module**.
3. Set partition scheme: **Default 4MB with spiffs** (or any scheme with ≥ 1.5 MB app).
4. Select the correct COM port.
5. Click **Upload**.
6. Flash the Hub first, then each Node individually.

> Hub and Node must be flashed with separate sketches — they are different programs.

---

## First-Time Setup

### Step 1 — Hub

1. Power on the Hub ESP32.
2. Connect your phone or laptop to the Wi-Fi network:
   - **SSID:** `ARIA-HUB`
   - **Password:** `aria1234`
3. Open a browser and navigate to `http://192.168.4.1` (the dashboard loads automatically).
4. Open the Arduino Serial Monitor (115200 baud) on the Hub.
5. The LDR laser-beam sensors on GPIO 34/35/32 start monitoring immediately. To verify and tune the threshold, type:
   ```
   STATUS
   ```
   Check the `LDR raw` line. If ADC readings are near the default threshold of 2000, tune it:
   ```
   LDRTHRESH 1500
   ```
   Set the threshold below the lit-beam reading but above the blocked-beam reading for your installation.

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

> Your router **must** be set to **channel 6** for both Telegram (STA mode) and ESP-NOW to operate simultaneously.

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
- Green = Safe exit route — proceed
- Red = Fire in this zone — route away
- Orange = Exit congested — use alternate exit
- Yellow = Gas leak — all strips yellow chase; avoid ignition sources
- White = Gas detected (no fire) — all strips solid white

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
| Gas / SOS | Force gas or SOS flags on individual nodes |
| Custom Command | Send any serial command directly from the browser |
| Clear All State | Reset all forced states and scores |

---

## Serial Command Reference

### Hub Commands

| Command | Description |
|---------|-------------|
| `NODE <id> <score>` | Set node fire score (0–10) |
| `NODE <id> CLEAR` | Clear forced state on one node |
| `SETSCORE <id> <0-10>` | Same as NODE, explicit syntax |
| `GAS <id> ON/OFF` | Force gas leak flag on a node |
| `SOS <id> ON/OFF` | Force SOS flag on a node |
| `FLAME <id> ON/OFF` | Force flame flag on a node |
| `NODECMD <id> <cmd>` | Send a command wirelessly to a node |
| `SETNODE <id>` | Set hub-side pending node map (1–6) or 0 to clear |
| `CONGEST A/B/C` | Force exit congestion |
| `CLEAR CONGEST A/B/C` | Clear forced congestion |
| `CLEAR OBSTRUCTION A/B/C` | Clear laser-beam obstruction alarm on an exit |
| `LDRTHRESH <1-4095>` | Set LDR laser-break ADC threshold (default 2000) |
| `CLEAR` | Reset all hub state |
| `STATUS` | Print hub + node + LDR state to serial |
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

- Protocol: ESP-NOW over 802.11 (Wi-Fi layer), **channel 6**
- No router required for core operation; nodes broadcast to `FF:FF:FF:FF:FF:FF`
- Hub broadcasts commands back to all nodes (filtered by `targetNodeID` in the packet)
- Packet size: 26 bytes (NodePacket), 34 bytes (HubCommandPacket)
- Transmit interval: **500 ms** per node
- Timeout: node considered offline after **5 seconds** of no packet
- WiFi power-save is **disabled** (`WIFI_PS_NONE`) on both hub and nodes to prevent WS2812B timing corruption

### Hub Network Modes

The hub runs `WIFI_AP_STA` — two simultaneous Wi-Fi roles:

| Role | SSID | Purpose |
|------|------|---------|
| Access Point (AP) | `ARIA-HUB` | Dashboard clients connect here |
| Station (STA) | Your router | Internet access for Telegram alerts |

> Both roles **must use the same channel** (channel 6). If your router is on a different channel, ESP-NOW to nodes will fail. Set your router to channel 6 or leave `STA_SSID` empty to disable Telegram.

### Web Server

- Framework: `ESPAsyncWebServer` (non-blocking)
- Real-time data: WebSocket at `/ws` (push every **1 second**, or immediately on state change)
- Captive portal: DNS intercepts all domains → redirects to dashboard
- Dashboard is a single self-contained HTML page served from `html_page.h` in PROGMEM

### Fire Scoring Algorithm (Node-side)

Each node calculates a **fire score (0–10)** every 500 ms:

| Condition | Points |
|-----------|--------|
| Flame confirmed (3 consecutive LOW reads) | +5 |
| MQ-2 EMA above spike threshold | +2 |
| MQ-2 EMA above slight-smoke threshold | +1 |
| CO ≥ 150 ppm (critical level) | +4 |
| CO ≥ 70 ppm (fire level) | +3 |
| CO ≥ 50 ppm (dangerous level) | +2 |
| CO ≥ 35 ppm (concern level) | +1 |
| CO ≥ 35 ppm AND rising fast (>5 ppm/window) | +1 |
| Humidity dropping rapidly (>3 % per 4-sample window) | +1 |
| MQ-135 above poor threshold | +1 |
| MQ-135 above hazard threshold | +1 |
| MQ-2 spike + CO clean + MQ-135 clean, no flame | −2 |
| MQ-2 smoke + rising humidity, no flame (steam/cooking) | −2 |
| CO concern-level only, slow rise, no smoke, no flame | −1 |
| MQ-135 elevated but MQ-2 clean, no flame (chemical vapour) | −1 |

Score is clamped to 0–10. A gas-leak pattern (MQ-2 or MQ-135 very high, CO safe, no flame, humidity stable) enters gas-leak mode **instead of** adding to the fire score.

### MQ Sensor Calibration

On startup, nodes sample all three MQ sensors **30 times over 3 seconds** in clean air. Thresholds are computed as offsets above the measured baseline:

**MQ-2 thresholds:**

| Threshold | Offset above baseline |
|-----------|----------------------|
| Slight smoke | +150 ADC counts |
| Spike (fire-like) | +400 ADC counts (min 200) |
| Gas leak | +200 ADC counts (min 200) |

**MQ-135 thresholds:**

| Threshold | Offset above baseline |
|-----------|----------------------|
| Slightly poor air | +200 ADC counts |
| Poor air quality | +500 ADC counts (min 500) |
| Hazardous air | +900 ADC counts (min 900) |

MQ-2 and MQ-135 use an **Exponential Moving Average (EMA, α = 0.3)** to smooth readings.

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

The fan relay (GPIO 19) turns ON if any of:
- MQ-2 EMA above slight-smoke threshold
- CO ppm > 35 (concern level)
- Humidity > 75 % (hysteresis: off at 70 %)
- Temperature > 30 °C (hysteresis: off at 28.5 °C)
- MQ-135 above poor threshold
- Gas leak mode active

Ventilation is **disabled** when fire score ≥ 6 (open vents would feed a fire).

### Buzzer Priority (Node-side)

| Priority | Condition | Pattern |
|----------|-----------|---------|
| 1 (highest) | Fire (score ≥ 6) | Continuous ON |
| 2 | Gas leak | 200 ms ON / 300 ms OFF |
| 3 | SOS active | Morse  · · · — — — · · · |
| 4 (idle) | Normal | Silent |

### Hub Buzzer

| Condition | Pattern |
|-----------|---------|
| Critical (score ≥ 8) | 100 ms ON / 100 ms OFF |
| Emergency (score ≥ 6) | 500 ms ON / 500 ms OFF |
| Gas leak | 200 ms ON / 300 ms OFF |
| SOS active | Morse  · · · — — — · · · |
| Normal | Silent |

### Congestion Detection (Hub LDR Laser Beams)

Three LDR + laser pairs on GPIO 34/35/32 monitor exits A, B, and C respectively. Each beam break is counted as one person passing through.

| Parameter | Value |
|-----------|-------|
| ADC threshold (default) | 2000 (tune with `LDRTHRESH`) |
| Minimum break duration (debounce) | 50 ms |
| Congestion trigger | > 23 persons in any 30-second window |
| Window reset interval | 30 seconds |
| Obstruction emergency | Beam blocked continuously > 8 seconds |

- **Congestion** is flagged immediately when the person count crosses 23 within a window; it clears at the next 30-second boundary if the rate has dropped.
- **Obstruction** (beam blocked > 8 s) sets the exit as congested and logs an emergency until the beam is restored or manually cleared with `CLEAR OBSTRUCTION A/B/C`.

---

## Alert Tiers & Scoring

Zones are formed by taking the **maximum score** across their assigned nodes.

| Tier | Zone Score | LED Colour | Hub Buzzer | Telegram |
|------|-----------|------------|------------|----------|
| 0 — Normal | 0–2 | Off | Silent | — |
| 1 — Pre-warning | 3–5 | Orange pulse | Silent | Yes (once per minute) |
| 2 — Emergency | 6–7 | Red chase | 500 ms alarm | Yes |
| 3 — Critical | 8–10 | Red chase | 100 ms rapid | Yes |

Telegram alerts include: tier label, all zone scores, exit congestion states, SOS locations, and gas locations. Alerts are rate-limited to **once per minute** per tier.

---

## LED Strip Behaviour

### Strip 1 — Exit Routing (GPIO 5, 30 pixels)

Divided into three 10-pixel zones:

```
Pixels 0–9   → Zone A (Exit A)
Pixels 10–19 → Zone B (Exit B)
Pixels 20–29 → Zone C (Exit C)
```

### Strip 2 — Zone Status (GPIO 17, 40 pixels)

Divided into three zones:

```
Pixels 0–16  → Zone A (17 pixels, chase animation)
Pixels 17–24 → Zone B (8 pixels, slow breathing status indicator)
Pixels 25–39 → Zone C (15 pixels, chase animation)
```

Zone B on Strip 2 shows a slow breathing colour (18 BPM) matching the zone's alert state — red, green, orange, or white (gas) — so it reads as a dedicated status bar rather than blending into the chase zones.

### Colour Meanings

| Colour | Meaning |
|--------|---------|
| Green | Safe zone — chase animation points toward exit |
| Red | Fire zone — animation points away from fire |
| Orange | Safe zone, but exit is congested — use alternate exit |
| Yellow | Gas leak (fire score < amber) — chase toward exits |
| White (solid) | Gas leak (no fire) — both strips solid white |
| Off | No alert |

### Chase Animation

A bright "packet" of LEDs moves across each zone. The packet grows from 1 LED to 3 LEDs on Strip 1 (1→2→3 near exit) and from 3 LEDs to 5 LEDs on Strip 2 as it approaches the exit end, signalling proximity urgency.

- **Left** = move toward Exit A
- **Right** = move toward Exit B or C

`FastLED.show()` is only called when pixel data has actually changed, which reduces interrupt interference with ESP-NOW and WebSocket during idle states.

### Exit Routing Logic

| Fire pattern | Zone A | Zone B | Zone C |
|-------------|--------|--------|--------|
| None | Off | Off | Off |
| Zone A only | Red → right | Green → right | Green → right |
| Zone B only | Green → left | Red → left | Green → right |
| Zone C only | Green → left | Green → right | Red → left |
| Zones A + C | Red → right | Green → right | Red → left |
| Zones A + B | Red → right | Red → left | Green → right |
| Zones B + C | Green → left | Red → left | Red → left |
| All zones | Red → left | Red → right | Red → right |
| Warning only (score 3–5) | Orange pulse | Orange pulse | Orange pulse |
| Gas (score < 3) | Yellow → left | Yellow → right | Yellow → right |

> If a green zone's corresponding exit is congested, the colour changes from green to orange automatically.

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

> The hub must connect to your router (STA mode) to reach the Telegram API. The router must be on **channel 6** to avoid ESP-NOW conflicts.

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
| ESP-NOW send failed (CHAN error) | Router on wrong channel | Set router to **channel 6**, or leave `STA_SSID` empty |
| MQ sensors always alarming | Calibration ran in contaminated air | Run `CALIB` in clean air |
| DHT reading NaN | Wiring issue or sensor fault | Check GPIO 14; firmware falls back to last valid reading |
| Dashboard not loading | Not connected to ARIA-HUB | Connect to `ARIA-HUB` / `aria1234` Wi-Fi first |
| LED strip shows wrong colours | FastLED colour order | Confirm strip is GRB (most WS2812B clones); change to `RGB` in `FastLED.addLeds` if needed |
| Telegram not sending | STA not connected or wrong channel | Check `STA_SSID`, `STA_PASSWORD`, router channel = 6 |
| Flame false positives | Ambient IR / sunlight | Shield flame sensor from direct sunlight; increase `FLAME_CONFIRM_READS` |
| LDR congestion triggering incorrectly | Threshold not tuned | Type `STATUS` to see raw LDR values, then set `LDRTHRESH <n>` below beam-on reading |
| Exit stuck as obstructed | Beam physically blocked | Remove obstruction; beam clears automatically, or run `CLEAR OBSTRUCTION A/B/C` |
| WS2812B flickering / corruption | WiFi radio interference | Both hub and nodes disable power-save (`WIFI_PS_NONE`) — ensure this is in effect |

---

## License

This project is developed for educational and research purposes.

---

*ARIA v6 — Hub + Node firmware for ESP32*
