#include "esp_sntp.h"       // Non-blocking SNTP event callbacks
#include "time.h"           // Time & NTP management
#include <Adafruit_AHT10.h> // AHT10 Temperature & Humidity sensor
#include <Arduino.h>
#include <ArduinoJson.h>       // JSON parsing and serialization
#include <EEPROM.h>            // EEPROM emulation
#include <ESPAsyncWebServer.h> // Non-blocking async web server
#include <ESPmDNS.h>           // Local domain name resolution (.local)
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
#include <arduinoFFT.h>  // Audio FFT frequency analysis
#include <driver/i2s.h>  // ESP32 I2S driver for INMP441
#include <math.h>

// ==========================================
// HARDWARE DEFINITION & PIN ASSIGNMENTS
// ==========================================
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 30 // Max MAX7219 modules (30 * 8 = 240 cols max)
#define CLK_PIN 18     // SPI SCK
#define DATA_PIN 23    // SPI MOSI
#define CS_PIN 5       // SPI SS / Chip Select

// INMP441 I2S MEMS Microphone Pins
#define I2S_SCK 14 // Serial Clock (BCLK)
#define I2S_WS 15  // Word Select (LRCK)
#define I2S_SD 32  // Serial Data (DOUT)
#define I2S_PORT I2S_NUM_0

#define MAX_ZONES 4 // Max simultaneous Parola zones supported
#define CONFIG_FILE "/config.json"
const char *BUILD_ETAG = "\"" __DATE__ "-" __TIME__ "\"";

// ==========================================
// GLOBAL OBJECTS & STATE
// ==========================================
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
Adafruit_AHT10 aht;
AsyncWebServer server(80);
const char *mdnsHostname = "ledstudio";

const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.google.com";
const long gmtOffset_sec = 19800; // IST (UTC+5:30)
const int daylightOffset_sec = 0;

bool ahtFound = false;
volatile bool configUpdated = false;

// ==========================================
// MUSIC SYNC CONFIGURATION & FFT STATE
// ==========================================
struct MusicSyncConfig {
  bool enabled = false;
  char zone[32] = "Zone 1";
  int startCol = 0;
  int endCol = 39;
  char animation[32] = "VU Bar";
  int sensitivity = 60;
  char peakDecay[16] = "Medium";
};

MusicSyncConfig musicSync;
bool isMusicSyncActive = false;

// I2S & FFT Parameters
#define FFT_SAMPLES 64      // Must be a power of 2
#define SAMPLING_FREQ 16000 // 16 kHz sampling
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLING_FREQ);

// Animation smoothing and physics buffers
float smoothVol = 0.0f;
float peakPos = 0.0f;
unsigned long lastPeakDropTime = 0;
float bouncePos = 0.0f;
float bounceVel = 0.0f;
float waveHistory[MAX_DEVICES * 8] = {0};
float bandPeaks[MAX_DEVICES * 8] = {0};
unsigned long lastBandDropTime = 0;

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
    /* 45 - */ {0x00, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x00},
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

  char sensorBuf[16];
  snprintf(sensorBuf, sizeof(sensorBuf), "%.1f", temp);
  out.replace("{TEMP}", sensorBuf);

  snprintf(sensorBuf, sizeof(sensorBuf), "%.0f", hum);
  out.replace("{HUM}", sensorBuf);

  out.replace("%", "%%");

  out.replace("{HH}", "%H");
  out.replace("{hh}", "%I");
  out.replace("{mm}", "%M");
  out.replace("{ss}", "%S");
  out.replace("{AMPM}", "%p");
  out.replace("{DD}", "%d");
  out.replace("{dd}", "%e");
  out.replace("{MM}", "%m");
  out.replace("{MMMM}", "%B");
  out.replace("{MMM}", "%b");
  out.replace("{YYYY}", "%Y");
  out.replace("{YY}", "%y");
  out.replace("{WWWW}", "%A");
  out.replace("{WWW}", "%a");

  char formatted[160];
  if (strftime(formatted, sizeof(formatted), out.c_str(), &t) > 0) {
    return String(formatted);
  }

  return out;
}

// ==========================================
// I2S HARDWARE INITIALIZATION (INMP441)
// ==========================================
void initI2S() {
  const i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLING_FREQ,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = FFT_SAMPLES,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0};

  const i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_SD};

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] Driver install failed: 0x%x\n", err);
    return;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[I2S] Pin configuration failed: 0x%x\n", err);
    return;
  }

  i2s_start(I2S_PORT);
  Serial.println("[I2S] INMP441 MEMS microphone initialized successfully.");
}

// Helper: Set matrix point with orientation mapping (r=0 is bottom, r=7 is top)
inline void setVUMatrixPoint(MD_MAX72XX *mx, int r, int c, bool state) {
  if (c >= 0 && c < MAX_DEVICES * 8 && r >= 0 && r < 8) {
    mx->setPoint(7 - r, c, state);
  }
}

// Clear only the columns inside designated zone
void clearVUZone(MD_MAX72XX *mx, int startCol, int endCol) {
  for (int c = startCol; c <= endCol; c++) {
    for (int r = 0; r < 8; r++) {
      setVUMatrixPoint(mx, r, c, false);
    }
  }
}

// ==========================================
// REAL-TIME AUDIO SAMPLING & VU DRAWING
// ==========================================
void runMusicSyncFrame() {
  MD_MAX72XX *mx = P.getGraphicObject();
  if (!mx)
    return;

  int zStart = constrain(musicSync.startCol, 0, (MAX_DEVICES * 8) - 1);
  int zEnd = constrain(musicSync.endCol, zStart, (MAX_DEVICES * 8) - 1);
  int zWidth = zEnd - zStart + 1;

  // 1. Read I2S audio samples into buffer
  int32_t i2sRawBuffer[FFT_SAMPLES];
  size_t bytesRead = 0;
  esp_err_t res = i2s_read(I2S_PORT, i2sRawBuffer, sizeof(i2sRawBuffer), &bytesRead, pdMS_TO_TICKS(15));
  if (res != ESP_OK || bytesRead == 0)
    return;

  // 2. Compute RMS Amplitude & fill FFT Real array
  float sumSquares = 0.0f;
  int sampleCount = bytesRead / sizeof(int32_t);

  for (int i = 0; i < sampleCount; i++) {
    int32_t sample = i2sRawBuffer[i] >> 14; // INMP441 uses top 24 bits
    vReal[i] = (double)sample;
    vImag[i] = 0.0;
    sumSquares += (float)sample * sample;
  }

  float rms = sqrt(sumSquares / sampleCount);

  // Noise floor suppression & sensitivity scaling
  const float noiseFloor = 30.0f;
  if (rms < noiseFloor)
    rms = 0.0f;
  else
    rms -= noiseFloor;

  float gain = (musicSync.sensitivity / 50.0f);
  float rawVol = (rms * gain) / 3200.0f;
  rawVol = constrain(rawVol, 0.0f, 1.0f);

  // Fast attack, smooth decay volume envelope
  if (rawVol > smoothVol)
    smoothVol = rawVol;
  else
    smoothVol = (smoothVol * 0.75f) + (rawVol * 0.25f);

  // Peak decay interval based on configuration
  unsigned long decayInterval = 60; // Medium
  if (strcmp(musicSync.peakDecay, "Fast") == 0)
    decayInterval = 30;
  else if (strcmp(musicSync.peakDecay, "Smooth") == 0)
    decayInterval = 120;

  // Begin direct hardware frame
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  clearVUZone(mx, zStart, zEnd);

  // ========================================
  // ANIMATION 1: VU Bar
  // ========================================
  if (strcmp(musicSync.animation, "VU Bar") == 0) {
    int fillCols = (int)round(smoothVol * zWidth);
    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      if (i < fillCols) {
        int height = constrain(map(i, 0, zWidth - 1, 3, 8), 1, 8);
        for (int r = 0; r < height; r++) {
          setVUMatrixPoint(mx, r, c, true);
        }
      }
    }
  }

  // ========================================
  // ANIMATION 2: VU Peak
  // ========================================
  else if (strcmp(musicSync.animation, "VU Peak") == 0) {
    int fillCols = (int)round(smoothVol * zWidth);
    if (fillCols > peakPos) {
      peakPos = fillCols;
      lastPeakDropTime = millis();
    } else if (millis() - lastPeakDropTime >= decayInterval) {
      if (peakPos > 0)
        peakPos -= 0.5f;
      lastPeakDropTime = millis();
    }

    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      if (i < fillCols) {
        for (int r = 0; r < 7; r++) {
          setVUMatrixPoint(mx, r, c, true);
        }
      }
    }
    int pCol = zStart + (int)peakPos;
    if (pCol <= zEnd) {
      for (int r = 0; r < 8; r++) {
        setVUMatrixPoint(mx, r, pCol, true);
      }
    }
  }

  // ========================================
  // ANIMATION 3: VU Mirror
  // ========================================
  else if (strcmp(musicSync.animation, "VU Mirror") == 0) {
    int center = zStart + (zWidth / 2);
    int halfSpan = (int)round(smoothVol * (zWidth / 2.0f));

    for (int i = 0; i <= halfSpan; i++) {
      int leftCol = center - i;
      int rightCol = center + i;
      int height = constrain(8 - (i * 8 / (zWidth / 2 + 1)), 2, 8);

      for (int r = 0; r < height; r++) {
        if (leftCol >= zStart)
          setVUMatrixPoint(mx, r, leftCol, true);
        if (rightCol <= zEnd)
          setVUMatrixPoint(mx, r, rightCol, true);
      }
    }
  }

  // ========================================
  // ANIMATION 4: VU Bounce
  // ========================================
  else if (strcmp(musicSync.animation, "VU Bounce") == 0) {
    float targetPos = smoothVol * (zWidth - 2);
    if (targetPos > bouncePos) {
      bounceVel = (targetPos - bouncePos) * 0.45f + 1.2f;
    }
    bounceVel -= 0.35f; // Gravity
    bouncePos += bounceVel;
    bouncePos = constrain(bouncePos, 0.0f, (float)(zWidth - 2));

    int bCol = zStart + (int)bouncePos;
    // Draw bouncing 2-pixel head
    for (int r = 2; r < 6; r++) {
      setVUMatrixPoint(mx, r, bCol, true);
      if (bCol + 1 <= zEnd)
        setVUMatrixPoint(mx, r, bCol + 1, true);
    }
    // Subtle trailing base
    for (int c = zStart; c <= bCol; c += 2) {
      setVUMatrixPoint(mx, 0, c, true);
    }
  }

  // ========================================
  // ANIMATION 5: VU Pulse
  // ========================================
  else if (strcmp(musicSync.animation, "VU Pulse") == 0) {
    int center = zStart + (zWidth / 2);
    int radius = (int)round(smoothVol * (zWidth / 2.0f));
    int vertHeight = (int)round(smoothVol * 4.0f);

    for (int d = 0; d <= radius; d++) {
      int c1 = center - d;
      int c2 = center + d;
      int h = constrain(vertHeight - (d / 2), 0, 4);

      for (int r = 3 - h; r <= 4 + h; r++) {
        if (c1 >= zStart)
          setVUMatrixPoint(mx, r, c1, true);
        if (c2 <= zEnd)
          setVUMatrixPoint(mx, r, c2, true);
      }
    }
  }

  // ========================================
  // ANIMATION 6: VU Wave (Oscilloscope Ripple)
  // ========================================
  else if (strcmp(musicSync.animation, "VU Wave") == 0) {
    // Shift historical waveform horizontally
    for (int i = 0; i < zWidth - 1; i++) {
      waveHistory[i] = waveHistory[i + 1];
    }
    waveHistory[zWidth - 1] = smoothVol;

    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      int waveH = (int)round(waveHistory[i] * 3.5f);
      for (int r = 3 - waveH; r <= 4 + waveH; r++) {
        setVUMatrixPoint(mx, r, c, true);
      }
    }
  }

  // ========================================
  // ANIMATION 7: VU Spectrum (arduinoFFT)
  // ========================================
  else if (strcmp(musicSync.animation, "VU Spectrum") == 0) {
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // Decay band peaks
    if (millis() - lastBandDropTime >= decayInterval) {
      for (int i = 0; i < zWidth; i++) {
        if (bandPeaks[i] > 0)
          bandPeaks[i] -= 0.5f;
      }
      lastBandDropTime = millis();
    }

    int usableBins = FFT_SAMPLES / 2; // 32 frequency bins
    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      int binIdx = map(i, 0, zWidth - 1, 1, usableBins - 2);
      double magnitude = vReal[binIdx] * (musicSync.sensitivity / 40.0f);

      int height = constrain((int)(magnitude / 350.0), 0, 8);
      if (height > bandPeaks[i]) {
        bandPeaks[i] = height;
      }

      // Draw spectrum vertical column
      for (int r = 0; r < height; r++) {
        setVUMatrixPoint(mx, r, c, true);
      }

      // Draw falling peak point on top of column
      int peakRow = (int)bandPeaks[i];
      if (peakRow > 0 && peakRow < 8) {
        setVUMatrixPoint(mx, peakRow, c, true);
      }
    }
  }

  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
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

  // Dedicated Music Sync Default Root Settings
  JsonObject ms = doc["music_sync"].to<JsonObject>();
  ms["enabled"] = false;
  ms["zone"] = "Zone 1";
  ms["start_col"] = 0;
  ms["end_col"] = 39;
  ms["animation"] = "VU Bar";
  ms["sensitivity"] = 60;
  ms["peak_decay"] = "Medium";

  JsonArray scenesArr = doc["scenes"].to<JsonArray>();

  JsonObject sc1 = scenesArr.add<JsonObject>();
  sc1["sceneName"] = "ESP";
  JsonObject z1 = sc1["zone"].to<JsonObject>();
  z1["name"] = "Zone 1";
  z1["startCol"] = 0;
  z1["endCol"] = 23;
  JsonObject m1 = sc1["message"].to<JsonObject>();
  m1["type"] = "plain";
  m1["content"] = "ESP";
  m1["bold"] = true;
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

  JsonObject sc2 = scenesArr.add<JsonObject>();
  sc2["sceneName"] = "32";
  JsonObject z2 = sc2["zone"].to<JsonObject>();
  z2["name"] = "Zone 2";
  z2["startCol"] = 24;
  z2["endCol"] = 39;
  JsonObject m2 = sc2["message"].to<JsonObject>();
  m2["type"] = "plain";
  m2["content"] = "32";
  m2["bold"] = true;
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

  // =========================================================================
  // STEP 1: FIRST CHECK MUSIC SYNC STATUS
  // =========================================================================
  if (doc["music_sync"].is<JsonObject>()) {
    musicSync.enabled = doc["music_sync"]["enabled"] | false;
    const char *zName = doc["music_sync"]["zone"] | "Zone 1";
    strncpy(musicSync.zone, zName, sizeof(musicSync.zone) - 1);
    musicSync.startCol = doc["music_sync"]["start_col"] | 0;
    musicSync.endCol = doc["music_sync"]["end_col"] | 39;
    const char *anim = doc["music_sync"]["animation"] | "VU Bar";
    strncpy(musicSync.animation, anim, sizeof(musicSync.animation) - 1);
    musicSync.sensitivity = doc["music_sync"]["sensitivity"] | 60;
    const char *decay = doc["music_sync"]["peak_decay"] | "Medium";
    strncpy(musicSync.peakDecay, decay, sizeof(musicSync.peakDecay) - 1);
  } else {
    musicSync.enabled = false;
  }

  // If Music Sync is ON: Focus ONLY on VU Meter Task
  if (musicSync.enabled) {
    isMusicSyncActive = true;
    P.displayClear();
    Serial.println("========================================");
    Serial.println("[TASK SWITCH] MUSIC SYNC IS ACTIVE!");
    Serial.printf(" -> Dedicated Mode : ESP runs exclusively as VU Meter\n");
    Serial.printf(" -> Zone Mapped    : '%s' [Cols %d -> %d]\n", musicSync.zone, musicSync.startCol, musicSync.endCol);
    Serial.printf(" -> Animation      : %s | Gain: %d%% | Decay: %s\n", musicSync.animation, musicSync.sensitivity, musicSync.peakDecay);
    Serial.println("========================================");
    return; // Exit early: do not load or animate Parola scenes!
  }

  // If Music Sync is OFF: Run normal multi-scene / zone renderer
  isMusicSyncActive = false;

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

      P.setZone(i, zones[i].startDev, zones[i].endDev);
      applyZoneConfiguration(i);

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
// NON-BLOCKING NTP TIME INITIALIZATION
// ==========================================
void timeSyncCallback(struct timeval *tv) {
  time_t now = tv->tv_sec;
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &timeinfo);
  Serial.printf("\n[NTP Callback] Synchronized successfully: %s\n", buf);
}

void initNTP() {
  struct tm tmFallback = {0};
  tmFallback.tm_year = 2026 - 1900;
  tmFallback.tm_mon = 0;
  tmFallback.tm_mday = 1;
  tmFallback.tm_hour = 12;
  time_t tFallback = mktime(&tmFallback);
  struct timeval tvFallback = {.tv_sec = tFallback, .tv_usec = 0};
  settimeofday(&tvFallback, nullptr);

  sntp_set_time_sync_notification_cb(timeSyncCallback);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
  Serial.println("[NTP] Background SNTP service started.");
}

// ==========================================
// WIFI SETUP
// ==========================================
bool connectToSavedWiFi() {
  Serial.println("\n==============================");
  Serial.println("WiFi Connection Started");
  Serial.println("==============================");

  WiFiManager wm;
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  int attempts = 0;
  const int MAX_ATTEMPTS = 10;

  while (attempts < MAX_ATTEMPTS) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[SUCCESS] Connected to Saved WiFi");
      Serial.printf("SSID       : %s\n", WiFi.SSID().c_str());
      Serial.printf("IP Address : %s\n", WiFi.localIP().toString().c_str());
      Serial.println("==============================\n");
      return true;
    }
    delay(300);
    attempts++;
  }

  Serial.println("\n[WARNING] No saved WiFi found! Starting Portal...");
  wm.setConfigPortalTimeout(180);
  bool success = wm.autoConnect("LED STUDIO");

  if (success) {
    Serial.println("\n[SUCCESS] Connected via Portal");
    Serial.printf("IP Address : %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("\n[INFO] Portal Timeout - Continuing in Offline Mode.");
  return false;
}

// ==========================================
// MDNS SERVER INIT
// ==========================================
void initMDNS() {
  MDNS.end();
  if (MDNS.begin(mdnsHostname)) {
    Serial.printf("[mDNS] Responder started: http://%s.local\n", mdnsHostname);
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("[mDNS] Error setting up MDNS responder!");
  }
}

// ==========================================
// ASYNC HTTP SERVER ROUTING
// ==========================================
void setupWebServer() {
  server.on("/", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasHeader("If-None-Match")) {
      const AsyncWebHeader *h = request->getHeader("If-None-Match");
      if (h && h->value() == BUILD_ETAG) {
        request->send(304);
        return;
      }
    }
    if (!SPIFFS.exists("/index.html")) {
      request->send(404, "text/plain", "index.html missing from SPIFFS!");
      return;
    }
    AsyncWebServerResponse *res = request->beginResponse(SPIFFS, "/index.html", "text/html");
    res->addHeader("ETag", BUILD_ETAG);
    res->addHeader("Cache-Control", "public, max-age=604800, must-revalidate");
    request->send(res);
  });

  server.serveStatic("/icon.svg", SPIFFS, "/icon.svg").setCacheControl("max-age=604800");

  server.on("/api/matrix/config", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists(CONFIG_FILE)) {
      AsyncWebServerResponse *res = request->beginResponse(SPIFFS, CONFIG_FILE, "application/json");
      res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      request->send(res);
    } else {
      request->send(200, "application/json", "{\"scenes\":[]}");
    }
  });

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
          configUpdated = true;
        }
      });

  server.begin();
  Serial.println("[HTTP] AsyncWebServer online.");
}

// ==========================================
// RUNTIME WIFI SUPERVISOR
// ==========================================
void checkWiFiAndStartServer() {
  static unsigned long lastCheck = 0;
  static unsigned long lastReconnectAttempt = 0;
  static bool wasConnected = (WiFi.status() == WL_CONNECTED);

  if (millis() - lastCheck < 3000)
    return;
  lastCheck = millis();

  bool isConnected = (WiFi.status() == WL_CONNECTED);

  if (isConnected && !wasConnected) {
    Serial.println("\n[WiFi] Reconnected!");
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    initMDNS();
    wasConnected = true;
  }

  if (!isConnected && wasConnected) {
    Serial.println("\n[WiFi] Disconnected! Background reconnect active.");
    wasConnected = false;
  }

  if (!isConnected) {
    if (millis() - lastReconnectAttempt > 15000) {
      lastReconnectAttempt = millis();
      Serial.println("[WiFi] Reconnect trigger...");
      WiFi.reconnect();
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n==============================");
  Serial.println("ESP32 LED Matrix + Music Sync");
  Serial.println("==============================");

  // 1. Build font table
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

  // 4. Initialize Parola Display & MAX72XX
  P.begin(MAX_ZONES);
  P.setIntensity(12);
  P.displayClear();

  // 5. Initialize I2S for INMP441 MEMS microphone
  initI2S();

  // 6. Connect WiFi
  if (connectToSavedWiFi()) {
    initMDNS();
  }

  // 7. Non-blocking NTP
  initNTP();

  // 8. Mount Web Server
  setupWebServer();

  // 9. Load config (Evaluates music_sync.enabled first)
  loadConfiguration();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // 1. Handle live config updates pushed from browser
  if (configUpdated) {
    configUpdated = false;
    loadConfiguration();
  }

  // 2. TASK EXECUTION BRANCHING
  if (isMusicSyncActive) {
    // DEDICATED TASK: Microsecond real-time audio sampling & VU Meter
    runMusicSyncFrame();
  } else {
    // DEFAULT TASK: Multi-zone scene text and animation rendering
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

  // 3. Non-blocking background network supervisor
  checkWiFiAndStartServer();
}