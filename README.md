# Ontime Display Bridge v2 — "Power Injector" Edition

A self-powered hardware bridge that drives legacy **Interspace Industries** numeric displays from the modern **Ontime** browser-based stage timer — and supplies the display's own 12V from a dedicated DC input.

> **Work in progress — untested and unbuilt.** This is an active, early-stage project. The hardware has **not** yet been built and the firmware has **not** yet been tested on real hardware. Everything here — the design, BOM, wiring and code — should be treated as a working draft that may change. This repo will be updated as the build and testing progress. Use at your own risk; nothing here is proven yet.

> **Attribution.** This is a personal side project — a fork of and successor to [henny19 / Ontime-interspace-Display-Bridge](https://github.com/henny19/Ontime-interspace-Display-Bridge) (© technicaleventservices.com, MIT). All original credit stays with them; this repo exists only because their v1 work made it possible. It's a variant built for one specific rig, published separately rather than as a pull request because the hardware changes would break every existing v1 build — **not** a replacement for, or a claim over, the original.

## Overview

This project breathes new life into legacy RS232-based numeric displays (common in AV and live events) by driving them directly from an **Ontime** instance over WiFi.

Unlike v1, which tapped 12V from the legacy controller and injected data inline, **v2 powers the display itself** from its own 12V DC input, removing the legacy controller from the chain entirely. It converts Ontime's WebSocket timer state into the specific 3-byte RS232 protocol the Interspace displays expect, and shows local status on an on-board OLED.

**Original work © 2025 technicaleventservices.com. v2 changes © 2026 Jay-PBS. MIT.**

## What changed from v1, and why

| Area | v1 | v2 | Reason |
|---|---|---|---|
| **MCU** | Wemos D1 Mini (ESP8266) | **Seeed XIAO ESP32-C3** | ~4× RAM headroom for TLS, hardware crypto, external-antenna connector. ESP8266 heap pressure under WSS is a live-show reliability risk. |
| **Power** | Tapped 12V from the legacy controller | **Own 12V DC input; the box powers the display** | Removes the legacy controller from the chain. No "cap off pin 2" mod. |
| **Status display** | 1602 LCD (5V, 0x27/0x3F) | **0.96" SSD1306 OLED (3.3V native, 0x3C)** | Kills the 5V-backpack-on-3.3V-bus problem and the I2C address lottery. |
| **Antenna** | PCB trace | **External RP-SMA whip via U.FL pigtail** | A diecast enclosure is a Faraday cage — external antenna is mandatory. |
| **WiFi channels** | ESP8266 default ch 1–13 | ESP32-C3 forces **GB / ch 13** in firmware | The C3 can't see ch 12/13 out of the box (IDF 5.x "world-safe" default). |
| **Ontime link** | WSS only | **WS or WSS, portal-selectable** | Shows run both local (plain WS) and cloud/proxy (WSS/443). |
| **Protection** | None | Fuse, ideal-diode reverse protection, TVS, polyfused display feed | It lives in a flightcase. |

**The RS232 display protocol is unchanged from v1** — the display previously received exactly this from its own controller.

## Features

* **Wireless Connectivity:** Connects to Ontime over WiFi, plain **WS or secure WSS**, selectable at config time.
* **Zero-Config Startup:** Captive portal (WiFiManager) for on-site setup of WiFi credentials and Ontime server details — no coding required on the show site.
* **Legacy Protocol Support:** Generates the 9600 baud, swapped-nibble 3-byte protocol required by Interspace displays.
* **Smart Colour Logic:** Maps Ontime state to display colour automatically:
  * Running: **Green**
  * Warning: **Amber**
  * Overtime / Negative: **Red**
* **Local Status Display:** On-board SSD1306 OLED shows connection status, current event title (scrolling for long titles), countdown time and playback state.
* **Self-Powered:** Own 12V DC input feeds both the electronics and the display — no dependence on the legacy controller.
* **Auto-Reconnect:** Robust connection handling with heartbeat monitoring and stateless recovery for reliability during live shows.

## Hardware Requirements

Built on veroboard inside a diecast enclosure.

* **Microcontroller:** Seeed XIAO ESP32-C3 (external antenna via U.FL → RP-SMA)
* **Level Shifter:** MAX3232 module (RS232 ↔ TTL), powered from **3V3**
* **Power:** 12V DC input → fuse → ideal-diode reverse protection → TVS → buck 12V→5V
* **Display (status):** 0.96" SSD1306 OLED, I2C, **address 0x3C**, 3.3V native
* **Enclosure:** Hammond 1590CE diecast (120×100×60mm)
* **Connectors:**
  * 1× 2.1mm DC barrel jack (12V power in)
  * 1× RP-SMA bulkhead (antenna)
  * 1× XLR 3-pin **male** chassis (12V + data out to display)

### Wiring Concept

The device is powered by its own 12V DC input and passes regulated/fused 12V straight to the display on the output XLR.

* **XLR Pin 1 (GND):** Common ground.
* **XLR Pin 2 (Data):** RS232 data from the MAX3232 `T1OUT`.
* **XLR Pin 3 (+12V):** 12V rail via the display polyfuse.

There is **no input XLR** — power comes from the DC jack, not the legacy controller.

## Software Setup

### Prerequisites

* **Arduino IDE 2.x** or **arduino-cli**
* **ESP32 core by Espressif ≥ 3.x** (IDF 5.x based)
  * FQBN: `esp32:esp32:XIAO_ESP32C3`
  * **USB CDC On Boot: Enabled** (mandatory — otherwise serial debug output vanishes)

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc .
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:XIAO_ESP32C3 .
```

### Required Libraries

Install via the Arduino Library Manager:

| Library | Author | Purpose |
| :---- | :---- | :---- |
| **WiFiManager** (≥ 2.0.17) | tzapu | Captive portal for WiFi / server config |
| **WebSockets** | Markus Sattler (Links2004) | WS / WSS client for the Ontime API |
| **ArduinoJson** (v7) | Benoît Blanchon | Parsing API payloads |
| **Adafruit SSD1306** + **Adafruit GFX** | Adafruit | Driving the local OLED |

## Configuration

On first boot (or if the configured WiFi is missing):

1. The device creates a WiFi hotspot named **`OntimeBridge-Setup`**.
2. Connect to it with a phone or laptop.
3. A captive portal should open automatically (or visit **192.168.4.1**).
4. Enter your:
   * **Venue WiFi SSID & Password**
   * **Ontime Host** (e.g. `ontime.my-event.com`)
   * **Ontime Port** (usually 443 for WSS, ~4001 for local WS)
   * **TLS** (checkbox — on for WSS, off for plain WS)
5. Save. The device reboots and connects.

Hold the config button ≥3s at any time to re-open the portal; ≥10s for a full factory reset.

## License

MIT, retained from v1 — see the [LICENSE](LICENSE) file.

Original work by **henny19 / technicaleventservices.com**. v2 hardware and firmware changes by **Jay-PBS**. Credit and blame for the original design stay with the original author.

---

*This README was generated by [Claude Code](https://claude.com/claude-code).*
