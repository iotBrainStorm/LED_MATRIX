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
MD_MAX72XX::moduleType_t HARDWARE_TYPE = MD_MAX72XX::FC16_HW;
char hardwareTypeStr[20] = "FC16_HW";

MD_MAX72XX::moduleType_t parseHardwareType(const char *str) {
  if (strcmp(str, "GENERIC_HW") == 0)
    return MD_MAX72XX::GENERIC_HW;
  if (strcmp(str, "PAROLA_HW") == 0)
    return MD_MAX72XX::PAROLA_HW;
  if (strcmp(str, "ICSTATION_HW") == 0)
    return MD_MAX72XX::ICSTATION_HW;
  return MD_MAX72XX::FC16_HW; // Default
}

#define ABSOLUTE_MAX_DEVICES 30
uint8_t MAX_DEVICES = 5;
#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 5

// INMP441 I2S MEMS Microphone Pins
#define I2S_SCK 14
#define I2S_WS 15
#define I2S_SD 32
#define I2S_PORT I2S_NUM_0

#define MAX_ZONES 8
#define MAX_SCENES 50
#define CONFIG_FILE "/config.json"
const char *BUILD_ETAG = "\"" __DATE__ "-" __TIME__ "\"";

// ==========================================
// GLOBAL OBJECTS & STATE
// ==========================================
MD_Parola *P_ptr = nullptr;
#define P (*P_ptr)
Adafruit_AHT10 aht;
AsyncWebServer server(80);
char mdnsHostname[32];

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
  uint8_t brightness = 12;
};

MusicSyncConfig musicSync;
bool isMusicSyncActive = false;

#define FFT_SAMPLES 64
#define SAMPLING_FREQ 16000
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLING_FREQ);

float smoothVol = 0.0f;
float peakPos = 0.0f;
unsigned long lastPeakDropTime = 0;
float bouncePos = 0.0f;
float bounceVel = 0.0f;
float waveHistory[ABSOLUTE_MAX_DEVICES * 8] = {0};
float bandPeaks[ABSOLUTE_MAX_DEVICES * 8] = {0};
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

uint8_t customBoldFont[32 + (96 * 7)];

void buildCustomBoldFont() {
  uint16_t ptr = 0;
  for (uint8_t i = 0; i < 32; i++) {
    customBoldFont[ptr++] = 0;
  }
  for (uint8_t i = 0; i < 95; i++) {
    uint8_t cols[6];
    for (uint8_t col = 0; col < 6; col++) {
      cols[col] = 0;
      for (uint8_t row = 0; row < 8; row++) {
        uint8_t rowByte = pgm_read_byte(&FONT_8x6_RAW[i][row]);
        if ((rowByte >> (6 - col)) & 1) {
          cols[col] |= (1 << row);
        }
      }
    }
    int firstCol = 0;
    while (firstCol < 6 && cols[firstCol] == 0)
      firstCol++;
    int lastCol = 5;
    while (lastCol >= 0 && cols[lastCol] == 0)
      lastCol--;

    if (firstCol > lastCol) {
      customBoldFont[ptr++] = 4;
      customBoldFont[ptr++] = 0x00;
      customBoldFont[ptr++] = 0x00;
      customBoldFont[ptr++] = 0x00;
      customBoldFont[ptr++] = 0x00;
    } else {
      uint8_t width = lastCol - firstCol + 1;
      customBoldFont[ptr++] = width;
      for (int c = firstCol; c <= lastCol; c++) {
        customBoldFont[ptr++] = cols[c];
      }
    }
  }
  customBoldFont[ptr++] = 4;
  customBoldFont[ptr++] = 0x06;
  customBoldFont[ptr++] = 0x09;
  customBoldFont[ptr++] = 0x09;
  customBoldFont[ptr++] = 0x06;
}

// ==========================================
// CUSTOM 5x7 THIN FONT TABLE (ASCII 0-127)
// ==========================================
const uint8_t PROGMEM customThinFont[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    3, 0x00, 0x00, 0x00,             // 32  ' '
    1, 0x5f,                         // 33  !
    3, 0x07, 0x00, 0x07,             // 34  "
    5, 0x14, 0x7f, 0x14, 0x7f, 0x14, // 35  #
    5, 0x24, 0x2a, 0x7f, 0x2a, 0x12, // 36  $
    5, 0x23, 0x13, 0x08, 0x64, 0x62, // 37  %
    5, 0x36, 0x49, 0x55, 0x22, 0x50, // 38  &
    1, 0x05,                         // 39  '
    2, 0x1c, 0x22,                   // 40  (
    2, 0x22, 0x1c,                   // 41  )
    5, 0x14, 0x08, 0x3e, 0x08, 0x14, // 42  *
    5, 0x08, 0x08, 0x3e, 0x08, 0x08, // 43  +
    2, 0x50, 0x30,                   // 44  ,
    5, 0x08, 0x08, 0x08, 0x08, 0x08, // 45  -
    1, 0x60,                         // 46  .
    5, 0x20, 0x10, 0x08, 0x04, 0x02, // 47  /
    5, 0x3e, 0x51, 0x49, 0x45, 0x3e, // 48  0
    3, 0x42, 0x7f, 0x40,             // 49  1
    5, 0x42, 0x61, 0x51, 0x49, 0x46, // 50  2
    5, 0x21, 0x41, 0x45, 0x4b, 0x31, // 51  3
    5, 0x18, 0x14, 0x12, 0x7f, 0x10, // 52  4
    5, 0x27, 0x45, 0x45, 0x45, 0x39, // 53  5
    5, 0x3c, 0x4a, 0x49, 0x49, 0x30, // 54  6
    5, 0x01, 0x71, 0x09, 0x05, 0x03, // 55  7
    5, 0x36, 0x49, 0x49, 0x49, 0x36, // 56  8
    5, 0x06, 0x49, 0x49, 0x29, 0x1e, // 57  9
    1, 0x36,                         // 58  :
    1, 0x56,                         // 59  ;
    4, 0x08, 0x14, 0x22, 0x41,       // 60  <
    5, 0x14, 0x14, 0x14, 0x14, 0x14, // 61  =
    4, 0x41, 0x22, 0x14, 0x08,       // 62  >
    5, 0x02, 0x01, 0x51, 0x09, 0x06, // 63  ?
    5, 0x32, 0x49, 0x79, 0x41, 0x3e, // 64  @
    5, 0x7e, 0x11, 0x11, 0x11, 0x7e, // 65  A
    5, 0x7f, 0x49, 0x49, 0x49, 0x36, // 66  B
    5, 0x3e, 0x41, 0x41, 0x41, 0x22, // 67  C
    5, 0x7f, 0x41, 0x41, 0x22, 0x1c, // 68  D
    5, 0x7f, 0x49, 0x49, 0x49, 0x41, // 69  E
    5, 0x7f, 0x09, 0x09, 0x09, 0x01, // 70  F
    5, 0x3e, 0x41, 0x49, 0x49, 0x7a, // 71  G
    5, 0x7f, 0x08, 0x08, 0x08, 0x7f, // 72  H
    3, 0x41, 0x7f, 0x41,             // 73  I
    5, 0x20, 0x40, 0x41, 0x3f, 0x01, // 74  J
    5, 0x7f, 0x08, 0x14, 0x22, 0x41, // 75  K
    5, 0x7f, 0x40, 0x40, 0x40, 0x40, // 76  L
    5, 0x7f, 0x02, 0x0c, 0x02, 0x7f, // 77  M
    5, 0x7f, 0x04, 0x08, 0x10, 0x7f, // 78  N
    5, 0x3e, 0x41, 0x41, 0x41, 0x3e, // 79  O
    5, 0x7f, 0x09, 0x09, 0x09, 0x06, // 80  P
    5, 0x3e, 0x41, 0x51, 0x21, 0x5e, // 81  Q
    5, 0x7f, 0x09, 0x19, 0x29, 0x46, // 82  R
    5, 0x46, 0x49, 0x49, 0x49, 0x31, // 83  S
    5, 0x01, 0x01, 0x7f, 0x01, 0x01, // 84  T
    5, 0x3f, 0x40, 0x40, 0x40, 0x3f, // 85  U
    5, 0x1f, 0x20, 0x40, 0x20, 0x1f, // 86  V
    5, 0x7f, 0x20, 0x18, 0x20, 0x7f, // 87  W
    5, 0x63, 0x14, 0x08, 0x14, 0x63, // 88  X
    5, 0x07, 0x08, 0x70, 0x08, 0x07, // 89  Y
    5, 0x61, 0x51, 0x49, 0x45, 0x43, // 90  Z
    3, 0x7f, 0x41, 0x41,             // 91  [
    5, 0x02, 0x04, 0x08, 0x10, 0x20, // 92  '\\'
    3, 0x41, 0x41, 0x7f,             // 93  ]
    5, 0x04, 0x02, 0x01, 0x02, 0x04, // 94  ^
    5, 0x40, 0x40, 0x40, 0x40, 0x40, // 95  _
    2, 0x01, 0x02,                   // 96  `
    5, 0x20, 0x54, 0x54, 0x54, 0x78, // 97  a
    5, 0x7f, 0x48, 0x44, 0x44, 0x38, // 98  b
    5, 0x38, 0x44, 0x44, 0x44, 0x20, // 99  c
    5, 0x38, 0x44, 0x44, 0x48, 0x7f, // 100 d
    5, 0x38, 0x54, 0x54, 0x54, 0x18, // 101 e
    4, 0x08, 0x7e, 0x09, 0x01,       // 102 f
    5, 0x0c, 0x52, 0x52, 0x52, 0x3e, // 103 g
    5, 0x7f, 0x08, 0x04, 0x04, 0x78, // 104 h
    3, 0x44, 0x7d, 0x40,             // 105 i
    4, 0x20, 0x40, 0x44, 0x3d,       // 106 j
    4, 0x7f, 0x10, 0x28, 0x44,       // 107 k
    3, 0x41, 0x7f, 0x40,             // 108 l
    5, 0x7c, 0x04, 0x18, 0x04, 0x78, // 109 m
    5, 0x7c, 0x08, 0x04, 0x04, 0x78, // 110 n
    5, 0x38, 0x44, 0x44, 0x44, 0x38, // 111 o
    5, 0xfc, 0x24, 0x24, 0x24, 0x18, // 112 p
    5, 0x18, 0x24, 0x24, 0x18, 0xfc, // 113 q
    5, 0x7c, 0x08, 0x04, 0x04, 0x08, // 114 r
    5, 0x48, 0x54, 0x54, 0x54, 0x20, // 115 s
    4, 0x04, 0x3f, 0x44, 0x40,       // 116 t
    5, 0x3c, 0x40, 0x40, 0x20, 0x7c, // 117 u
    5, 0x1c, 0x20, 0x40, 0x20, 0x1c, // 118 v
    5, 0x3c, 0x40, 0x30, 0x40, 0x3c, // 119 w
    5, 0x44, 0x28, 0x10, 0x28, 0x44, // 120 x
    5, 0x0c, 0x50, 0x50, 0x50, 0x3c, // 121 y
    5, 0x44, 0x64, 0x54, 0x4c, 0x44, // 122 z
    3, 0x08, 0x36, 0x41,             // 123 {
    1, 0x7f,                         // 124 |
    3, 0x41, 0x36, 0x08,             // 125 }
    4, 0x08, 0x04, 0x08, 0x10,       // 126 ~
    4, 0x06, 0x09, 0x09, 0x06        // 127 °
};

// ==========================================
// SCENE & ZONE CONFIGURATION STRUCTURES
// ==========================================
struct SceneConfig {
  char name[32] = "";
  char zoneName[32] = "";
  int startCol = 0;
  int endCol = 39;
  char rawMessage[128] = "";
  char activeMessage[128] = "";
  bool isCustom = false;
  bool isBold = false;
  textPosition_t align = PA_CENTER;
  textEffect_t inEffect = PA_SCROLL_LEFT;
  textEffect_t outEffect = PA_SCROLL_LEFT;
  uint16_t speed = 35;
  uint16_t pause = 0;      // endDelayMs
  uint16_t startDelay = 0; // startDelayMs
  uint8_t brightness = 12;
  int repeat = -1;
};

enum ZoneAnimState {
  ZSTATE_START_DELAY,
  ZSTATE_PLAYING,
  ZSTATE_FINISHED
};

struct ZoneConfig {
  bool inUse = false;
  char name[32] = "";
  uint8_t startDev = 0;
  uint8_t endDev = 0;

  uint8_t sceneList[MAX_SCENES];
  uint8_t sceneCount = 0;
  uint8_t currentSceneIdx = 0; // Index in sceneList

  int playlistRepeat = 1;
  int playlistLoopCounter = 0;

  int loopCounter = 0;
  ZoneAnimState state = ZSTATE_PLAYING;
  unsigned long stateStartTime = 0;
};

SceneConfig scenes[MAX_SCENES];
uint8_t totalScenes = 0;

ZoneConfig zones[MAX_ZONES];
uint8_t activeZoneCount = 0;

// ==========================================
// STRING & TEMPLATE PARSING UTILITIES
// ==========================================
textEffect_t parseEffect(const char *str) {
  if (strcmp(str, "PA_NO_EFFECT") == 0)
    return PA_NO_EFFECT;
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

  static float cachedTemp = 26.0f;
  static float cachedHum = 62.0f;
  static unsigned long lastSensorPoll = 0;

  if (ahtFound && (millis() - lastSensorPoll >= 30000 || lastSensorPoll == 0)) {
    sensors_event_t hEvent, tEvent;
    aht.getEvent(&hEvent, &tEvent);
    cachedTemp = tEvent.temperature;
    cachedHum = hEvent.relative_humidity;
    lastSensorPoll = millis();
  }

  String out = tmpl;

  char sensorBuf[16];

  // Temperature tags: 2 decimals, 1 decimal, integer ({TEM} & {TEMP})
  snprintf(sensorBuf, sizeof(sensorBuf), "%.2f", cachedTemp);
  out.replace("{TEM2}", sensorBuf);
  snprintf(sensorBuf, sizeof(sensorBuf), "%.1f", cachedTemp);
  out.replace("{TEM1}", sensorBuf);
  snprintf(sensorBuf, sizeof(sensorBuf), "%.0f", cachedTemp);
  out.replace("{TEM}", sensorBuf);
  out.replace("{TEMP}", sensorBuf); // Keeps backward compatibility

  // Humidity tags: 2 decimals, 1 decimal, integer
  snprintf(sensorBuf, sizeof(sensorBuf), "%.2f", cachedHum);
  out.replace("{HUM2}", sensorBuf);
  snprintf(sensorBuf, sizeof(sensorBuf), "%.1f", cachedHum);
  out.replace("{HUM1}", sensorBuf);
  snprintf(sensorBuf, sizeof(sensorBuf), "%.0f", cachedHum);
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
  String res = out;
  if (strftime(formatted, sizeof(formatted), out.c_str(), &t) > 0) {
    res = String(formatted);
  }

  // Convert 2-byte UTF-8 degree symbol and {DEG} tag to font char 127
  res.replace("\xC2\xB0", "\x7F");
  res.replace("°", "\x7F");
  res.replace("{DEG}", "\x7F");

  return res;
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

inline void setVUMatrixPoint(MD_MAX72XX *mx, int r, int c, bool state) {
  if (c >= 0 && c < MAX_DEVICES * 8 && r >= 0 && r < 8) {
    int physC = (MAX_DEVICES * 8 - 1) - c;
    mx->setPoint(7 - r, physC, state);
  }
}

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
  static unsigned long lastVUDraw = 0;
  if (millis() - lastVUDraw < 15)
    return;
  lastVUDraw = millis();

  MD_MAX72XX *mx = P.getGraphicObject();
  if (!mx)
    return;

  int zStart = constrain(musicSync.startCol, 0, (MAX_DEVICES * 8) - 1);
  int zEnd = constrain(musicSync.endCol, zStart, (MAX_DEVICES * 8) - 1);
  int zWidth = zEnd - zStart + 1;

  int32_t i2sRawBuffer[FFT_SAMPLES];
  size_t bytesRead = 0;

  while (i2s_read(I2S_PORT, i2sRawBuffer, sizeof(i2sRawBuffer), &bytesRead, 0) == ESP_OK && bytesRead > 0) {
  }
  if (bytesRead == 0) {
    i2s_read(I2S_PORT, i2sRawBuffer, sizeof(i2sRawBuffer), &bytesRead, 10 / portTICK_PERIOD_MS);
  }
  if (bytesRead == 0)
    return;

  int64_t sum = 0;
  int sampleCount = bytesRead / sizeof(int32_t);

  for (int i = 0; i < sampleCount; i++) {
    int32_t sample = i2sRawBuffer[i] >> 14;
    sum += sample;
  }
  int32_t dcOffset = sum / sampleCount;

  float sumSquares = 0.0f;
  for (int i = 0; i < sampleCount; i++) {
    int32_t acSample = (i2sRawBuffer[i] >> 14) - dcOffset;
    vReal[i] = (double)acSample;
    vImag[i] = 0.0;
    sumSquares += (float)acSample * acSample;
  }

  float rms = sqrtf(sumSquares / sampleCount);

  const float noiseFloor = 30.0f;
  if (rms < noiseFloor)
    rms = 0.0f;
  else
    rms -= noiseFloor;

  float gain = (float)musicSync.sensitivity / 50.0f;
  float rawVol = (rms * gain) / 2800.0f;
  rawVol = constrain(rawVol, 0.0f, 1.0f);
  rawVol = powf(rawVol, 0.85f);

  if (rawVol > smoothVol) {
    smoothVol = rawVol;
  } else {
    smoothVol = (smoothVol * 0.45f) + (rawVol * 0.55f);
  }

  unsigned long decayInterval = 60;
  if (strcmp(musicSync.peakDecay, "Fast") == 0)
    decayInterval = 30;
  else if (strcmp(musicSync.peakDecay, "Smooth") == 0)
    decayInterval = 120;

  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  clearVUZone(mx, zStart, zEnd);

  if (strcmp(musicSync.animation, "VU Bar") == 0) {
    float level = constrain(smoothVol, 0.0f, 1.0f);
    int fillCols = (int)round(level * zWidth);
    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      bool on = (i < fillCols);
      for (int r = 0; r < 8; r++)
        setVUMatrixPoint(mx, r, c, on);
    }
  } else if (strcmp(musicSync.animation, "VU Peak") == 0) {
    float level = constrain(smoothVol, 0.0f, 1.0f);
    int fillCols = constrain((int)round(level * zWidth), 0, zWidth);

    if (fillCols > peakPos) {
      peakPos = fillCols;
      lastPeakDropTime = millis();
    } else if (millis() - lastPeakDropTime >= decayInterval) {
      if (peakPos > 0.0f)
        peakPos -= 1.0f;
      lastPeakDropTime = millis();
    }
    peakPos = constrain(peakPos, 0.0f, (float)zWidth);

    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;
      bool on = (i < fillCols);
      for (int r = 0; r < 8; r++)
        setVUMatrixPoint(mx, r, c, on);
    }

    if (peakPos > 0.0f) {
      int peakCol = (int)peakPos - 1;
      if (peakCol >= 0 && peakCol < zWidth) {
        int c = zStart + peakCol;
        for (int r = 0; r < 8; r++)
          setVUMatrixPoint(mx, r, c, true);
      }
    }
  } else if (strcmp(musicSync.animation, "VU Mirror") == 0) {
    float level = constrain(smoothVol, 0.0f, 1.0f);
    float maxHalfWidth = zWidth / 2.0f;
    float halfSpan = level * maxHalfWidth;
    float center = ((float)zWidth - 1.0f) / 2.0f;

    for (int i = 0; i < zWidth; i++) {
      float distance = fabsf((float)i - center);
      bool on = (distance <= halfSpan);
      int c = zStart + i;
      for (int r = 0; r < 8; r++)
        setVUMatrixPoint(mx, r, c, on);
    }
  } else if (strcmp(musicSync.animation, "VU Bounce") == 0) {
    // 1. Calculate target based on music volume
    float targetPos = smoothVol * (float)(zWidth - 2);
    float diff = targetPos - bouncePos;

    // 2. Beat-driven spring physics:
    // Strong punch forward on volume spikes; elastic spring pulling back on drops
    if (diff > 0.0f) {
      bounceVel += (diff * 0.32f) + (smoothVol * 0.45f);
    } else {
      bounceVel += diff * 0.16f;
    }

    // Natural fluid damping to create elastic musical bounce
    bounceVel *= 0.84f;
    bouncePos += bounceVel;

    // 3. Elastic wall bounce on right edge
    float maxPos = (float)(zWidth - 2);
    if (bouncePos > maxPos) {
      bouncePos = maxPos;
      bounceVel = -fabsf(bounceVel) * 0.5f; // Rebound off the right wall
    }

    // 4. Elastic wall bounce on left edge
    if (bouncePos < 0.0f) {
      bouncePos = 0.0f;
      bounceVel = fabsf(bounceVel) * 0.35f; // Soft rebound off the left wall
    }

    // 5. Render the original clean 2-column block (rows 2 to 5)
    int bCol = zStart + (int)round(bouncePos);
    for (int r = 2; r < 6; r++) {
      setVUMatrixPoint(mx, r, bCol, true);
      if (bCol + 1 <= zEnd)
        setVUMatrixPoint(mx, r, bCol + 1, true);
    }
  } else if (strcmp(musicSync.animation, "VU Pulse") == 0) {
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
  } else if (strcmp(musicSync.animation, "VU Wave") == 0) {
    // 1. Shift history buffer towards the right (Left-to-Right wave propagation)
    for (int i = zWidth - 1; i > 0; i--) {
      waveHistory[i] = waveHistory[i - 1];
    }

    // 2. Inject new energy at the left edge (index 0)
    float instantEnergy = (rawVol * 0.75f) + (smoothVol * 0.25f);
    waveHistory[0] = instantEnergy;

    // 3. Phase advancing forward in time
    static float wavePhase = 0.0f;
    wavePhase += 0.20f + (smoothVol * 0.35f);

    float gain = (float)musicSync.sensitivity / 50.0f;

    // 4. Render solid acoustic wave ribbon from peak to center axis
    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;

      // Amplitude scaling with gain
      float v = constrain(waveHistory[i] * gain * 1.6f, 0.0f, 1.0f);
      float amp = powf(v, 0.65f) * 3.8f;

      // Harmonic ripple formula traveling left-to-right
      float angle = ((float)i * 0.42f) - wavePhase;
      float ripple = (sinf(angle) + 0.35f * sinf(angle * 2.0f + 0.5f)) / 1.35f;

      int topY = 4;
      int botY = 3;

      if (amp >= 0.35f) {
        int h = constrain((int)round(fabsf(ripple) * amp), 0, 3);
        topY = constrain(4 + h, 4, 7);
        botY = constrain(3 - h, 0, 3);
      }

      // Fill continuously from botY up to topY (solid fill through the center axis)
      for (int r = botY; r <= topY; r++) {
        setVUMatrixPoint(mx, r, c, true);
      }
    }
  } else if (strcmp(musicSync.animation, "VU Spectrum") == 0 ||
             strcmp(musicSync.animation, "VU Spectrum Bar") == 0 ||
             strcmp(musicSync.animation, "VU Spectrum Peak") == 0) {

    bool showBars = (strcmp(musicSync.animation, "VU Spectrum Peak") != 0);
    bool showPeaks = (strcmp(musicSync.animation, "VU Spectrum Bar") != 0);
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    if (millis() - lastBandDropTime >= decayInterval) {
      for (int i = 0; i < zWidth; i++) {
        if (bandPeaks[i] > 0.0f)
          bandPeaks[i] -= 0.5f;
      }
      lastBandDropTime = millis();
    }

    const int startBin = 1;
    const int maxBin = 28;
    float gain = (float)musicSync.sensitivity / 50.0f;

    for (int i = 0; i < zWidth; i++) {
      int c = zStart + i;

      // Logarithmic distribution across columns
      float logRatio = powf((float)i / (float)(zWidth > 1 ? zWidth - 1 : 1), 1.35f);
      float continuousBin = startBin + logRatio * (maxBin - startBin);
      int bFloor = constrain((int)continuousBin, startBin, maxBin - 1);
      float bFrac = continuousBin - bFloor;

      // Interpolate between adjacent frequency bins
      double rawMag = (vReal[bFloor] * (1.0f - bFrac)) + (vReal[bFloor + 1] * bFrac);

      // 1. Subtract room ambient noise floor so silence stays at 0
      rawMag -= 500.0;
      if (rawMag < 0.0)
        rawMag = 0.0;

      // 2. Balanced treble compensation (gentle 1.0x to 2.8x curve)
      float eqBoost = 1.0f + ((float)i / (float)zWidth) * 1.8f;

      // 3. Sensitivity gain application
      double mag = rawMag * gain * eqBoost;

      // 4. Properly scaled height calculation (0 to 8)
      int height = 0;
      if (mag > 0.0) {
        float norm = (float)(mag / 18000.0);
        if (norm > 1.0f)
          norm = 1.0f;
        height = (int)round(powf(norm, 0.65f) * 8.0f);
      }
      height = constrain(height, 0, 8);

      // Update peak hold dot
      if (height > bandPeaks[i])
        bandPeaks[i] = (float)height;

      // Draw equalizer column bar (if not Peak-only)
      if (showBars) {
        for (int r = 0; r < height; r++)
          setVUMatrixPoint(mx, r, c, true);
      }

      // Draw floating peak dot (if not Bar-only)
      if (showPeaks) {
        int peakRow = (int)bandPeaks[i] - 1;
        if (peakRow >= 0 && peakRow < 8 && (!showBars || peakRow >= height)) {
          setVUMatrixPoint(mx, peakRow, c, true);
        }
      }
    }
  }

  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

// ==========================================
// SCENE PLAYLIST & TRANSITION MANAGEMENT
// ==========================================
void launchSceneOnDisplay(uint8_t z, uint8_t sIdx) {
  SceneConfig &sc = scenes[sIdx];

  if (sc.isBold) {
    P.setFont(z, customBoldFont);
  } else {
    P.setFont(z, customThinFont);
  }

  P.setIntensity(z, sc.brightness);

  String resolved = sc.isCustom ? processTemplate(sc.rawMessage) : String(sc.rawMessage);
  resolved.replace("\xC2\xB0", "\x7F");
  resolved.replace("°", "\x7F");
  resolved.replace("{DEG}", "\x7F");
  strncpy(sc.activeMessage, resolved.c_str(), sizeof(sc.activeMessage) - 1);
  sc.activeMessage[sizeof(sc.activeMessage) - 1] = '\0';

  P.displayZoneText(
      z,
      sc.activeMessage,
      sc.align,
      sc.speed,
      sc.pause,
      sc.inEffect,
      sc.outEffect);
  P.displayReset(z);
}

void startZoneScene(uint8_t z, uint8_t listIdx) {
  if (z >= activeZoneCount || zones[z].sceneCount == 0)
    return;

  // Check if current playlist sequence has finished all its scenes
  if (listIdx >= zones[z].sceneCount) {
    if (zones[z].playlistRepeat == -1) {
      // Infinite playlist loop: Rewind immediately to scene 0!
      Serial.printf("[Playlist] Zone %u ('%s'): Sequence completed. Restarting infinite loop...\n", z, zones[z].name);
      listIdx = 0;
    } else {
      zones[z].playlistLoopCounter++;
      if (zones[z].playlistLoopCounter < zones[z].playlistRepeat) {
        // Run next playlist iteration
        Serial.printf("[Playlist] Zone %u: Sequence iteration %d of %d starting...\n",
                      z, zones[z].playlistLoopCounter + 1, zones[z].playlistRepeat);
        listIdx = 0;
      } else {
        // All playlist cycles completed: cleanly stop the zone
        zones[z].state = ZSTATE_FINISHED;
        P.displayClear(z);
        Serial.printf("[Playlist] Zone %u ('%s'): All playlist cycles finished. Zone stopped.\n", z, zones[z].name);
        return;
      }
    }
  }

  zones[z].currentSceneIdx = listIdx;
  zones[z].loopCounter = 0;

  uint8_t sIdx = zones[z].sceneList[listIdx];
  SceneConfig &sc = scenes[sIdx];

  if (sc.startDelay > 0) {
    zones[z].state = ZSTATE_START_DELAY;
    zones[z].stateStartTime = millis();
    P.displayClear(z);
    Serial.printf("[Playlist] Zone %u: Scene %u starting with %u ms start delay...\n", z, listIdx, sc.startDelay);
  } else {
    zones[z].state = ZSTATE_PLAYING;
    launchSceneOnDisplay(z, sIdx);
    Serial.printf("[Playlist] Zone %u: Playing scene %u ('%s') [Repeat: %d]\n", z, listIdx, sc.rawMessage, sc.repeat);
  }
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
  matrix["hardware_type"] = "FC16_HW";

  JsonObject ms = doc["music_sync"].to<JsonObject>();
  ms["enabled"] = false;
  ms["zone"] = "Zone 1";
  ms["start_col"] = 0;
  ms["end_col"] = 39;
  ms["animation"] = "VU Bar";
  ms["sensitivity"] = 60;
  ms["peak_decay"] = "Medium";
  ms["brightness"] = 12;

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
  d1["repeat"] = 3;

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
  a2["outEffect"] = "PA_NO_EFFECT";
  a2["speedMs"] = 0;
  a2["startDelayMs"] = 0;
  a2["endDelayMs"] = 0;
  JsonObject d2 = sc2["display"].to<JsonObject>();
  d2["brightness"] = 12;
  d2["repeat"] = -1;

  serializeJson(doc, file);
  file.close();
  Serial.println("[Config] Fresh default /config.json created in SPIFFS.");
}

bool matchScene(uint8_t sIdx, const char *target) {
  if (!target)
    return false;
  // Matches either the Scene Name ("Scene 1") OR the text message ("Basanti Studio")
  if (strlen(scenes[sIdx].name) > 0 && strcmp(scenes[sIdx].name, target) == 0)
    return true;
  if (strlen(scenes[sIdx].rawMessage) > 0 && strcmp(scenes[sIdx].rawMessage, target) == 0)
    return true;
  return false;
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
  DynamicJsonDocument doc(24576);
#endif

  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("[Config] JSON Deserialization error: %s\n", err.c_str());
    return;
  }

  if (doc["matrix"].is<JsonObject>()) {
    uint8_t newMax = doc["matrix"]["modules"] | MAX_DEVICES;
    const char *newHw = doc["matrix"]["hardware_type"] | "FC16_HW";

    // Reboot to re-initialize the matrix if modules or hardware type changed
    if (newMax != MAX_DEVICES || strcmp(newHw, hardwareTypeStr) != 0) {
      Serial.println("[Config] Matrix hardware/size changed! Rebooting ESP memory to apply safely...");
      delay(500);
      ESP.restart();
    }
  }

  uint8_t baseBrightness = 12;
  if (doc["scenes"].is<JsonArray>() && doc["scenes"].size() > 0) {
    baseBrightness = doc["scenes"][0]["display"]["brightness"] | 12;
  }
  MD_MAX72XX *mx = P.getGraphicObject();
  if (mx) {
    mx->control(MD_MAX72XX::INTENSITY, baseBrightness);
  }

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
    musicSync.brightness = doc["music_sync"]["brightness"] | 12;
  } else {
    musicSync.enabled = false;
  }

  if (musicSync.enabled) {
    isMusicSyncActive = true;
    P.displayClear();
    if (mx)
      mx->control(MD_MAX72XX::INTENSITY, musicSync.brightness);
    Serial.println("========================================");
    Serial.println("[TASK SWITCH] MUSIC SYNC IS ACTIVE!");
    Serial.printf(" -> Dedicated Mode : ESP runs exclusively as VU Meter\n");
    Serial.printf(" -> Zone Mapped    : '%s' [Cols %d -> %d]\n", musicSync.zone, musicSync.startCol, musicSync.endCol);
    Serial.printf(" -> Animation      : %s | Gain: %d%% | Decay: %s | Brightness: %u\n", musicSync.animation, musicSync.sensitivity, musicSync.peakDecay, musicSync.brightness);
    Serial.println("========================================");
    return;
  }

  isMusicSyncActive = false;

  JsonArray scenesArr = doc["scenes"].as<JsonArray>();
  if (scenesArr.isNull() || scenesArr.size() == 0) {
    Serial.println("[Config] 'scenes' array empty or invalid.");
    return;
  }

  // =========================================================================
  // GROUP SCENES INTO UNIQUE PHYSICAL PAROLA ZONES
  // =========================================================================
  totalScenes = 0;
  activeZoneCount = 0;

  for (uint8_t z = 0; z < MAX_ZONES; z++) {
    zones[z].inUse = false;
    zones[z].sceneCount = 0;
    zones[z].currentSceneIdx = 0;
    zones[z].loopCounter = 0;
    zones[z].state = ZSTATE_PLAYING;
  }

  int numScenes = min((int)scenesArr.size(), (int)MAX_SCENES);

  for (int s = 0; s < numScenes; s++) {
    JsonObject sc = scenesArr[s];
    SceneConfig &curScene = scenes[totalScenes];

    const char *sName = sc["sceneName"] | "";
    strncpy(curScene.name, sName, sizeof(curScene.name) - 1);
    curScene.name[sizeof(curScene.name) - 1] = '\0';

    const char *zName = "Zone 1";
    if (sc["zone"].is<JsonObject>()) {
      zName = sc["zone"]["name"] | sc["sceneName"] | "Zone 1";
    }
    strncpy(curScene.zoneName, zName, sizeof(curScene.zoneName) - 1);
    curScene.zoneName[sizeof(curScene.zoneName) - 1] = '\0';

    curScene.startCol = 0;
    if (sc["zone"].is<JsonObject>()) {
      if (sc["zone"]["startCol"].is<int>())
        curScene.startCol = sc["zone"]["startCol"].as<int>();
      else if (sc["zone"]["start"].is<int>())
        curScene.startCol = sc["zone"]["start"].as<int>();
    }

    curScene.endCol = (MAX_DEVICES * 8) - 1;
    if (sc["zone"].is<JsonObject>()) {
      if (sc["zone"]["endCol"].is<int>())
        curScene.endCol = sc["zone"]["endCol"].as<int>();
      else if (sc["zone"]["end"].is<int>())
        curScene.endCol = sc["zone"]["end"].as<int>();
    }

    int webStartDev = curScene.startCol / 8;
    int webEndDev = curScene.endCol / 8;
    int physStartDev = (MAX_DEVICES - 1) - webEndDev;
    int physEndDev = (MAX_DEVICES - 1) - webStartDev;
    uint8_t sDev = constrain(physStartDev, 0, MAX_DEVICES - 1);
    uint8_t eDev = constrain(physEndDev, sDev, MAX_DEVICES - 1);

    const char *mType = sc["message"]["type"] | "plain";
    curScene.isCustom = (strcmp(mType, "custom") == 0);

    const char *mContent = sc["message"]["content"] | "ESP";
    strncpy(curScene.rawMessage, mContent, sizeof(curScene.rawMessage) - 1);
    curScene.rawMessage[sizeof(curScene.rawMessage) - 1] = '\0';

    curScene.isBold = sc["message"]["bold"] | false;
    curScene.align = parseAlign(sc["message"]["align"] | "center");
    curScene.inEffect = parseEffect(sc["animation"]["inEffect"] | "PA_SCROLL_LEFT");
    curScene.outEffect = parseEffect(sc["animation"]["outEffect"] | "PA_SCROLL_LEFT");

    curScene.speed = sc["animation"]["speedMs"] | 35;
    curScene.startDelay = sc["animation"]["startDelayMs"] | 0;
    curScene.pause = sc["animation"]["endDelayMs"] | 0;

    curScene.brightness = sc["display"]["brightness"] | 12;
    curScene.repeat = sc["display"]["repeat"] | -1;

    // Static print adjustments: speed must be 0, outEffect must be PA_NO_EFFECT
    bool isStatic = (curScene.inEffect == PA_PRINT &&
                     (curScene.outEffect == PA_NO_EFFECT || curScene.outEffect == PA_PRINT));
    if (isStatic) {
      curScene.outEffect = PA_NO_EFFECT;
      curScene.speed = 0;
      if (curScene.repeat != -1 && curScene.pause == 0) {
        curScene.pause = 1000; // Default finite static hold so it doesn't vanish in 0ms
      }
    }

    // Match with existing hardware zone or create a new one
    int targetZone = -1;
    for (uint8_t z = 0; z < activeZoneCount; z++) {
      if (strcmp(zones[z].name, curScene.zoneName) == 0 ||
          (zones[z].startDev == sDev && zones[z].endDev == eDev)) {
        targetZone = z;
        break;
      }
    }

    if (targetZone == -1) {
      if (activeZoneCount < MAX_ZONES) {
        targetZone = activeZoneCount;
        zones[targetZone].inUse = true;
        strncpy(zones[targetZone].name, curScene.zoneName, sizeof(zones[targetZone].name) - 1);
        zones[targetZone].startDev = sDev;
        zones[targetZone].endDev = eDev;
        zones[targetZone].sceneCount = 0;
        zones[targetZone].currentSceneIdx = 0;
        zones[targetZone].loopCounter = 0;
        activeZoneCount++;
      } else {
        Serial.printf("[Config] Max zones (%d) reached! Skipping scene %d\n", MAX_ZONES, s);
        continue;
      }
    }

    if (zones[targetZone].sceneCount < MAX_SCENES) {
      zones[targetZone].sceneList[zones[targetZone].sceneCount++] = totalScenes;
    }

    totalScenes++;
  }

  // =========================================================================
  // OPTIONAL PLAYLIST PARSING & OVERRIDE
  // =========================================================================
  if (doc["playlists"].is<JsonArray>()) {
    JsonArray plArr = doc["playlists"].as<JsonArray>();
    for (JsonObject pl : plArr) {
      const char *plZone = pl["zone"] | "";
      int plRepeat = pl["repeat"] | -1;

      for (uint8_t z = 0; z < activeZoneCount; z++) {
        if (strcmp(zones[z].name, plZone) == 0) {
          JsonArray plScenes = pl["scenes"].as<JsonArray>();
          if (plScenes.size() > 0) {
            uint8_t tempSceneList[MAX_SCENES];
            uint8_t matchedCount = 0;

            for (JsonVariant sVar : plScenes) {
              const char *targetName = sVar.as<const char *>();
              for (uint8_t s = 0; s < totalScenes; s++) {
                if (matchScene(s, targetName) && matchedCount < MAX_SCENES) {
                  tempSceneList[matchedCount++] = s;
                  break;
                }
              }
            }

            // Only overwrite zone queue if scenes were successfully matched
            if (matchedCount > 0) {
              zones[z].sceneCount = matchedCount;
              zones[z].playlistRepeat = plRepeat;
              zones[z].playlistLoopCounter = 0;
              for (uint8_t i = 0; i < matchedCount; i++) {
                zones[z].sceneList[i] = tempSceneList[i];
              }
              Serial.printf("[Config] Applied Playlist '%s' to Zone %u (%u scenes, repeat: %d)\n",
                            pl["name"] | "PL", z, zones[z].sceneCount, plRepeat);
            }
          }
          break;
        }
      }
    }
  }

  // =========================================================================
  // INITIALIZE HARDWARE ZONES & START PLAYLISTS
  // =========================================================================
  P.displayClear();
  Serial.println("========================================");
  Serial.printf("[Config] Total Scenes: %d | Formed %d Unique Physical Zones:\n", totalScenes, activeZoneCount);

  for (uint8_t z = 0; z < activeZoneCount; z++) {
    P.setZone(z, zones[z].startDev, zones[z].endDev);
    startZoneScene(z, 0); // Start scene 0 for this zone

    Serial.printf("  -> Zone %u ('%s'): Modules [%u..%u] | Queued %u Scenes\n",
                  z, zones[z].name, zones[z].startDev, zones[z].endDev, zones[z].sceneCount);
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
      WiFi.setSleep(false);
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
    WiFi.setSleep(false);
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
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(mdnsHostname, sizeof(mdnsHostname), "ledstudio-%02X%02X", mac[4], mac[5]);

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
  auto handleIndex = [](AsyncWebServerRequest *request) {
    if (request->hasHeader("If-None-Match")) {
      const AsyncWebHeader *h = request->getHeader("If-None-Match");
      if (h && h->value() == BUILD_ETAG) {
        request->send(304);
        return;
      }
    }

    bool clientAcceptsGzip = request->hasHeader("Accept-Encoding") &&
                             request->getHeader("Accept-Encoding")->value().indexOf("gzip") >= 0;

    String filePath = "/index.html";
    bool isGzip = false;

    if (clientAcceptsGzip && SPIFFS.exists("/index.html.gz")) {
      filePath = "/index.html.gz";
      isGzip = true;
    } else if (!SPIFFS.exists("/index.html")) {
      request->send(404, "text/plain", "index.html missing from SPIFFS!");
      return;
    }

    AsyncWebServerResponse *res = request->beginResponse(SPIFFS, filePath, "text/html");
    if (isGzip) {
      res->addHeader("Content-Encoding", "gzip");
    }
    res->addHeader("ETag", BUILD_ETAG);
    res->addHeader("Cache-Control", "public, max-age=604800, must-revalidate");
    request->send(res);
  };

  server.on("/", WebRequestMethod::HTTP_GET, handleIndex);
  server.on("/index.html", WebRequestMethod::HTTP_GET, handleIndex);
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
        if (uploadFile)
          uploadFile.write(data, len);
        if (index + len >= total) {
          if (uploadFile) {
            uploadFile.close();
            Serial.printf("[SPIFFS] Streamed %u bytes to %s\n", total, CONFIG_FILE);
          }
          configUpdated = true;
        }
      });

  // ==========================================
  // SYSTEM HEALTH & TELEMETRY REST ENDPOINT
  // ==========================================
  server.on("/api/system/status", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(512);
#endif

    // 1. CPU Frequency & Core Temperature
    doc["cpu_mhz"] = ESP.getCpuFreqMHz();
    doc["cpu_temp"] = round(temperatureRead() * 10.0) / 10.0; // Built-in internal sensor (°C)

    // 2. RAM (Heap) in Bytes
    uint32_t totalRam = ESP.getHeapSize();
    uint32_t freeRam = ESP.getFreeHeap();
    doc["ram_total"] = totalRam;
    doc["ram_used"] = totalRam - freeRam;

    // 3. SPIFFS Storage in Bytes
    doc["spiffs_total"] = SPIFFS.totalBytes();
    doc["spiffs_used"] = SPIFFS.usedBytes();

    // 4. WiFi Link Quality
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    doc["wifi_ssid"] = wifiConnected ? WiFi.SSID() : "Disconnected";
    doc["wifi_rssi"] = wifiConnected ? WiFi.RSSI() : 0;

    String res;
    serializeJson(doc, res);
    request->send(200, "application/json", res);
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

// Helper to scroll a message across the full display and wait until finished
void scrollStartupText(const char *msg, uint16_t speed = 30) {
  static char textBuf[64];
  strncpy(textBuf, msg, sizeof(textBuf) - 1);
  textBuf[sizeof(textBuf) - 1] = '\0';

  P.setZone(0, 0, MAX_DEVICES - 1);
  P.setFont(0, customThinFont);
  // PA_SCROLL_LEFT scrolls text into the matrix from the right and exits to the left
  P.displayZoneText(0, textBuf, PA_LEFT, speed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  P.displayReset(0);

  while (!P.getZoneStatus(0)) {
    P.displayAnimate();
    delay(10);
  }
  P.displayClear(0);
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

  buildCustomBoldFont();

  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS mount failed!");
  } else {
    Serial.println("[FS] SPIFFS mounted successfully.");
  }

  Wire.begin(21, 22);
  if (aht.begin()) {
    Serial.println("[Sensor] AHT10 found & initialized.");
    ahtFound = true;
  } else {
    Serial.println("[Sensor] AHT10 not found. Defaulting to virtual readings.");
  }

  if (SPIFFS.exists(CONFIG_FILE)) {
    File file = SPIFFS.open(CONFIG_FILE, "r");
    if (file) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
      JsonDocument tempDoc;
#else
      DynamicJsonDocument tempDoc(1024);
#endif
      if (!deserializeJson(tempDoc, file)) {
        if (tempDoc["matrix"]["modules"]) {
          MAX_DEVICES = tempDoc["matrix"]["modules"].as<uint8_t>();
        }
        const char *hw = tempDoc["matrix"]["hardware_type"] | "FC16_HW";
        strncpy(hardwareTypeStr, hw, sizeof(hardwareTypeStr) - 1);
        HARDWARE_TYPE = parseHardwareType(hardwareTypeStr);
      }
      file.close();
    }
  }

  P_ptr = new MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
  P.begin(MAX_ZONES);
  P.setFont(customThinFont);
  P.setIntensity(12);
  P.displayClear();

  initI2S();

  if (connectToSavedWiFi()) {
    initMDNS();

    // 1. Scroll IP Address (Right-to-Left)
    String ipMsg = "IP: " + WiFi.localIP().toString();
    scrollStartupText(ipMsg.c_str());

    // 2. Scroll mDNS Address (Right-to-Left)
    String mdnsMsg = String(mdnsHostname) + ".local";
    scrollStartupText(mdnsMsg.c_str());
  }

  initNTP();
  setupWebServer();
  loadConfiguration();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // 1. Live config update from browser
  if (configUpdated) {
    configUpdated = false;
    loadConfiguration();
  }

  // 2. Music Sync or Multi-Zone Scene Execution
  if (isMusicSyncActive) {
    runMusicSyncFrame();
  } else {
    P.displayAnimate();

    // Check dynamic templates (like clock seconds) at 20 Hz (every 50 ms)
    static unsigned long lastCustomPoll = 0;
    bool pollCustomTemplates = (millis() - lastCustomPoll >= 50);
    if (pollCustomTemplates) {
      lastCustomPoll = millis();
    }

    for (uint8_t z = 0; z < activeZoneCount; z++) {
      if (!zones[z].inUse || zones[z].state == ZSTATE_FINISHED)
        continue;

      // Handle non-blocking start delay for the current scene
      if (zones[z].state == ZSTATE_START_DELAY) {
        uint8_t sIdx = zones[z].sceneList[zones[z].currentSceneIdx];
        if (millis() - zones[z].stateStartTime >= scenes[sIdx].startDelay) {
          zones[z].state = ZSTATE_PLAYING;
          launchSceneOnDisplay(z, sIdx);
        }
        continue;
      }

      uint8_t sIdx = zones[z].sceneList[zones[z].currentSceneIdx];
      SceneConfig &sc = scenes[sIdx];

      bool isStatic = (sc.inEffect == PA_PRINT &&
                       (sc.outEffect == PA_NO_EFFECT || sc.outEffect == PA_PRINT));

      if (isStatic) {
        // =====================================================================
        // STATIC PRINT SCENE
        // =====================================================================
        if (sc.repeat == -1) {
          // INFINITE LOOP STATIC: Stays forever, never advances
          if (sc.isCustom && pollCustomTemplates) {
            String resolved = processTemplate(sc.rawMessage);
            if (resolved != sc.activeMessage) {
              strncpy(sc.activeMessage, resolved.c_str(), sizeof(sc.activeMessage) - 1);
              sc.activeMessage[sizeof(sc.activeMessage) - 1] = '\0';
              P.displayZoneText(z, sc.activeMessage, sc.align, 0, 0, PA_PRINT, PA_NO_EFFECT);
              P.displayReset(z);
            }
          }
        } else {
          // FINITE ITERATION STATIC: Holds for pause (endDelayMs) per iteration
          if (P.getZoneStatus(z)) {
            zones[z].loopCounter++;
            if (zones[z].loopCounter >= sc.repeat) {
              // Iterations done! Advance to next scene in playlist
              startZoneScene(z, zones[z].currentSceneIdx + 1);
            } else {
              // Reset for next hold iteration
              if (sc.isCustom) {
                String resolved = processTemplate(sc.rawMessage);
                strncpy(sc.activeMessage, resolved.c_str(), sizeof(sc.activeMessage) - 1);
                sc.activeMessage[sizeof(sc.activeMessage) - 1] = '\0';
              }
              P.displayReset(z);
            }
          }
        }
      } else {
        // =====================================================================
        // ANIMATED SCENE (Scroll, Wipe, Fade, etc.)
        // =====================================================================
        if (P.getZoneStatus(z)) {
          if (sc.repeat == -1) {
            // INFINITE LOOP ANIMATION: Never advances to next scene
            if (sc.isCustom) {
              String resolved = processTemplate(sc.rawMessage);
              strncpy(sc.activeMessage, resolved.c_str(), sizeof(sc.activeMessage) - 1);
              sc.activeMessage[sizeof(sc.activeMessage) - 1] = '\0';
            }
            P.displayReset(z);
          } else {
            // FINITE LOOP ANIMATION (e.g. repeat = 3)
            zones[z].loopCounter++;
            if (zones[z].loopCounter >= sc.repeat) {
              // Finished 3 iterations! Advance to next scene in playlist
              startZoneScene(z, zones[z].currentSceneIdx + 1);
            } else {
              // Continue looping this scene
              if (sc.isCustom) {
                String resolved = processTemplate(sc.rawMessage);
                strncpy(sc.activeMessage, resolved.c_str(), sizeof(sc.activeMessage) - 1);
                sc.activeMessage[sizeof(sc.activeMessage) - 1] = '\0';
              }
              P.displayReset(z);
            }
          }
        }
      }
    }
  }

  // 3. Non-blocking background network supervisor
  checkWiFiAndStartServer();
}