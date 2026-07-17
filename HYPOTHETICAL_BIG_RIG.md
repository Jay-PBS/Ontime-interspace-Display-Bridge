# HYPOTHETICAL_BIG_RIG.md — "The Big Rig"

**Status: thought experiment, not a build sheet.** This is the answer to "what if the tiny XIAO bridge grew up and became the whole control node." It scopes a single multi-function box that runs the Ontime Display Bridge *and* everything else — Docker services, Pi-hole, network tooling, live-show device control — off one power feed, in one flightcase, with real pins to the outside world.

The honest headline first: **do not put hard-real-time device timing and a Linux services box on the same chip.** The whole reason v2 exists (see [PROJECT.md](PROJECT.md) §1) is that a busy general-purpose stack is a reliability risk on a live show. The Big Rig keeps that lesson: it is **two planes bolted into one enclosure**, not one CPU trying to do both.

---

## 1. The one idea

```
              ┌────────────────────────── THE BIG RIG ──────────────────────────┐
              │                                                                  │
              │   COMPUTE PLANE (Linux)              REAL-TIME PLANE (MCU)        │
              │   ┌───────────────────────┐          ┌────────────────────────┐  │
  LAN ════════╪══▶│ SBC / mini-PC         │◀── USB ─▶│ MCU co-processor       │  │
  WiFi ═══════╪══▶│  Docker: Ontime,      │  or UART │  (ESP32-S3 / RP2040 /  │  │
  BT ═════════╪══▶│  Pi-hole, Node-RED,   │          │   Teensy 4.1)          │  │
              │   │  MQTT, dashboards     │          │  deterministic GPIO,   │  │
              │   │                       │          │  RS232/DMX/relay timing│  │
              │   └───────────┬───────────┘          └───────────┬────────────┘  │
              │               │ services / config                │ pins          │
              │               ▼                                  ▼               │
              │        [ web UI, APIs ]                  [ GPIO / XLR / DMX /     │
              │                                            relay / serial banks ] │
              └──────────────────────────────────────────────────────────────────┘
                         one 12V feed · one case · one power switch
```

- **Compute plane** = a proper Linux machine. Runs containers, holds config, talks to the network, hosts the Ontime server itself if you want it local. This is where "run other systems at the same time" lives.
- **Real-time plane** = an MCU (or two) doing exactly what the XIAO does today, just more of it. It owns anything where *timing is the product*: the Interspace RS232 frames, DMX, relay pulses, GPIO edges. It never blocks on the network or a container restart.
- The two talk over **USB-CDC or a UART link**. The MCU exposes a tiny line protocol ("show frame X", "pulse relay 3"); the Linux side decides *what* to send, the MCU guarantees *when*.

Everything below is choices for filling in those two boxes.

---

## 2. Why split it (the part people skip)

| Concern | Put it on Linux? | Put it on the MCU? |
|---|---|---|
| Bit-accurate RS232 to the Interspace display | ✗ — jitter under load, no boot silence guarantee | ✓ — this is literally today's firmware |
| DMX512 / relay timing for cues | ✗ — USB-DMX works but adds latency + a failure point | ✓ |
| Docker, Pi-hole, web dashboards, VPN | ✓ | ✗ — no OS, no room |
| Survives a container crash mid-show | The MCU keeps driving the display; Linux reboots underneath it | ✓ |
| Firmware you can reason about at 2am | Big surface | Small surface — keep it boring |

The split is the reliability feature. A live show tolerates "the dashboard went funny"; it does not tolerate "the countdown froze because Pi-hole got busy."

---

## 3. Compute plane — pick a tier

| Tier | Board | Why | Rough £ |
|---|---|---|---|
| **A — Pi-native** | Raspberry Pi 5 (8GB) + active cooler + NVMe HAT | You already run Docker on a Pi; same muscle memory. 40-pin GPIO is a bonus real-time-ish header. PoE HAT option. | ~120–170 |
| **B — x86 mini** | Beelink / Minisforum N100 or Ryzen mini-PC | Real x86, more containers, faster Ontime + DB + dashboards, dual NIC on some. Handles VMs, not just containers. | ~150–300 |
| **C — industrial** | Fanless DIN-rail x86 (e.g. OnLogic-class) with 2–4 NICs + wide-input DC | The "it lives in a flightcase for 10 years" answer. Wide 9–36V input, no fan to fail. | ~400+ |

**Recommendation for a "mega test + live rig": Tier B.** A Pi 5 (Tier A) is the sentimental pick and totally viable; an N100 mini-PC gives you headroom to run everything at once without thinking about it, and x86 makes some tooling (certain container images, nested virtualization) painless. Tier C only if it's going to be permanently racked and abused.

---

## 4. Real-time plane — pick your MCU

Reuse the v2 brain where you can — you already have working, hardened firmware for it.

| MCU | Best for | Notes |
|---|---|---|
| **Seeed XIAO ESP32-C3** (today's) | The single Interspace bridge, unchanged | Keep one as-is. It's proven. Don't rewrite what works. |
| **ESP32-S3** | The upgrade path | More GPIO, USB-OTG native, PSRAM, WiFi+BT. Runs the same Arduino stack. Natural "bridge, but bigger." |
| **Teensy 4.1** | Serious pin count + hard timing | 600MHz M7, tons of GPIO, real Ethernet option, rock-solid UART/DMX. Overkill until it isn't. |
| **RP2040 / RP2350 (Pico 2)** | Cheap, PIO state machines | PIO is unfair for bit-banged protocols (DMX, custom serial) — offloads timing off the CPU entirely. |

**Recommendation: ESP32-S3 as the general co-processor, plus keep a dedicated XIAO-C3 running the exact v2 firmware for the Interspace display.** One MCU per *critical* real-time job beats one MCU multiplexing all of them. Cheap to add, and a wedged relay driver then can't take the countdown with it.

---

## 5. I/O — the "multiple pinout control" you asked for

Break the MCU pins out to labelled, protected connectors on the panel. Don't hang bare jumper wires in a flightcase.

| Bank | Connector | Driven by | Use |
|---|---|---|---|
| **Serial / RS232** | XLR + DB9 (MAX3232 per line) | MCU UART | Interspace display (today), plus spare RS232 for projectors/switchers |
| **DMX512** | 3-pin + 5-pin XLR (MAX485) | MCU UART / PIO | Fixtures, house lights cue |
| **Relays** | Pluggable terminal block, opto-isolated | MCU GPIO → relay board | Mains-ish switching, GPO triggers, "go" contacts |
| **GPIO / logic** | Screw terminals, level-shifted + TVS | MCU GPIO | Buttons, lamps, sensors, tally |
| **Analog in** | Terminal block | MCU ADC | Sensors, faders |
| **Status** | Panel OLED (SSD1306 0x3C) + RGB LED | MCU I2C/GPIO | Same local-status ethos as v2 |

Carry the v2 traps forward verbatim on the MCU side — they still bite:
- Named `PIN_` constants, **never** bare `Dn` from copied code ([CLAUDE.md](CLAUDE.md) trap 2).
- Every external line gets **TVS + series resistance**; this box lives in a flightcase and things get plugged in hot.
- **Opto-isolate** relay and any mains-adjacent bank. No shared ground with logic.
- **Boot silence** (trap 9): no bytes on any output line until a valid, intended frame. Nothing twitches at power-on.

---

## 6. Connectivity — USB / LAN / WiFi / BT, all of it

| Path | Lives on | Role |
|---|---|---|
| **LAN (1–2× RJ45)** | Compute plane | Primary. Ontime, dashboards, SSH, VPN. Dual NIC = show-net / house-net separation. |
| **WiFi** | Compute plane (AP+STA) and/or MCU | STA to join a network; AP mode to serve a config/dashboard when there's no infra. Mirrors v2's setup portal idea, scaled up. |
| **Bluetooth / BLE** | Compute plane and/or ESP32-S3 | Phone-in-your-pocket status, BLE beacons, a cheap wireless "go" button. |
| **USB host** | Compute plane | The MCU link, USB-serial gadgets, a USB-DMX dongle, storage, a keyboard for on-site fixes. |
| **USB-CDC** | MCU ↔ Compute | The internal control channel. Same CDC-On-Boot discipline as v2 (trap 4). |

Design rule: **LAN is the show path, WiFi/BT are convenience paths.** Don't make a live countdown depend on Bluetooth. WiFi/BT are for config, monitoring, and the "I forgot the laptop" case.

---

## 7. Software stack (compute plane)

Everything in Docker Compose so the whole rig is one `git pull && docker compose up -d`.

```
┌─ docker compose ──────────────────────────────────────────────┐
│  ontime          the stage timer server (or point at a remote) │
│  bridge-gateway  small service: Linux ⇄ MCU line protocol      │
│  pi-hole         DNS / adblock  (your existing use case)        │
│  node-red        glue: cues, MQTT, HTTP, GPIO orchestration     │
│  mosquitto       MQTT broker — the rig's internal message bus   │
│  grafana + db    dashboards / logging for the "test rig" hat    │
│  portainer       manage all of the above from a browser         │
│  wireguard       remote access without exposing anything        │
└────────────────────────────────────────────────────────────────┘
```

- **`bridge-gateway`** is the only new bespoke piece: it owns the USB/UART link to the MCU, republishes MCU state onto MQTT, and turns MQTT/HTTP commands into MCU frames. Keep it as boring as the firmware.
- **Node-RED + MQTT** is what makes it a *control* rig rather than a bunch of parallel services — it's where "when Ontime hits overtime, pulse relay 2 and go DMX red" gets wired without recompiling anything.
- Pin containers to versions. A live rig is not where you discover a breaking image update.

---

## 8. Power & physical

One feed in, everything derived — same philosophy as the v2 "Power Injector," scaled.

```
 12V DC IN (or wide 9–36V on Tier C)
   │  fuse ─ ideal-diode reverse protection ─ TVS
   ├─▶ Compute plane (Pi 5 needs 5V/5A; mini-PC often wants 12–19V — size the buck/boost)
   ├─▶ 12V rail  → Interspace display feed (XLR pin 3), as today
   ├─▶ 5V rail   → MCU(s), relay coils, OLED
   └─▶ isolated rail → opto / mains-adjacent bank
```

- **Size the PSU for worst case, then add headroom.** A Pi 5 + NVMe + WiFi + relays pulling in can brown out a marginal supply mid-show — the exact failure class v2 was built to kill.
- **UPS / hold-up.** A small DC UPS or supercap hold-up so a flickery venue supply doesn't reboot the rig. This is the single biggest reliability upgrade over the tiny bridge.
- **Flightcase, panel-mount everything.** RP-SMA whip antenna(s) out the panel — a metal case is a Faraday cage (PROJECT.md §1, antenna row). Passive/large heatsink over fans where you can; fans are the first thing to fail dusty.
- **One panel switch, labelled banks, a fuse you can reach.** It gets used at 2am in the dark.

---

## 9. Two hats: Test Rig vs Live Rig

The same hardware, two modes — declare which you're in.

| | **Test / bench mode** | **Live / show mode** |
|---|---|---|
| Services | Everything on — Grafana, extra containers, experiments | Minimal set: Ontime + bridge-gateway + what the show needs |
| Network | Open, WiFi AP up, SSH wide | Locked down, WireGuard only, WiFi optional |
| Logging | Verbose, `[WiFi] [WS] [CFG] [TX]` to console | Persistent to disk, quiet console |
| Updates | Freely | **Frozen.** No `docker pull` on show day. |
| MCU firmware | Debug build | Release build, boot-silent, watchdog on |

Make the mode a switch (a real toggle or a `MODE=live` env) so nobody has to remember the whole checklist under pressure.

---

## 10. Reliability carry-overs (non-negotiable)

Straight from the v2 hard-won list — they scale up unchanged:

1. **Watchdog on the MCU**, fed in `loop()`. A wedged co-processor must reset itself, not freeze the display (CLAUDE.md §State machine).
2. **Stateless recovery** beats a clever stuck state. WiFi lost >30s → reconnect; wedged >5min → restart. Prefer a clean reboot to a half-alive TLS stack mid-show.
3. **Rate-limit outputs** — value-change + 1Hz refresh, not per-message (trap 7). Applies to every output bank, not just the display.
4. **The display is sacred.** Even if Linux is rebooting, the dedicated Interspace MCU keeps driving the last valid frame. Decouple its power/logic from the compute plane so a mini-PC reboot doesn't blank the countdown.
5. **Keep the firmware small and commented on the WHY** (CLAUDE.md §Style). The Big Rig's complexity belongs in containers you can restart, never in the real-time firmware you can't debug live.

---

## 11. Rough phased path (if this ever stopped being hypothetical)

1. **Keep the v2 bridge exactly as-is.** It's the proven core. Don't touch it.
2. **Add the compute plane beside it.** Mini-PC/Pi running Docker, Ontime local, Pi-hole — talking to the *existing* bridge over the network. No new hardware risk.
3. **Add `bridge-gateway` + MQTT** so services can see bridge state. Now it's observable.
4. **Add an ESP32-S3 co-processor** for the *second* real-time job (relays or DMX). Prove the two-MCU pattern on something non-critical.
5. **Integrate into one flightcase** with the shared power tree, UPS hold-up, panel I/O banks.
6. **Only then** consider collapsing anything. Probably don't. The split is the point.

---

### One-line summary

A mini-PC (or Pi 5) running your Docker world for the "many systems at once" half, wired over USB to one-or-more ESP32/Teensy co-processors for the "real pins, real timing" half — same power tree, same flightcase, same boring-and-reliable rules that made the little bridge trustworthy. The tiny XIAO bridge doesn't get replaced; it becomes one honest module inside a bigger, still-boring rig.
