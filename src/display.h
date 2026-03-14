#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>
#include "weather_api.h"
#include "gps_config.h"

// Palette
#define PALETTE_BG      0xF7BE  // very light blue-grey background
#define HEADER_BG       0x039F  // teal-ish
#define HEADER_SEP      0x01F3  // slightly darker teal band
#define PALETTE_CARD    TFT_WHITE
#define PALETTE_BORDER  0xD69A  // light border
#define PALETTE_TEXT    0x18E3  // near-black, softer than pure black
#define PALETTE_MUTE    0x6B4D  // muted grey
#define PALETTE_ACCENT  0xFD20  // warm accent (orange) for highlights

// Expose the tft instance and shared state to display functions
extern TFT_eSPI tft;
extern int centerX;
extern int centerY;
extern char lastClockStr[6];
extern CurrentConditions currentWeather;
extern DailyForecast dailyForecast;
extern bool everFetchedSuccessfully;

// Screen functions
void showSplashScreen();
void displayWeather();
void showErrorScreen();

// App bar
void drawClockInAppBar();
void updateBlinkIndicator(bool blinkState);

// Helpers
String windDirShort(int deg);
void drawSunIcon(int x, int y, int r, uint16_t color, uint16_t bg);
void drawMoonIcon(int x, int y, int r, uint16_t color, uint16_t bg);

void renderScreen(int screen);
void displayForecastPage(int page); // stub

void drawHeader(bool showDayNightIcon = false);
void drawTempArrow(int x, int y, bool up, uint16_t bg);

#endif