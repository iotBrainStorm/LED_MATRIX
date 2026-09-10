ESP32 LED Matrix Studio + Music Sync
A web-controlled ESP32 LED matrix project built around MAX7219 / MD_MAX72XX / MD_Parola, with support for animated text scenes, multiple display zones, custom fonts, real-time audio visualization using an INMP441 I2S MEMS microphone, FFT spectrum analysis, AHT10 temperature/humidity data, SNTP time synchronization, Wi-Fi configuration, mDNS, and SPIFFS-based configuration.
The current firmware is designed for an 8-row MAX7219 matrix, with a default web configuration of 8 × 40 pixels (5 modules) and firmware support for up to 30 MAX7219 modules (240 columns).

---

Features
LED Matrix
MAX7219-based LED matrix
`MD_MAX72XX::FC16_HW` hardware type
Up to 30 MAX7219 modules
8-pixel matrix height
Up to 240 display columns
SPI communication
Adjustable display brightness
Custom 8×6 bold bitmap font
Multiple independent MD_Parola zones
Text & Animation
MD_Parola-based text rendering
Multiple scenes/zones
Per-zone:
Message
Alignment
Bold/custom font
Brightness
Repeat count
Animation speed
Entrance effect
Exit effect
Supports custom messages containing time/date and sensor placeholders
Music Sync
Real-time audio visualization using an INMP441 microphone and ArduinoFFT.
Available visualizations include:
VU Bar
VU Peak
VU Mirror
VU Bounce
VU Pulse
VU Wave
VU Spectrum
Music Sync operates as a dedicated rendering mode. When enabled, the normal MD_Parola scene renderer is bypassed so that audio visualization can directly control the MAX7219 display.
Audio Processing
INMP441 I2S microphone
16 kHz sampling
64-point FFT
Hamming window for spectrum analysis
Magnitude calculation
Volume smoothing
Peak decay
Configurable sensitivity
Real-time direct matrix rendering
Sensors
Optional AHT10 temperature/humidity sensor.
If the AHT10 is unavailable, the current firmware uses fallback virtual readings.
Time & Date
Background SNTP synchronization
`pool.ntp.org`
`time.google.com`
Configured for UTC+5:30 / IST
Non-blocking time synchronization callback
Wi-Fi
Connects to saved Wi-Fi credentials
WiFiManager configuration portal when saved Wi-Fi is unavailable
Automatic background reconnection
Reconnection check every 3 seconds
Reconnect trigger after approximately 15 seconds of disconnection
Web Interface
AsyncWebServer
Web UI served from SPIFFS
`index.html`
`icon.svg`
JSON configuration stored as `/config.json`
HTTP ETag support for the main page
Browser caching for static icon resources
Configuration endpoint uses no-cache headers
Configuration changes can be pushed from the browser without restarting the ESP32
mDNS
The ESP32 advertises itself as:

```text
http://ledstudio.local
```

---

Hardware
ESP32
The firmware uses an ESP32 development board with the following connections.
MAX7219 Matrix
MAX7219 ESP32
CLK GPIO 18
DIN GPIO 23
CS GPIO 5
VCC 5V
GND GND
Firmware configuration:

```cpp
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 30

#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5
```

Each MAX7219 module provides:

```text
8 rows × 8 columns
```

Therefore:

```text
5 modules  = 8 × 40
30 modules = 8 × 240
```

---

INMP441 Microphone
INMP441 ESP32
SCK / BCLK GPIO 14
WS / LRCL GPIO 15
SD / DOUT GPIO 32
VDD 3.3V
GND GND
Firmware configuration:

```cpp
#define I2S_SCK 14
#define I2S_WS 15
#define I2S_SD 32
#define I2S_PORT I2S_NUM_0
```

The current I2S configuration uses:

```text
Sample rate       : 16,000 Hz
Bits per sample   : 32-bit
Channel           : LEFT
FFT samples       : 64
```

## Make sure the INMP441 L/R channel selection matches the firmware's `I2S_CHANNEL_FMT_ONLY_LEFT` setting.

AHT10
The AHT10 is connected through I2C:
AHT10 ESP32
SDA GPIO 21
SCL GPIO 22
VCC 3.3V
GND GND
Firmware:

```cpp
Wire.begin(21, 22);
```

## The sensor is optional.

Software Requirements
The project is written for the Arduino ESP32 environment.
Required libraries used by the firmware include:
Arduino ESP32 core
MD_MAX72XX
MD_Parola
ESPAsyncWebServer
WiFiManager
ArduinoJson
ArduinoFFT
Adafruit AHT10
Adafruit Unified Sensor dependencies as required by the AHT10 library
The firmware also uses ESP32/system libraries:
WiFi
SPI
SPIFFS
Preferences
Wire
ESPmDNS
HTTPClient
HardwareSerial
`esp_sntp.h`
`driver/i2s.h`
`time.h`
EEPROM

---

Project Structure
A typical project structure is:

```text
ESP32_LED_MATRIX/
│
├── ESP32_LED_MATRIX.ino
│
├── data/
│   ├── index.html
│   └── icon.svg
│
└── README.md
```

The web interface is stored in SPIFFS.
The firmware expects:

```text
/index.html
/icon.svg
/config.json
```

## `config.json` can be created automatically with the firmware's default configuration.

Default Matrix Configuration
The firmware creates a default matrix configuration of:

```text
Height  : 8 pixels
Width   : 40 pixels
Modules : 5
```

Default Music Sync configuration:

```text
Enabled     : false
Zone        : Zone 1
Start       : Column 0
End         : Column 39
Animation   : VU Bar
Sensitivity : 60
Peak Decay  : Medium
```

---

Display Zones
The firmware supports:

```cpp
#define MAX_ZONES 4
```

This means up to four MD_Parola zones can be active simultaneously.
A scene can define a zone such as:

```json
{
  "zone": {
    "name": "Zone 1",
    "startCol": 0,
    "endCol": 23
  }
}
```

Important
Normal MD_Parola zones are ultimately converted from column positions to MAX7219 device/module positions:

```text
startCol / 8
endCol   / 8
```

Therefore normal Parola zones are effectively aligned to 8-column module boundaries.
Music Sync rendering, however, works directly with column positions and can use arbitrary column ranges.

---

Text Templates
Custom messages can contain dynamic placeholders.
Time
Placeholder Meaning
`{HH}` 24-hour hour
`{hh}` 12-hour hour
`{mm}` Minutes
`{ss}` Seconds
`{AMPM}` AM/PM
Date
Placeholder Meaning
`{DD}` Day, zero padded
`{dd}` Day, space padded
`{MM}` Month number
`{MMM}` Short month name
`{MMMM}` Full month name
`{YY}` Two-digit year
`{YYYY}` Four-digit year
`{WWW}` Short weekday
`{WWWW}` Full weekday
Sensors
Placeholder Meaning
`{TEMP}` AHT10 temperature
`{HUM}` AHT10 relative humidity
Example:

```text
{HH}:{mm}:{ss}
```

or:

```text
{TEMP}C  {HUM}%
```

---

Music Sync Architecture
The Music Sync path is separate from normal MD_Parola animation.
The processing flow is approximately:

```text
INMP441
   │
   ▼
I2S
   │
   ▼
Audio samples
   │
   ├──────────────► RMS / volume
   │                    │
   │                    ▼
   │              smoothVol
   │                    │
   │                    ▼
   │             VU animations
   │
   └──────────────► ArduinoFFT
                        │
                        ▼
                  Frequency magnitude
                        │
                        ▼
                   VU Spectrum
                        │
                        ▼
                 MAX7219 columns
```

## This allows volume-based effects and FFT-based spectrum effects to share the same audio input.

Music Sync Animations
VU Bar
A horizontal volume bar grows according to the smoothed audio level.

```text
██████████████░░░░░░
```

## The current implementation also varies the vertical height of the filled columns.

VU Peak
A volume bar is combined with a peak indicator.
The peak remains above the current level and falls according to the selected decay speed.

---

VU Mirror
The animation expands from the center toward both sides.

```text
░░░░████████████░░░░
```

---

VU Bounce
A moving/bouncing indicator follows the audio level using simple position and velocity calculations.

---

VU Pulse
The animation expands outward from the center according to audio volume.

---

VU Wave
A history buffer stores previous volume values to create a moving waveform/ripple effect.

```text
      █
     ███
   █████
 ███████
   █████
     ███
      █
```

---

VU Spectrum
The firmware uses ArduinoFFT to calculate frequency magnitudes.
Current FFT parameters:

```text
FFT size       : 64
Sampling rate  : 16 kHz
Frequency bins : 32 usable positive-frequency bins
Resolution     : 250 Hz/bin
Nyquist        : 8 kHz
```

## The current implementation maps display columns to FFT bins to create a spectrum visualization.

Configuration Storage
Configuration is stored in SPIFFS:

```text
/config.json
```

The web interface can request the configuration through:

```text
GET /api/matrix/config
```

and save configuration through:

```text
POST /api/matrix/config
```

The GET endpoint intentionally disables browser caching:

```text
Cache-Control: no-cache, no-store, must-revalidate
```

## This prevents an old configuration from being reused when the user requests the current ESP32 configuration.

Web Caching
Static resources can use browser caching.
For example:

```cpp
server.serveStatic("/icon.svg", SPIFFS, "/icon.svg")
    .setCacheControl("max-age=604800");
```

The main page also uses an ETag based on the build date/time.
This allows a browser to avoid downloading unchanged static resources repeatedly while still keeping dynamic configuration requests fresh.

---

Wi-Fi Behaviour
At startup the firmware:
Starts Wi-Fi in station mode.
Attempts to connect to the saved Wi-Fi network.
If no saved connection is available, starts the WiFiManager portal.
Continues in offline mode if the portal times out.
Starts the web server independently.
Monitors Wi-Fi in the background.
The Wi-Fi supervisor checks the connection periodically and calls:

```cpp
WiFi.reconnect();
```

when required.
When Wi-Fi reconnects, mDNS is initialized again.
The AsyncWebServer itself is started once during setup; it does not need to be recreated after every Wi-Fi reconnection.

---

mDNS
The configured hostname is:

```cpp
const char *mdnsHostname = "ledstudio";
```

When mDNS starts successfully:

```text
http://ledstudio.local
```

## can be used instead of the ESP32 IP address on networks where `.local` mDNS resolution is supported.

SNTP / Time Synchronization
The firmware uses ESP32 SNTP support through:

```cpp
#include "esp_sntp.h"
```

and:

```cpp
configTime(
    gmtOffset_sec,
    daylightOffset_sec,
    ntpServer1,
    ntpServer2
);
```

Current configuration:

```text
NTP server 1 : pool.ntp.org
NTP server 2 : time.google.com
UTC offset   : +05:30
DST offset   : 0
```

Synchronization is handled in the background.
A callback reports successful synchronization to Serial.

---

Runtime Mode Switching
The firmware has two main display modes.
Normal Scene Mode

```text
Configuration
      │
      ▼
MD_Parola
      │
      ▼
Multiple text zones
      │
      ▼
Animated MAX7219 display
```

Music Sync Mode

```text
INMP441
   │
   ▼
I2S + FFT / Volume
   │
   ▼
Music Sync renderer
   │
   ▼
Direct MAX7219 drawing
```

When:

```cpp
musicSync.enabled == true
```

the firmware activates:

```cpp
isMusicSyncActive = true;
```

and the normal Parola scene renderer is not used.
When Music Sync is disabled, normal scenes are loaded and animated again.
This separation prevents MD_Parola and direct Music Sync drawing from simultaneously trying to control the same display buffer.

---

Startup Sequence
The firmware startup sequence is:

```text
Serial
  │
  ▼
Build custom font
  │
  ▼
Mount SPIFFS
  │
  ▼
Initialize AHT10
  │
  ▼
Initialize MD_Parola / MAX7219
  │
  ▼
Initialize INMP441 I2S
  │
  ▼
Connect Wi-Fi
  │
  ▼
Initialize mDNS
  │
  ▼
Start SNTP
  │
  ▼
Start AsyncWebServer
  │
  ▼
Load configuration
```

---

Main Loop
The main loop performs three major tasks:

```text
1. Apply configuration changes
2. Render either Music Sync or MD_Parola
3. Run the Wi-Fi supervisor
```

Conceptually:

```cpp
if (configUpdated) {
    loadConfiguration();
}

if (isMusicSyncActive) {
    runMusicSyncFrame();
}
else {
    P.displayAnimate();
}

checkWiFiAndStartServer();
```

## The design keeps the network supervision in the main loop without requiring a separate blocking network task.

Serial Monitor
The firmware uses:

```text
115200 baud
```

Example startup output:

```text
==============================
ESP32 LED Matrix + Music Sync
==============================
```

Useful diagnostic messages include:

```text
[FS]
[Sensor]
[I2S]
[WiFi]
[mDNS]
[NTP]
[HTTP]
[Config]
```

## These prefixes make it easier to identify which subsystem generated a message.

Building & Uploading

1. Prepare the Arduino project
   Open the ESP32 firmware in Arduino IDE or another supported Arduino development environment.
   Select the appropriate ESP32 board, such as:

```text
ESP32 Dev Module
```

2. Install required libraries
   Install the libraries listed in the Software Requirements section.
3. Prepare SPIFFS files
   Place the web interface in:

```text
data/index.html
```

and the icon in:

```text
data/icon.svg
```

4. Upload the filesystem
   Upload the contents of the `data` directory to the ESP32 SPIFFS filesystem using the filesystem upload method appropriate for your Arduino/ESP32 development setup.
5. Upload the firmware
   Compile and upload the ESP32 firmware.
6. Open Serial Monitor
   Use:

```text
115200 baud
```

7. Connect Wi-Fi
   If saved Wi-Fi credentials are unavailable, the firmware starts the WiFiManager configuration portal:

```text
LED STUDIO
```

## After connection, check the IP address printed in Serial Monitor.

Configuration Example
A simplified configuration looks like:

```json
{
  "device": "ESP_LED_MATRIX_MD_PAROLA",

  "matrix": {
    "height": 8,
    "width": 40,
    "modules": 5
  },

  "music_sync": {
    "enabled": false,
    "zone": "Zone 1",
    "start_col": 0,
    "end_col": 39,
    "animation": "VU Bar",
    "sensitivity": 60,
    "peak_decay": "Medium"
  },

  "scenes": [
    {
      "sceneName": "ESP",

      "zone": {
        "name": "Zone 1",
        "startCol": 0,
        "endCol": 23
      },

      "message": {
        "type": "plain",
        "content": "ESP",
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
        "brightness": 12,
        "repeat": -1
      }
    }
  ]
}
```

## The web interface normally generates and manages this configuration, so manual editing is not required for normal operation.

Important Design Notes
MAX7219 Module Count
The firmware is configured for:

```cpp
#define MAX_DEVICES 30
```

which corresponds to:

```text
30 × 8 = 240 columns
```

Actual hardware limits also depend on power supply capacity, wiring, signal integrity, PCB/module quality, and the physical layout of a long MAX7219 chain.
For very long chains, signal buffering/level shifting and careful power distribution may be required.

---

Column-Based vs Module-Based Zones
There are two different concepts in the firmware:
Normal MD_Parola scenes
These are ultimately assigned using MAX7219 device indices, so zone boundaries are effectively module-aligned.
Music Sync
Music Sync draws directly by matrix column:

```cpp
startCol
endCol
```

Therefore Music Sync can work with arbitrary column boundaries.
This distinction is important when designing zones such as:

```text
0–10
11–35
36–50
53–90
```

---

Current Limitations / Future Improvements
The current firmware is functional, but several areas can be improved as the project grows.

1. I2S partial reads
   The audio processing should ideally verify that a complete FFT frame was received before processing it.
   For example:

```cpp
if (sampleCount != FFT_SAMPLES) {
    return;
}
```

This prevents stale values from remaining in unused FFT positions. 2. FFT Spectrum Bands
The current spectrum implementation maps display columns to FFT bins.
A more advanced implementation could use a fixed number of frequency bands, for example:

```text
FFT
 ↓
20 frequency bands
 ↓
Band levels
 ↓
Interpolate to display width
 ↓
MAX7219
```

This would create a more conventional multi-band spectrum analyzer. 3. Configuration Power-Loss Safety
The current configuration upload writes directly to:

```text
/config.json
```

A safer approach is:

```text
/config.tmp
      │
      ▼
complete write
      │
      ▼
close successfully
      │
      ▼
replace /config.json
```

This prevents a power loss during upload from leaving the primary configuration file incomplete. 4. Music Sync State Reset
When changing Music Sync zones or widths, temporary animation state such as waveform history and peak positions should be reset to avoid old data appearing in the new zone.

---

Troubleshooting
Display is blank
Check:
MAX7219 VCC
Common GND between ESP32 and display
DIN/CLK/CS wiring
`FC16_HW` hardware type
Number of modules
MAX7219 module orientation

---

Display shows corrupted characters
Check:
Module type
Daisy-chain direction
DIN/CLK/CS signal integrity
Power supply
Number of configured modules

---

INMP441 produces no audio response
Check:
3.3V power
GND
BCLK → GPIO 14
WS → GPIO 15
SD → GPIO 32
INMP441 L/R channel selection
I2S wiring length and grounding

---

Music Sync is too sensitive
Reduce:

```text
Sensitivity
```

## in the Music Sync configuration.

Music Sync is too weak
Increase the sensitivity setting and verify the microphone's physical orientation and gain behaviour.

---

AHT10 is not detected
The firmware reports:

```text
[Sensor] AHT10 not found.
```

Check:
SDA → GPIO 21
SCL → GPIO 22
3.3V
GND
I2C wiring
The display can still operate without the AHT10 because the current firmware uses fallback values.

---

`ledstudio.local` does not open
Try the ESP32 IP address printed in Serial Monitor.
mDNS availability depends on the client device and local network.

---

Web page does not update after changing it
The main page uses an ETag based on the firmware build date/time.
If necessary, perform a normal browser hard refresh and ensure the updated `index.html` has actually been uploaded to SPIFFS.

---

Performance Considerations
The project combines several relatively demanding operations:

```text
MAX7219 display updates
        +
MD_Parola animation
        +
I2S audio acquisition
        +
FFT calculations
        +
Async web server
        +
Wi-Fi
        +
SNTP
        +
AHT10
```

For this reason, the firmware avoids long blocking operations during normal runtime.
Music Sync is intentionally implemented as a separate rendering path so the audio visualization can update the display directly.

---

Project Concept
The overall project can be viewed as an ESP32-based programmable LED matrix studio:

```text
                 ┌─────────────────────┐
                 │     Web Browser     │
                 │  PC / Mobile / UI   │
                 └──────────┬──────────┘
                            │ HTTP
                            ▼
                 ┌─────────────────────┐
                 │       ESP32         │
                 │                     │
                 │ AsyncWebServer      │
                 │ SPIFFS              │
                 │ Wi-Fi / mDNS        │
                 │ SNTP                │
                 └──────┬───────┬──────┘
                        │       │
              ┌─────────┘       └─────────┐
              ▼                           ▼
      ┌──────────────┐             ┌──────────────┐
      │ MD_Parola    │             │ Music Sync   │
      │ Text Scenes  │             │ I2S + FFT    │
      └──────┬───────┘             └──────┬───────┘
             │                            │
             └────────────┬───────────────┘
                          ▼
                  ┌───────────────┐
                  │   MAX7219     │
                  │  LED MATRIX   │
                  └───────────────┘

       ┌─────────────┐       ┌─────────────┐
       │   INMP441   │       │    AHT10    │
       │ Microphone  │       │ Temp / Hum  │
       └─────────────┘       └─────────────┘
```

---

### `LICENSE`

```text
MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE..

---

Project Status
This project is an actively developed ESP32 LED matrix controller.
The current firmware already combines:
MAX7219 matrix control
MD_Parola animations
Multi-zone scenes
Custom bitmap font
Web-based configuration
SPIFFS storage
Wi-Fi management
mDNS
SNTP
AHT10 sensor support
INMP441 audio input
ArduinoFFT
Real-time Music Sync animations
Future development can expand the Music Sync engine, zone handling, long-chain MAX7219 support, web UI, configuration reliability, and additional audio-reactive effects.
```
