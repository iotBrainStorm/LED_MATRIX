#include "time.h"           // Time & NTP management
#include <Adafruit_AHT10.h> // AHT10 Temperature & Humidity sensor
#include <Arduino.h>
#include <ArduinoJson.h>       // JSON parsing and serialization
#include <EEPROM.h>            // EEPROM emulation
#include <ESPAsyncWebServer.h> // Non-blocking async web server
#include <HTTPClient.h>        // HTTP client utility
#include <HardwareSerial.h>    // Hardware Serial
#include <MD_MAX72xx.h>        // LED Matrix MAX72XX hardware driver
#include <MD_Parola.h>         // LED Matrix Parola library
#include <Preferences.h>       // NVS storage
#include <SPI.h>               // SPI communication for LED matrix
#include <SPIFFS.h>            // Flash File System
#include <WiFi.h>
#include <WiFiManager.h> // Config portal for WiFi credentials
#include <Wire.h>        // I2C communication for sensors
#include <math.h>

// ==========================================
// HARDWARE DEFINITION & PIN ASSIGNMENTS
// ==========================================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 24 // Max possible physical 8x8 modules supported
#define CLK_PIN 18     // SPI SCK (VSPI default)
#define DATA_PIN 23    // SPI MOSI (VSPI default)
#define CS_PIN 5       // SPI SS / Chip Select

#define MAX_ZONES 4 // Max simultaneous Parola zones supported
#define CONFIG_FILE "/config.json"
const char *BUILD_ETAG = "\"" __DATE__ "-" __TIME__ "\"";

// ==========================================
// GLOBAL OBJECTS & STATE
// ==========================================
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
Adafruit_AHT10 aht;
AsyncWebServer server(80);

bool ahtFound = false;
volatile bool configUpdated = false;

// ==========================================
// 8x6 BOLD BITMAP FONT TABLE (ASCII 32-126)
// ==========================================
const uint8_t PROGMEM FONT_8x6_RAW[95][8] = {
    /* 32   */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 33 ! */ {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18},
    /* 34 " */ {0x6c, 0x6c, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 35 # */ {0x24, 0x24, 0x7e, 0x24, 0x24, 0x7e, 0x24, 0x24},
    /* 36 $ */ {0x18, 0x3c, 0x7a, 0x3c, 0x5e, 0x7c, 0x38, 0x18},
    /* 37 % */ {0x00, 0x62, 0x66, 0x0c, 0x18, 0x30, 0x66, 0x06},
    /* 38 & */ {0x18, 0x24, 0x24, 0x18, 0x30, 0x6a, 0x64, 0x3a},
    /* 39 ' */ {0x18, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 40 ( */ {0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0c},
    /* 41 ) */ {0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c, 0x18, 0x30},
    /* 42 * */ {0x00, 0x18, 0x5a, 0x3c, 0x3c, 0x5a, 0x18, 0x00},
    /* 43 + */ {0x00, 0x18, 0x18, 0x7e, 0x7e, 0x18, 0x18, 0x00},
    /* 44 , */ {0x00, 0x00, 0x00, 0x00, 0x60, 0x60, 0x20, 0x40},
    /* 45 - */ {0x00, 0x00, 0x00, 0x7e, 0x7e, 0x00, 0x00, 0x00},
    /* 46 . */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* 47 / */ {0x00, 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x00},
    /* 48 0 */ {0x3c, 0x66, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c},
    /* 49 1 */ {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c},
    /* 50 2 */ {0x3c, 0x66, 0x46, 0x0c, 0x18, 0x30, 0x66, 0x7e},
    /* 51 3 */ {0x3c, 0x66, 0x06, 0x1c, 0x06, 0x06, 0x66, 0x3c},
    /* 52 4 */ {0x0c, 0x18, 0x30, 0x60, 0x6c, 0x7e, 0x0c, 0x0c},
    /* 53 5 */ {0x7e, 0x60, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c},
    /* 54 6 */ {0x3c, 0x66, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x3c},
    /* 55 7 */ {0x7E, 0x46, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30},
    /* 56 8 */ {0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x66, 0x3c},
    /* 57 9 */ {0x3c, 0x66, 0x66, 0x66, 0x3e, 0x06, 0x66, 0x3c},
    /* 58 : */ {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* 59 ; */ {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x08, 0x10},
    /* 60 < */ {0x00, 0x18, 0x30, 0x60, 0x60, 0x30, 0x18, 0x00},
    /* 61 = */ {0x00, 0x00, 0x7e, 0x7e, 0x00, 0x7e, 0x7e, 0x00},
    /* 62 > */ {0x00, 0x18, 0x0c, 0x06, 0x06, 0x0c, 0x18, 0x00},
    /* 63 ? */ {0x3c, 0x66, 0x66, 0x06, 0x0c, 0x18, 0x00, 0x18},
    /* 64 @ */ {0x1c, 0x3e, 0x22, 0x02, 0x3a, 0x2a, 0x3e, 0x1c},
    /* 65 A */ {0x3c, 0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66},
    /* 66 B */ {0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x66, 0x7c},
    /* 67 C */ {0x3c, 0x66, 0x60, 0x60, 0x60, 0x60, 0x66, 0x3c},
    /* 68 D */ {0x7c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7c},
    /* 69 E */ {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x7e},
    /* 70 F */ {0x7e, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x60},
    /* 71 G */ {0x3e, 0x66, 0x60, 0x60, 0x6e, 0x66, 0x66, 0x3e},
    /* 72 H */ {0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x66},
    /* 73 I */ {0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c},
    /* 74 J */ {0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x6c, 0x38},
    /* 75 K */ {0x66, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0x66},
    /* 76 L */ {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x7e},
    /* 77 M */ {0x42, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x66, 0x66},
    /* 78 N */ {0x66, 0x66, 0x76, 0x76, 0x6e, 0x6e, 0x66, 0x66},
    /* 79 O */ {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c},
    /* 80 P */ {0x7c, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60},
    /* 81 Q */ {0x3c, 0x66, 0x66, 0x66, 0x66, 0x6a, 0x64, 0x3a},
    /* 82 R */ {0x7c, 0x66, 0x66, 0x66, 0x7c, 0x6c, 0x66, 0x66},
    /* 83 S */ {0x3c, 0x66, 0x60, 0x7c, 0x3e, 0x06, 0x66, 0x3c},
    /* 84 T */ {0x7e, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    /* 85 U */ {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c},
    /* 86 V */ {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18},
    /* 87 W */ {0x66, 0x66, 0x66, 0x66, 0x66, 0x7e, 0x66, 0x42},
    /* 88 X */ {0x66, 0x66, 0x3c, 0x18, 0x18, 0x3c, 0x66, 0x66},
    /* 89 Y */ {0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x18},
    /* 90 Z */ {0x7e, 0x7e, 0x06, 0x0c, 0x18, 0x30, 0x7e, 0x7e},
    /* 91 [ */ {0x38, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x38},
    /* 92 \ */ {0x00, 0x40, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x00},
    /* 93 ] */ {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x38},
    /* 94 ^ */ {0x18, 0x24, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 95 _ */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x7e},
    /* 96 ` */ {0x60, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 97 a */ {0x00, 0x00, 0x3c, 0x06, 0x3e, 0x66, 0x66, 0x3a},
    /* 98 b */ {0x60, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x66, 0x7c},
    /* 99 c */ {0x00, 0x00, 0x3e, 0x60, 0x60, 0x60, 0x60, 0x3e},
    /* 100 d */ {0x06, 0x06, 0x3e, 0x66, 0x66, 0x66, 0x66, 0x3e},
    /* 101 e */ {0x00, 0x00, 0x3c, 0x66, 0x7e, 0x60, 0x60, 0x3c},
    /* 102 f */ {0x0c, 0x18, 0x18, 0x18, 0x3c, 0x18, 0x18, 0x18},
    /* 103 g */ {0x00, 0x00, 0x3e, 0x66, 0x66, 0x3e, 0x06, 0x3c},
    /* 104 h */ {0x60, 0x60, 0x78, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c},
    /* 105 i */ {0x18, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    /* 106 j */ {0x18, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x30},
    /* 107 k */ {0x60, 0x60, 0x66, 0x6e, 0x78, 0x6c, 0x66, 0x66},
    /* 108 l */ {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1c},
    /* 109 m */ {0x00, 0x00, 0x74, 0x6a, 0x6a, 0x6a, 0x6a, 0x6a},
    /* 110 n */ {0x00, 0x00, 0x78, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c},
    /* 111 o */ {0x00, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c},
    /* 112 p */ {0x00, 0x00, 0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60},
    /* 113 q */ {0x00, 0x00, 0x3e, 0x66, 0x66, 0x3e, 0x06, 0x06},
    /* 114 r */ {0x00, 0x00, 0x66, 0x6e, 0x70, 0x60, 0x60, 0x60},
    /* 115 s */ {0x00, 0x00, 0x3e, 0x60, 0x7c, 0x3e, 0x06, 0x7c},
    /* 116 t */ {0x18, 0x18, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x0c},
    /* 117 u */ {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c},
    /* 118 v */ {0x00, 0x00, 0x6c, 0x6c, 0x6c, 0x6c, 0x38, 0x10},
    /* 119 w */ {0x00, 0x00, 0x62, 0x62, 0x6a, 0x6a, 0x6a, 0x34},
    /* 120 x */ {0x00, 0x00, 0x66, 0x3c, 0x18, 0x18, 0x3c, 0x66},
    /* 121 y */ {0x00, 0x00, 0x66, 0x66, 0x66, 0x3e, 0x06, 0x3c},
    /* 122 z */ {0x00, 0x00, 0x7e, 0x64, 0x08, 0x10, 0x26, 0x7e},
    /* 123 { */ {0x0c, 0x18, 0x18, 0x30, 0x18, 0x18, 0x0c, 0x00},
    /* 124 | */ {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    /* 125 } */ {0x30, 0x18, 0x18, 0x0c, 0x18, 0x18, 0x30, 0x00},
    /* 126 ~ */ {0x00, 0x00, 0x00, 0x32, 0x7e, 0x4c, 0x00, 0x00}};

uint8_t customBoldFont[3 + (95 * 7)];

void buildCustomBoldFont() {
  customBoldFont[0] = 32;  // First character
  customBoldFont[1] = 126; // Last character
  customBoldFont[2] = 8;   // Height in pixels

  uint16_t ptr = 3;
  for (uint8_t i = 0; i < 95; i++) {
    customBoldFont[ptr++] = 6; // 6 columns width
    for (uint8_t col = 1; col <= 6; col++) {
      uint8_t colByte = 0;
      for (uint8_t row = 0; row < 8; row++) {
        uint8_t rowByte = pgm_read_byte(&FONT_8x6_RAW[i][row]);
        if ((rowByte >> (7 - col)) & 1) {
          colByte |= (1 << row);
        }
      }
      customBoldFont[ptr++] = colByte;
    }
  }
}

// ==========================================
// SCENE & ZONE CONFIGURATION STRUCTURES
// ==========================================
struct ZoneConfig {
  bool inUse = false;
  uint8_t startDev = 0;
  uint8_t endDev = 0;
  char rawMessage[128] = "";
  char activeMessage[128] = "";
  bool isCustom = false;
  bool isBold = false;
  textPosition_t align = PA_CENTER;
  textEffect_t inEffect = PA_SCROLL_LEFT;
  textEffect_t outEffect = PA_SCROLL_LEFT;
  uint16_t speed = 35;
  uint16_t pause = 0;
  uint8_t brightness = 12;
  int repeat = -1;
  int loopCounter = 0;
};

ZoneConfig zones[MAX_ZONES];
uint8_t activeZoneCount = 1;

// ==========================================
// STRING & TEMPLATE PARSING UTILITIES
// ==========================================
textEffect_t parseEffect(const char *str) {
  if (strcmp(str, "PA_PRINT") == 0)
    return PA_PRINT;
  if (strcmp(str, "PA_SCROLL_LEFT") == 0)
    return PA_SCROLL_LEFT;
  if (strcmp(str, "PA_SCROLL_RIGHT") == 0)
    return PA_SCROLL_RIGHT;
  if (strcmp(str, "PA_SCROLL_UP") == 0)
    return PA_SCROLL_UP;
  if (strcmp(str, "PA_SCROLL_DOWN") == 0)
    return PA_SCROLL_DOWN;
  if (strcmp(str, "PA_SCROLL_UP_LEFT") == 0)
    return PA_SCROLL_UP_LEFT;
  if (strcmp(str, "PA_SCROLL_UP_RIGHT") == 0)
    return PA_SCROLL_UP_RIGHT;
  if (strcmp(str, "PA_SCROLL_DOWN_LEFT") == 0)
    return PA_SCROLL_DOWN_LEFT;
  if (strcmp(str, "PA_SCROLL_DOWN_RIGHT") == 0)
    return PA_SCROLL_DOWN_RIGHT;
  if (strcmp(str, "PA_SPRITE") == 0)
    return PA_SPRITE;
  if (strcmp(str, "PA_SLICE") == 0)
    return PA_SLICE;
  if (strcmp(str, "PA_MESH") == 0)
    return PA_MESH;
  if (strcmp(str, "PA_FADE") == 0)
    return PA_FADE;
  if (strcmp(str, "PA_DISSOLVE") == 0)
    return PA_DISSOLVE;
  if (strcmp(str, "PA_BLINDS") == 0)
    return PA_BLINDS;
  if (strcmp(str, "PA_RANDOM") == 0)
    return PA_RANDOM;
  if (strcmp(str, "PA_WIPE") == 0)
    return PA_WIPE;
  if (strcmp(str, "PA_WIPE_CURSOR") == 0)
    return PA_WIPE_CURSOR;
  if (strcmp(str, "PA_OPENING") == 0)
    return PA_OPENING;
  if (strcmp(str, "PA_OPENING_CURSOR") == 0)
    return PA_OPENING_CURSOR;
  if (strcmp(str, "PA_CLOSING") == 0)
    return PA_CLOSING;
  if (strcmp(str, "PA_CLOSING_CURSOR") == 0)
    return PA_CLOSING_CURSOR;
  if (strcmp(str, "PA_SCAN_HORIZ") == 0)
    return PA_SCAN_HORIZ;
  if (strcmp(str, "PA_SCAN_HORIZX") == 0)
    return PA_SCAN_HORIZX;
  if (strcmp(str, "PA_SCAN_VERT") == 0)
    return PA_SCAN_VERT;
  if (strcmp(str, "PA_SCAN_VERTX") == 0)
    return PA_SCAN_VERTX;
  if (strcmp(str, "PA_GROW_UP") == 0)
    return PA_GROW_UP;
  if (strcmp(str, "PA_GROW_DOWN") == 0)
    return PA_GROW_DOWN;
  return PA_SCROLL_LEFT;
}

textPosition_t parseAlign(const char *str) {
  if (strcmp(str, "left") == 0)
    return PA_LEFT;
  if (strcmp(str, "right") == 0)
    return PA_RIGHT;
  return PA_CENTER;
}

/**
 * High-performance dynamic template parser.
 * Combines AHT10 sensor readings with C-standard strftime formatting.
 */
String processTemplate(const String &tmpl) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);

  float temp = 26.0;
  float hum = 62.0;
  if (ahtFound) {
    sensors_event_t hEvent, tEvent;
    aht.getEvent(&hEvent, &tEvent);
    temp = tEvent.temperature;
    hum = hEvent.relative_humidity;
  }

  String out = tmpl;

  // 1. Replace sensor placeholders first
  char sensorBuf[16];
  snprintf(sensorBuf, sizeof(sensorBuf), "%.1f", temp);
  out.replace("{TEMP}", sensorBuf);

  snprintf(sensorBuf, sizeof(sensorBuf), "%.0f", hum);
  out.replace("{HUM}", sensorBuf);

  // 2. Escape literal '%' to '%%' so strftime doesn't choke on tokens like {HUM}%
  out.replace("%", "%%");

  // 3. Map custom web tags to standard POSIX strftime format specifiers
  // Note: 4-character tokens are replaced before shorter ones to prevent partial matches
  out.replace("{HH}", "%H");   // 24-hr [00-23]
  out.replace("{hh}", "%I");   // 12-hr [01-12]
  out.replace("{mm}", "%M");   // Minute [00-59]
  out.replace("{ss}", "%S");   // Second [00-59]
  out.replace("{AMPM}", "%p"); // AM / PM
  out.replace("{DD}", "%d");   // Day [01-31]
  out.replace("{dd}", "%e");   // Day [1-31]
  out.replace("{MM}", "%m");   // Month [01-12]
  out.replace("{MMMM}", "%B"); // Full month (e.g. January)
  out.replace("{MMM}", "%b");  // Abbr month (e.g. Jan)
  out.replace("{YYYY}", "%Y"); // Year (e.g. 2026)
  out.replace("{YY}", "%y");   // Year 2-digit (e.g. 26)
  out.replace("{WWWW}", "%A"); // Full weekday (e.g. Sunday)
  out.replace("{WWW}", "%a");  // Abbr weekday (e.g. Sun)

  // 4. Single-pass C-standard string formatting
  char formatted[160];
  if (strftime(formatted, sizeof(formatted), out.c_str(), &t) > 0) {
    return String(formatted);
  }

  return out;
}

// ==========================================
// CONFIGURATION PERSISTENCE & HARDWARE SYNC
// ==========================================
void applyZoneConfiguration(uint8_t z) {
  if (z >= MAX_ZONES || !zones[z].inUse)
    return;

  if (zones[z].isBold) {
    P.setFont(z, customBoldFont);
  } else {
    P.setFont(z, nullptr);
  }

  P.setIntensity(z, zones[z].brightness);

  String resolved = zones[z].isCustom ? processTemplate(zones[z].rawMessage) : String(zones[z].rawMessage);
  strncpy(zones[z].activeMessage, resolved.c_str(), sizeof(zones[z].activeMessage) - 1);
  zones[z].activeMessage[sizeof(zones[z].activeMessage) - 1] = '\0';

  P.displayZoneText(
      z,
      zones[z].activeMessage,
      zones[z].align,
      zones[z].speed,
      zones[z].pause,
      zones[z].inEffect,
      zones[z].outEffect);
  P.displayReset(z);
}

// ==========================================
// CONFIGURATION PERSISTENCE & HARDWARE SYNC
// ==========================================
void saveDefaultConfiguration() {
  File file = SPIFFS.open(CONFIG_FILE, "w");
  if (!file) {
    Serial.println("[Config] Failed to create default /config.json");
    return;
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(2048);
#endif

  doc["device"] = "ESP_LED_MATRIX_MD_PAROLA";
  JsonObject matrix = doc["matrix"].to<JsonObject>();
  matrix["height"] = 8;
  matrix["width"] = 40;
  matrix["modules"] = 5;

  JsonArray scenesArr = doc["scenes"].to<JsonArray>();

  // Default Scene 1: Zone 1 (Cols 0 to 15 -> Modules 0 to 1)
  JsonObject sc1 = scenesArr.add<JsonObject>();
  sc1["sceneName"] = "Status Zone";
  JsonObject z1 = sc1["zone"].to<JsonObject>();
  z1["name"] = "Zone 1";
  z1["startCol"] = 0;
  z1["endCol"] = 15;
  JsonObject m1 = sc1["message"].to<JsonObject>();
  m1["type"] = "plain";
  m1["content"] = "ESP";
  m1["bold"] = false;
  m1["align"] = "center";
  JsonObject a1 = sc1["animation"].to<JsonObject>();
  a1["inEffect"] = "PA_SCROLL_LEFT";
  a1["outEffect"] = "PA_SCROLL_LEFT";
  a1["speedMs"] = 35;
  a1["startDelayMs"] = 0;
  a1["endDelayMs"] = 0;
  JsonObject d1 = sc1["display"].to<JsonObject>();
  d1["brightness"] = 12;
  d1["repeat"] = -1;

  // Default Scene 2: Zone 2 (Cols 16 to 39 -> Modules 2 to 4)
  JsonObject sc2 = scenesArr.add<JsonObject>();
  sc2["sceneName"] = "Clock Zone";
  JsonObject z2 = sc2["zone"].to<JsonObject>();
  z2["name"] = "Zone 2";
  z2["startCol"] = 16;
  z2["endCol"] = 39;
  JsonObject m2 = sc2["message"].to<JsonObject>();
  m2["type"] = "custom";
  m2["content"] = "{HH}:{mm}:{ss}";
  m2["bold"] = false;
  m2["align"] = "center";
  JsonObject a2 = sc2["animation"].to<JsonObject>();
  a2["inEffect"] = "PA_PRINT";
  a2["outEffect"] = "PA_PRINT";
  a2["speedMs"] = 35;
  a2["startDelayMs"] = 0;
  a2["endDelayMs"] = 1000;
  JsonObject d2 = sc2["display"].to<JsonObject>();
  d2["brightness"] = 12;
  d2["repeat"] = -1;

  serializeJson(doc, file);
  file.close();
  Serial.println("[Config] Fresh default /config.json created in SPIFFS.");
}

void loadConfiguration() {
  if (!SPIFFS.exists(CONFIG_FILE)) {
    Serial.println("[Config] No saved config found in flash. Generating defaults...");
    saveDefaultConfiguration();
  }

  File file = SPIFFS.open(CONFIG_FILE, "r");
  if (!file) {
    Serial.println("[Config] Failed to open /config.json");
    return;
  }

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(4096);
#endif

  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("[Config] JSON Deserialization error: %s\n", err.c_str());
    return;
  }

  JsonArray scenesArr = doc["scenes"].as<JsonArray>();
  if (scenesArr.isNull() || scenesArr.size() == 0) {
    Serial.println("[Config] 'scenes' array empty or invalid.");
    return;
  }

  P.displayClear();
  activeZoneCount = min((int)scenesArr.size(), (int)MAX_ZONES);

  Serial.println("========================================");
  Serial.printf("[Config] Loading %d Scenes/Zones from Flash:\n", activeZoneCount);

  for (uint8_t i = 0; i < MAX_ZONES; i++) {
    if (i < activeZoneCount) {
      JsonObject sc = scenesArr[i];
      zones[i].inUse = true;

      // Extract column boundaries (supports startCol/start and endCol/end)
      int startCol = 0;
      if (sc["zone"].is<JsonObject>()) {
        if (sc["zone"]["startCol"].is<int>())
          startCol = sc["zone"]["startCol"].as<int>();
        else if (sc["zone"]["start"].is<int>())
          startCol = sc["zone"]["start"].as<int>();
      }

      int endCol = (MAX_DEVICES * 8) - 1;
      if (sc["zone"].is<JsonObject>()) {
        if (sc["zone"]["endCol"].is<int>())
          endCol = sc["zone"]["endCol"].as<int>();
        else if (sc["zone"]["end"].is<int>())
          endCol = sc["zone"]["end"].as<int>();
      }

      // Convert physical pixel columns to MAX7219 device indices
      zones[i].startDev = constrain(startCol / 8, 0, MAX_DEVICES - 1);
      zones[i].endDev = constrain(endCol / 8, zones[i].startDev, MAX_DEVICES - 1);

      const char *mType = sc["message"]["type"] | "plain";
      zones[i].isCustom = (strcmp(mType, "custom") == 0);

      const char *mContent = sc["message"]["content"] | "ESP";
      strncpy(zones[i].rawMessage, mContent, sizeof(zones[i].rawMessage) - 1);
      zones[i].rawMessage[sizeof(zones[i].rawMessage) - 1] = '\0';

      zones[i].isBold = sc["message"]["bold"] | false;
      zones[i].align = parseAlign(sc["message"]["align"] | "center");
      zones[i].inEffect = parseEffect(sc["animation"]["inEffect"] | "PA_SCROLL_LEFT");
      zones[i].outEffect = parseEffect(sc["animation"]["outEffect"] | "PA_SCROLL_LEFT");
      zones[i].speed = sc["animation"]["speedMs"] | 35;
      zones[i].pause = sc["animation"]["endDelayMs"] | 0;
      zones[i].brightness = sc["display"]["brightness"] | 12;
      zones[i].repeat = sc["display"]["repeat"] | -1;
      zones[i].loopCounter = 0;

      // Configure Parola hardware zone
      P.setZone(i, zones[i].startDev, zones[i].endDev);
      applyZoneConfiguration(i);

      // Hardware verification log
      Serial.printf("  -> Zone %u ('%s'): Cols [%d..%d] -> Modules [%u..%u] | Msg: '%s'\n",
                    i,
                    sc["zone"]["name"] | sc["sceneName"] | "Zone",
                    startCol, endCol,
                    zones[i].startDev, zones[i].endDev,
                    zones[i].rawMessage);
    } else {
      zones[i].inUse = false;
    }
  }
  Serial.println("========================================");
}

// ==========================================
// ASYNC HTTP SERVER ROUTING
// ==========================================
void setupWebServer() {
  // 1. Root index.html directly from SPIFFS with ETag validation & 7-day caching
  server.on("/", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasHeader("If-None-Match")) {
      const AsyncWebHeader *h = request->getHeader("If-None-Match");
      if (h && h->value() == BUILD_ETAG) {
        request->send(304); // Browser uses cached version (0 bytes transferred)
        return;
      }
    }
    if (!SPIFFS.exists("/index.html")) {
      request->send(404, "text/plain", "index.html missing from SPIFFS! Please flash data folder.");
      return;
    }
    AsyncWebServerResponse *res = request->beginResponse(SPIFFS, "/index.html", "text/html");
    res->addHeader("ETag", BUILD_ETAG);
    res->addHeader("Cache-Control", "public, max-age=604800, must-revalidate");
    request->send(res);
  });

  // 2. Static Resources (Icons / Images)
  server.serveStatic("/icon.png", SPIFFS, "/icon.png").setCacheControl("max-age=604800");

  // 3. GET /api/matrix/config -> Direct flash file streaming
  server.on("/api/matrix/config", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists(CONFIG_FILE)) {
      AsyncWebServerResponse *res = request->beginResponse(SPIFFS, CONFIG_FILE, "application/json");
      res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      request->send(res);
    } else {
      request->send(200, "application/json", "{\"scenes\":[]}");
    }
  });

  // 4. POST /api/matrix/config -> Direct flash streaming upload
  server.on(
      "/api/matrix/config",
      WebRequestMethod::HTTP_POST,
      [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Saved to Flash\"}");
      },
      nullptr,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        static File uploadFile;
        if (index == 0) {
          if (SPIFFS.exists(CONFIG_FILE))
            SPIFFS.remove(CONFIG_FILE);
          uploadFile = SPIFFS.open(CONFIG_FILE, "w");
        }
        if (uploadFile) {
          uploadFile.write(data, len);
        }
        if (index + len >= total) {
          if (uploadFile) {
            uploadFile.close();
            Serial.printf("[SPIFFS] Streamed %u bytes to %s\n", total, CONFIG_FILE);
          }
          configUpdated = true; // Reload settings in loop()
        }
      });

  server.begin();
  Serial.println("[HTTP] AsyncWebServer online.");
}

// ==========================================
// SETUP & INITIALIZATION
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n======================================");
  Serial.println("   ESP32 LED Matrix Controller        ");
  Serial.println("======================================");

  // 1. Build and map custom 8x6 bold font table
  buildCustomBoldFont();

  // 2. Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS mount failed!");
  } else {
    Serial.println("[FS] SPIFFS mounted successfully.");
  }

  // 3. Initialize I2C and AHT10 sensor
  Wire.begin(21, 22);
  if (aht.begin()) {
    Serial.println("[Sensor] AHT10 found & initialized.");
    ahtFound = true;
  } else {
    Serial.println("[Sensor] AHT10 not found. Defaulting to virtual readings.");
  }

  // 4. Initialize Parola Matrix Display
  P.begin(MAX_ZONES);
  P.setIntensity(12);
  P.displayClear();

  // 5. WiFiManager: captive portal without hardcoded credentials
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  bool connected = wm.autoConnect("ESP_MATRIX_AP", "12345678");

  if (!connected) {
    Serial.println("[WiFi] Portal timed out. Running standalone SoftAP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP_MATRIX_AP", "12345678");
    Serial.print("[WiFi] Access Point IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.print("[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
  }

  // 6. NTP Clock Synchronization (IST = UTC+5:30 -> 19800 sec)
  configTime(19800, 0, "pool.ntp.org", "time.google.com");

  // 7. Mount Web Server routes
  setupWebServer();

  // 8. Load persistent matrix zones & scene config
  loadConfiguration();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // 1. Handle live configuration updates from the web UI
  if (configUpdated) {
    configUpdated = false;
    loadConfiguration();
  }

  // 2. Step Parola animation frames
  if (P.displayAnimate()) {
    for (uint8_t z = 0; z < activeZoneCount; z++) {
      if (zones[z].inUse && P.getZoneStatus(z)) {
        if (zones[z].repeat != -1) {
          zones[z].loopCounter++;
          if (zones[z].loopCounter >= zones[z].repeat) {
            continue;
          }
        }

        if (zones[z].isCustom) {
          String resolved = processTemplate(zones[z].rawMessage);
          strncpy(zones[z].activeMessage, resolved.c_str(), sizeof(zones[z].activeMessage) - 1);
          zones[z].activeMessage[sizeof(zones[z].activeMessage) - 1] = '\0';
        }

        P.displayReset(z);
      }
    }
  }
}