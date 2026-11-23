// Pins, timings and defaults. Wiring: README.md "Bağlantı / Wiring".
#pragma once

#define FW_NAME "APRS World Radar"
#ifndef FW_VERSION
#define FW_VERSION "0.1.0"
#endif

// GMT130 1.3" 240x240 ST7789 (no CS pin) on an ESP32 DevKit V1 (WROOM-32). RST/DC avoid GPIO16/17: many boards
// label them RX2/TX2 or leave them out, and WROVER modules use them for PSRAM.
#define PIN_TFT_SCLK 18
#define PIN_TFT_MOSI 23
#define PIN_TFT_RST 4
#define PIN_TFT_DC 21
#define PIN_TFT_BL 22

// GPS module (NEO-6M / NEO-M8N): GPS TX -> PIN_GPS_RX, GPS RX -> PIN_GPS_TX.
#define PIN_GPS_RX 32
#define PIN_GPS_TX 33
#define GPS_BAUD 9600

// Buttons to GND (internal pull-ups). GPIO0 is the board's own BOOT button.
#define PIN_BUTTON 27
#define PIN_BOOT 0
// Passive or active buzzer, + to the pin.
#define PIN_BUZZER 25

#define SCREEN_W 240
#define SCREEN_H 240
// A full 240x240 RGB565 frame (115 200 B) is larger than the biggest free heap block of a WROOM-32, so a frame is
// drawn in strips of this height and each strip is pushed on its own.
#define STRIP_H 120

#define API_HOST "api.aprs.world"
#define ADSB_HOST "api.adsb.lol"
#define WEB_HOST "aprs.world"

#define MAX_STATIONS 48
#define MAX_AIRCRAFT 32

#define STATIONS_POLL_MS 30000UL
#define AIRCRAFT_POLL_MS 10000UL
#define FOLLOW_POLL_MS 30000UL
// A followed station's position older than this is not used as "my position".
#define FOLLOW_MAX_AGE_S 1800
