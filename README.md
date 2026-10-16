# 🎛️ LED Studio — ESP32 Multi-Zone Matrix & I2S Audio Visualizer

![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)
![Framework](https://img.shields.io/badge/Framework-Arduino-00979D.svg)
![Display](https://img.shields.io/badge/Display-MAX7219%20%7C%20MD__Parola-red.svg)
![Audio](https://img.shields.io/badge/Audio-INMP441%20I2S%20FFT-brightgreen.svg)
![Storage](https://img.shields.io/badge/Storage-SPIFFS%20%7C%20NVS-orange.svg)

**LED Studio** is an asynchronous, multi-zone LED matrix controller and real-time I2S FFT spectrum visualizer built for the **ESP32**. Powered by `MD_Parola`, `ESPAsyncWebServer`, and `arduinoFFT`, it splits chained MAX7219 dot-matrix panels into independent physical zones, interpolates live environmental and NTP time data, reacts to music via a digital MEMS microphone, and serves a gzip-capable web interface with a live WebSocket serial terminal.

---

## ✨ Key Features

### 🖥️ Multi-Zone & Playlist Display Engine

- **Dynamic Zone Allocation:** Supports up to **32 cascaded MAX7219 modules** (256 columns), partitioned into up to **20 independent zones** (`MAX_ZONES`) and **100 scenes** (`MAX_SCENES`).
- **Heap-Optimized Architecture:** Scene and zone structures are dynamically allocated on the heap (`new SceneConfig[]` / `new ZoneConfig[]`) to save ~65 KB of static DRAM and guarantee contiguous memory during large JSON deserialization passes.
- **Custom Typography & Sprites:**
  - **5x7 Thin Font:** Crisp, compact proportional ASCII font with a custom degree symbol (`char 127`).
  - **8x6 Bold Font:** Dynamically transposed and width-trimmed in RAM at boot (`buildCustomBoldFont()`).
  - **Pacman Sprite Engine:** Built-in animated right-facing entry and left-facing exit `PROGMEM` sprites (`PA_SPRITE`).
- **29 Parola Text Effects:** Full support for scrolling, slicing, mesh, fade, dissolve, blinds, wipe, cursor animations, scanning, and growing effects.
- **Hardware Agnostic:** Live switching between `FC16_HW`, `GENERIC_HW`, `PAROLA_HW`, and `ICSTATION_HW` module orientations via JSON configuration.

### 🎵 Real-Time I2S Music Sync & FFT Visualizer

- **32-Bit I2S DMA Audio Capture:** Reads digital audio directly from an **INMP441** MEMS microphone at configurable sample rates (default **16 kHz**, **256-sample** FFT window, 8 DMA buffers).
- **DSP Noise & Frequency Filtering:**
  - Automatic DC-offset removal and RMS volume calculation.
  - **Frequency Ratio Gate:** Computes Hamming-windowed FFT magnitudes and attenuates background noise falling outside the user-defined frequency passband (`start_freq` to `end_freq`).
  - **Logarithmic Bin Mapping & Treble EQ:** Allocates higher column density to bass/midrange frequencies (`powf(colRatio, 1.35f)`) while applying progressive high-frequency gain compensation (`eqBoost`).
- **9 Built-In Audio Visualizations:**
  1. `VU Bar` — Horizontal solid volume meter.
  2. `VU Peak` — Volume bar with configurable `Fast` or `Smooth` falling peak-hold indicator.
  3. `VU Mirror` — Center-outward symmetrical volume expansion.
  4. `VU Bounce` — Beat-driven 2-column block with spring physics and elastic wall rebounds.
  5. `VU Pulse` — Center diamond/waveform expansion across both X and Y axes.
  6. `VU Wave` — SoundCloud-style scrolling time-domain waveform history buffer.
  7. `VU Spectrum` — Multi-band logarithmic FFT spectrum analyzer with both bars and peak dots.
  8. `VU Spectrum Bar` — Clean FFT spectrum bars without peak dots.
  9. `VU Spectrum Peak` — Floating FFT peak dots only.

### 🌡️ Environmental Sensors & Smart Peripherals

- **Live Template Interpolation:** Embed dynamic tags for **AHT10** temperature/humidity and **SNTP** real-time clock data directly inside scene messages.
- **Glitch-Filtered Auto-Brightness:** Non-blocking **BH1750** ambient light polling (every 2 seconds) mapped across a 16-step logarithmic lux curve (`0–15`), with per-scene offset biasing and I2C glitch rejection during heavy I2S interrupts.
- **Non-Blocking Buzzer State Machine:** Drives an active buzzer on GPIO 26 for configuration save double-chirps, system action alerts, and an automated **Hourly Chime** (`{HBEEP}`) that triggers for 2 seconds at `XX:00:00`.
- **Hardware Timer Status LED:** Uses ESP32 `Ticker` ISR interrupts on GPIO 2 to signal portal status, scene transitions, and configuration loads without blocking the animation loop.

### 🌐 Networking, Web UI & System Resilience

- **WiFiManager Captive Portal:** Broadcasts a `"LED STUDIO"` access point (180-second timeout) if saved credentials fail, falling back gracefully to Offline Mode.
- **Dynamic mDNS Discovery:** Automatically registers `http://ledstudio-XXXX.local` (using the last two bytes of the ESP32 MAC address) and scrolls both the IP and mDNS URL on startup.
- **Gzip Web Server & ETag Caching:** Serves `/index.html.gz` (or `/index.html`) from SPIFFS with build-timestamp `ETag` headers (`304 Not Modified`) and streamed `POST` configuration uploads.
- **WebSocket Live Terminal:** Streams `WebLog()` and `WebLogf()` output directly to `/ws/terminal` (capped at 2 concurrent clients, with a **25 KB free-heap safety cutoff** to prevent TCP queue memory exhaustion).
- **Online / Offline Mode Toggle:** Hold the onboard **BOOT button (GPIO 0) for 10 seconds** at any time to toggle between Online Mode and Offline Mode (persisted in NVS `Preferences`).

---

## 🔌 Hardware Requirements & Pinout

### Bill of Materials (BOM)

- **Microcontroller:** ESP32 Development Board (ESP-WROOM-32, 30-pin or 38-pin)
- **Display:** 1 to 32 cascaded **MAX7219 8x8 LED Matrix** modules (FC-16 or equivalent)
- **Microphone:** **INMP441** Omnidirectional I2S MEMS Microphone module
- **Temp/Humidity Sensor:** **AHT10** I2C environmental sensor
- **Ambient Light Sensor:** **BH1750** I2C digital lux sensor
- **Audio Alert:** 3.3V / 5V **Active Buzzer**
- **Power Supply:** Dedicated 5V DC power supply rated for your matrix size (~150–300 mA per 8x8 module at full brightness)

### Pin Assignment Table

| Component          | Module Pin    | ESP32 GPIO         | Protocol / Notes                       |
| :----------------- | :------------ | :----------------- | :------------------------------------- |
| **MAX7219 Matrix** | `CLK`         | **GPIO 18**        | Hardware VSPI SCK                      |
|                    | `DIN` (DATA)  | **GPIO 23**        | Hardware VSPI MOSI                     |
|                    | `CS` (LOAD)   | **GPIO 5**         | Chip Select                            |
|                    | `VCC` / `GND` | **5V** / **GND**   | Use external 5V rail for >4 modules    |
| **INMP441 Mic**    | `SCK` (BCLK)  | **GPIO 14**        | I2S Serial Clock (`I2S_NUM_0`)         |
|                    | `WS` (LRCLK)  | **GPIO 15**        | I2S Word Select                        |
|                    | `SD` (DOUT)   | **GPIO 32**        | I2S Serial Data In                     |
|                    | `L/R`         | **GND**            | **Must be tied to GND** (Left channel) |
|                    | `VDD` / `GND` | **3.3V** / **GND** | **3.3V only** (Do not connect to 5V)   |
| **AHT10 Sensor**   | `SDA`         | **GPIO 21**        | Shared I2C Data (`Wire`)               |
|                    | `SCL`         | **GPIO 22**        | Shared I2C Clock (150ms bus timeout)   |
| **BH1750 Sensor**  | `SDA`         | **GPIO 21**        | Shared I2C Data (`Wire`)               |
|                    | `SCL`         | **GPIO 22**        | Shared I2C Clock                       |
| **Active Buzzer**  | `+` (Signal)  | **GPIO 26**        | Active HIGH digital output             |
| **Status LED**     | Anode         | **GPIO 2**         | Built-in onboard blue LED              |
| **Mode Button**    | Button        | **GPIO 0**         | Onboard `BOOT` button (`INPUT_PULLUP`) |

---

## 📚 Required Libraries

Install the following libraries via the Arduino Library Manager or `platformio.ini`:

| Library             | Author / Source            | Purpose                                                              |
| :------------------ | :------------------------- | :------------------------------------------------------------------- |
| `MD_Parola`         | MajicDesigns               | Multi-zone matrix text animations & sprites                          |
| `MD_MAX72XX`        | MajicDesigns               | Low-level MAX7219 SPI hardware driver                                |
| `ArduinoJson`       | Benoit Blanchon            | Config serialization/deserialization (v6 or v7 supported)            |
| `ESPAsyncWebServer` | Me-No-Dev / mathieucarbou  | Non-blocking HTTP & WebSocket server                                 |
| `AsyncTCP`          | Me-No-Dev / mathieucarbou  | Asynchronous TCP dependency for ESP32                                |
| `WiFiManager`       | tzapu                      | Captive portal for WiFi provisioning                                 |
| `Adafruit AHTX0`    | Adafruit                   | AHT10 temperature & humidity driver (plus `Adafruit Unified Sensor`) |
| `BH1750`            | Christopher Laws (`claws`) | Continuous high-res ambient light sensing                            |
| `arduinoFFT`        | Enrique Condes (`kosme`)   | v2.x C++ template FFT processor (`ArduinoFFT<double>`)               |

> **Built-in ESP32 Core Libraries Used:** `WiFi.h`, `ESPmDNS.h`, `SPIFFS.h`, `Preferences.h`, `Ticker.h`, `Wire.h`, `SPI.h`, `driver/i2s.h`, `esp_sntp.h`, `time.h`.

---

## 🏷️ Dynamic Template Tag Reference

When a scene's message type is set to `"custom"`, the firmware parses and replaces the following tags in real time (polled at **1 Hz** for time and every **30 seconds** for the AHT10 sensor):

### 🕒 Time & Date Tags (IST `UTC+5:30` by default)

| Tag      | `strftime` Specifier | Output Description          | Example    |
| :------- | :------------------: | :-------------------------- | :--------- |
| `{HH}`   |         `%H`         | 24-hour clock (zero-padded) | `14`       |
| `{hh}`   |         `%I`         | 12-hour clock (zero-padded) | `02`       |
| `{mm}`   |         `%M`         | Minutes (zero-padded)       | `35`       |
| `{ss}`   |         `%S`         | Seconds (zero-padded)       | `09`       |
| `{AMPM}` |         `%p`         | AM / PM indicator           | `PM`       |
| `{DD}`   |         `%d`         | Day of month (zero-padded)  | `07`       |
| `{dd}`   |         `%e`         | Day of month (space-padded) | ` 7`       |
| `{MMMM}` |         `%B`         | Full month name             | `October`  |
| `{MMM}`  |         `%b`         | Abbreviated month name      | `Oct`      |
| `{MM}`   |         `%m`         | Month number (01–12)        | `10`       |
| `{YYYY}` |         `%Y`         | 4-digit year                | `2026`     |
| `{YY}`   |         `%y`         | 2-digit year                | `26`       |
| `{WWWW}` |         `%A`         | Full weekday name           | `Thursday` |
| `{WWW}`  |         `%a`         | Abbreviated weekday name    | `Thu`      |

### 🌡️ Environmental & System Control Tags

| Tag                | Output Description                                                                                                    | Example    |
| :----------------- | :-------------------------------------------------------------------------------------------------------------------- | :--------- |
| `{TEM}` / `{TEMP}` | Temperature rounded to nearest integer                                                                                | `28`       |
| `{TEM1}`           | Temperature with 1 decimal place                                                                                      | `28.4`     |
| `{TEM2}`           | Temperature with 2 decimal places                                                                                     | `28.42`    |
| `{HUM}`            | Relative Humidity rounded to nearest integer                                                                          | `65`       |
| `{HUM1}`           | Relative Humidity with 1 decimal place                                                                                | `65.2`     |
| `{HUM2}`           | Relative Humidity with 2 decimal places                                                                               | `65.25`    |
| `{DEG}` or `°`     | Custom 4-column degree symbol (`\x7F`)                                                                                | `°`        |
| `{HBEEP}`          | **Silent Trigger:** Enables the 2-second hourly chime at `XX:00:00`. Automatically stripped from visible matrix text. | _(Hidden)_ |

**Example Template String:**

```text
{hh}:{mm}:{ss} {AMPM} | {TEM1}{DEG}C {HUM}% {HBEEP}
```

---

## 🎬 Supported Parola Text Animations

Use any of the following strings in `animation.inEffect` and `animation.outEffect`:

- **Basic & Static:** `PA_PRINT`, `PA_NO_EFFECT`
- **Scrolling:** `PA_SCROLL_LEFT`, `PA_SCROLL_RIGHT`, `PA_SCROLL_UP`, `PA_SCROLL_DOWN`
- **Diagonal Scrolling:** `PA_SCROLL_UP_LEFT`, `PA_SCROLL_UP_RIGHT`, `PA_SCROLL_DOWN_LEFT`, `PA_SCROLL_DOWN_RIGHT`
- **Transitions:** `PA_SLICE`, `PA_MESH`, `PA_FADE`, `PA_DISSOLVE`, `PA_BLINDS`, `PA_RANDOM`
- **Wipes & Curtains:** `PA_WIPE`, `PA_WIPE_CURSOR`, `PA_OPENING`, `PA_OPENING_CURSOR`, `PA_CLOSING`, `PA_CLOSING_CURSOR`
- **Scanners & Growth:** `PA_SCAN_HORIZ`, `PA_SCAN_HORIZX`, `PA_SCAN_VERT`, `PA_SCAN_VERTX`, `PA_GROW_UP`, `PA_GROW_DOWN`
- **Custom Sprite:** `PA_SPRITE` _(Renders the built-in animated Pacman sprite eating/revealing the text)_

> **Note:** When `PA_GROW_UP` or `PA_GROW_DOWN` is selected as an `outEffect`, the firmware automatically inverts the direction so the text exits naturally. Static scenes (`inEffect: "PA_PRINT"` with `outEffect: "PA_NO_EFFECT"`) automatically zero out scroll speed and default to a `1000ms` hold if `repeat` is finite and `endDelayMs` is `0`.

---

## 🔊 Hardware Controls, Buzzer & LED Feedback

### 🔘 BOOT Button (GPIO 0) — Online / Offline Mode Switch

Hold the **BOOT** button continuously for **10 seconds** during normal operation to toggle network modes:

- **Switching Online ➔ Offline:** Buzzer sounds continuously for **3 seconds** while the Status LED stays **OFF**, then the ESP32 reboots with WiFi disabled and scrolls `"OFFLINE MODE"`.
- **Switching Offline ➔ Online:** Both the Buzzer and Status LED turn **ON** together for **3 seconds**, then the ESP32 reboots and connects to WiFi.

### 🔔 Buzzer Audio Codes (GPIO 26)

| Event                                           | Buzzer Pattern                                                             |
| :---------------------------------------------- | :------------------------------------------------------------------------- |
| **Live Config Saved (`/api/matrix/config`)**    | 2 quick beeps (`100ms` ON / `100ms` OFF)                                   |
| **Hardware Module / Type Changed**              | 1 medium beep (`500ms` ON) followed by automatic reboot                    |
| **Web System Restart (`/api/system/restart`)**  | 1 long beep (`1000ms` ON) followed by reboot                               |
| **Forget WiFi (`/api/system/forget-wifi`)**     | 2 fast beeps (`150ms` ON / `150ms` OFF) followed by AP Portal reboot       |
| **Factory Reset (`/api/system/factory-reset`)** | 3 rapid warning beeps (`100ms` ON / `100ms` OFF) followed by wipe & reboot |
| **Hourly Chime (`{HBEEP}` active)**             | 1 extended chime (`2000ms` ON) at `XX:00:00`                               |

### 💡 Status LED Patterns (GPIO 2)

- **Continuous Fast Blink (`0.2s` interval):** Device is in WiFiManager Captive Portal (`"LED STUDIO"` AP) waiting for WiFi configuration.
- **2 Quick Flashes (`0.1s` interval):** A zone is transitioning to a new scene.
- **3 Quick Flashes (`0.1s` interval):** New configuration JSON received and loaded into RAM.

---

## 🌐 REST API & WebSocket Reference

### HTTP Endpoints

| Method | Endpoint                    | Description                                                                                                                                                   |
| :----: | :-------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `GET`  | `/` or `/index.html`        | Serves the web UI from SPIFFS (automatically prefers `/index.html.gz` if supported by browser; uses `ETag` caching).                                          |
| `GET`  | `/icon.svg`                 | Serves static application icon from SPIFFS (`max-age=604800`).                                                                                                |
| `GET`  | `/api/matrix/config`        | Returns the raw `/config.json` file stored in SPIFFS.                                                                                                         |
| `POST` | `/api/matrix/config`        | Streams a new JSON configuration payload directly to `/config.json` in SPIFFS and triggers a live hot-reload.                                                 |
| `GET`  | `/api/system/status`        | Returns real-time ESP32 telemetry JSON (CPU MHz, internal die temp, RAM usage, SPIFFS usage, WiFi SSID & RSSI). Returns `503` if free heap drops below 15 KB. |
| `POST` | `/api/system/restart`       | Triggers a graceful 1-second beep and reboots the ESP32.                                                                                                      |
| `POST` | `/api/system/forget-wifi`   | Clears stored WiFi credentials in NVS/WiFiManager and reboots into AP Portal mode.                                                                            |
| `POST` | `/api/system/factory-reset` | Deletes `/config.json` (preserving `/index.html.gz`), wipes WiFi credentials, and reboots to factory defaults.                                                |

### Telemetry Response Example (`GET /api/system/status`)

```json
{
  "cpu_mhz": 240,
  "cpu_temp": 48.5,
  "ram_total": 327680,
  "ram_used": 142310,
  "spiffs_total": 1376256,
  "spiffs_used": 184320,
  "wifi_ssid": "MyHomeNetwork",
  "wifi_rssi": -58
}
```

### WebSocket Terminal (`ws://<device-ip>/ws/terminal`)

- Broadcasts all formatted system logs (`WebLog` / `WebLogf`) in real time to the browser terminal.
- Automatically prunes ghost sockets (`cleanupClients(2)`) every 2 seconds and on new connections.
- **Heap Guard:** Automatically suspends WebSocket broadcasts if `ESP.getFreeHeap()` drops below **25,000 bytes** to guarantee uninterrupted `MD_Parola` frame rendering.

---

## 📄 Configuration Schema (`/config.json`)

If `/config.json` is missing on boot (or after a factory reset), the firmware automatically generates the following structure in SPIFFS:

```json
{
  "device": "ESP_LED_MATRIX_MD_PAROLA",
  "matrix": {
    "height": 8,
    "width": 32,
    "modules": 4,
    "hardware_type": "FC16_HW",
    "absolute_max_devices": 32
  },
  "music_sync": {
    "enabled": false,
    "zone": "Music Zone",
    "start_col": 0,
    "end_col": 31,
    "animation": "VU Spectrum Bar",
    "sensitivity": 20,
    "peak_decay": "Smooth",
    "brightness": 7,
    "auto": true,
    "fft_samples": 256,
    "sampling_freq": 16000,
    "start_freq": 500,
    "end_freq": 7000
  },
  "zones": [
    {
      "id": "zone_txt_1",
      "name": "Zone 1",
      "start": 0,
      "end": 31,
      "isMusic": false
    },
    {
      "id": "zone_mus_1",
      "name": "Music Zone",
      "start": 0,
      "end": 31,
      "isMusic": true
    }
  ],
  "scenes": [
    {
      "sceneName": "Welcome",
      "zone": {
        "name": "Zone 1",
        "startCol": 0,
        "endCol": 31
      },
      "message": {
        "type": "plain",
        "content": "Led Studio",
        "bold": true,
        "align": "center"
      },
      "animation": {
        "inEffect": "PA_SCROLL_LEFT",
        "outEffect": "PA_SCROLL_LEFT",
        "speedMs": 35,
        "startDelayMs": 0,
        "endDelayMs": 0
      },
      "display": {
        "brightness": 7,
        "auto": true,
        "repeat": -1
      }
    }
  ],
  "playlists": [
    {
      "name": "Default Playlist",
      "zone": "Zone 1",
      "repeat": -1,
      "enabled": true,
      "scenes": ["Welcome"]
    }
  ]
}
```

---

## 🚀 Installation & Deployment

1. **Prepare the Project Directory:**
   Place the firmware sketch inside your project folder alongside a `data/` directory containing your web assets:
   ```text
   LED_Studio/
   ├── LED_Studio.ino
   └── data/
       ├── index.html.gz   (or index.html)
       └── icon.svg
   ```
2. **Upload SPIFFS Image:**
   - **Arduino IDE:** Use the _ESP32 Sketch Data Upload_ plugin to flash the `data/` folder to SPIFFS.
   - **PlatformIO:** Run `pio run --target uploadfs`.
3. **Compile & Flash Firmware:**
   Select your **ESP32 Dev Module** board (Partition Scheme: _Huge APP (3MB No OTA/1MB SPIFFS)_), set upload speed to `921600`, and flash the sketch.
4. **Connect to WiFi:**
   - On first boot, connect your phone or PC to the **`LED STUDIO`** WiFi access point.
   - Enter your 2.4 GHz WiFi credentials in the captive portal.
   - Upon reboot, the matrix will scroll its assigned local IP address and mDNS hostname (`http://ledstudio-XXXX.local`).

## 📜 License & Acknowledgments

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for full details.

**Copyright © 2026 Mrinal Maity**

### Third-Party Libraries & Licenses

LED Studio relies on the following open-source libraries, each governed by its respective license:

- **[MD_Parola](https://github.com/MajicDesigns/MD_Parola) & [MD_MAX72XX](https://github.com/MajicDesigns/MD_MAX72XX)** by MajicDesigns — _LGPL-2.1 / MIT License_
- **[ArduinoJson](https://github.com/bblanchon/ArduinoJson)** by Benoit Blanchon — _MIT License_
- **[ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) & [AsyncTCP](https://github.com/mathieucarbou/AsyncTCP)** — _LGPL-3.0 License_
- **[WiFiManager](https://github.com/tzapu/WiFiManager)** by tzapu — _MIT License_
- **[arduinoFFT](https://github.com/kosme/arduinoFFT)** by Enrique Condes — _GPL-3.0 License_
- **[Adafruit AHTX0](https://github.com/adafruit/Adafruit_AHTX0)** by Adafruit — _BSD License_
- **[BH1750](https://github.com/claws/BH1750)** by Christopher Laws — _MIT License_
