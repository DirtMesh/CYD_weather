#include "display.h"
#include <math.h>
#include <time.h>

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2

// Small helper to redraw just the clock in the app bar
void drawClockInAppBar() {
  char timeBuf[6] = "--:--";

  time_t now = time(nullptr);
  struct tm t;
  bool haveTime = (now > 24 * 3600) && localtime_r(&now, &t);
  if (haveTime) {
    strftime(timeBuf, sizeof(timeBuf), "%H:%M", &t);
  }

  if (strcmp(timeBuf, lastClockStr) == 0) return;
  strcpy(lastClockStr, timeBuf);

  // Clear just the clock region
  int clockW = 60;
  int clockH = 18;
  int clockBuff = 30;
  int clockX = tft.width() - clockBuff - clockW;
  int clockY = 5;
  tft.fillRect(clockX, clockY, clockW, clockH, HEADER_BG);

  // Draw the new time
  tft.setTextColor(TFT_WHITE, HEADER_BG);
  int textX = tft.width() - clockBuff - tft.textWidth(timeBuf, 2);
  tft.drawString(timeBuf, textX, 6, 2);
}

String windDirShort(int deg) {
  // Normalize 0..359
  while (deg < 0) deg += 360;
  deg %= 360;

  // 8-point compass
  const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
  int idx = (deg + 22) / 45;
  idx = idx % 8;
  return String(dirs[idx]);
}

void drawSunIcon(int x, int y, int r, uint16_t color, uint16_t bg) {
  // Simple sun: circle + 8 rays
  tft.fillCircle(x, y, r, color);
  for (int i = 0; i < 8; i++) {
    float a = (3.1415926f * 2.0f * i) / 8.0f;
    int x1 = x + (int)((r + 2) * cos(a));
    int y1 = y + (int)((r + 2) * sin(a));
    int x2 = x + (int)((r + 7) * cos(a));
    int y2 = y + (int)((r + 7) * sin(a));
    tft.drawLine(x1, y1, x2, y2, color);
  }
}

void drawMoonIcon(int x, int y, int r, uint16_t color, uint16_t bg) {
  // Crescent: big circle minus offset circle filled with bg
  tft.fillCircle(x, y, r, color);
  tft.fillCircle(x + r / 2, y - r / 3, r, bg);
}

void showSplashScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader(false);

  // ---- Main content ----
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Big focal text
  tft.drawCentreString("Monitoring", centerX, 70, 4);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString("Local Conditions", centerX, 110, 2);

  // Location
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString(LOCATION_NAME, centerX, 145, 2);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawCentreString("Starting...", centerX, 40, 1);
}

void displayWeather() {


  tft.fillScreen(PALETTE_BG);

  // ---- Header (two-band "gradient") ----
  drawHeader(true);
  
  // ---- Layout ----
  const int pad = 5;
  const int r   = 12;
  const int yTop = 40;

  // ---- Card 1: Primary conditions ----
  int c1x = pad;
  int c1y = yTop;
  int c1w = tft.width() - 2 * pad;
  int c1h = 98;

  tft.fillRoundRect(c1x, c1y, c1w, c1h, r, PALETTE_CARD);
  tft.drawRoundRect(c1x, c1y, c1w, c1h, r, PALETTE_BORDER);

  // Layout: reserve right column for wind
  const int innerPad = 14;
  const int gap = 10;
  const int windColW = 120;

  int leftX = c1x + innerPad;
  int leftW = c1w - (innerPad * 1) - gap - windColW;

  int windX = c1x + c1w - innerPad - windColW;
  int windY = c1y + 14;

  // Temperature string
  String tempNum = String(currentWeather.temperatureF, 0);

  // Draw temp
  tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
  // Big numeric
  tft.drawString(tempNum, leftX, c1y + 16, 6);

  // Medium unit
  tft.drawString("F", leftX + tft.textWidth(tempNum, 6) + 2, c1y + 12, 4);

  // Wind block (right column)
  tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
  tft.drawString("WIND", windX, windY, 2);

  tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
  String windStr = String(currentWeather.windSpeedMph, 0) + " mph";
  tft.drawString(windStr, windX, windY + 22, 4);

  tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
  String dirStr = windDirShort(currentWeather.windDirDeg) + " " + String(currentWeather.windDirDeg) + " deg";
  tft.drawString(dirStr, windX, windY + 58, 2);

  // Condition line, keep it within left column
  String cond = currentWeather.conditionText;
  if (cond.length() > 24) cond = cond.substring(0, 24) + "...";
  tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
  tft.drawString(cond, leftX, c1y + c1h - 30, 4);


  // ---- Card 2: Metrics ----
  int c2x = pad;
  int c2y = c1y + c1h + 5;
  int c2w = c1w;
  int c2h = 78;

  tft.fillRoundRect(c2x, c2y, c2w, c2h, r, PALETTE_CARD);
  tft.drawRoundRect(c2x, c2y, c2w, c2h, r, PALETTE_BORDER);

  // Grid separators
  tft.drawLine(c2x + c2w / 2, c2y + 10, c2x + c2w / 2, c2y + c2h - 10, PALETTE_BORDER);
  tft.drawLine(c2x + 10, c2y + c2h / 2, c2x + c2w - 10, c2y + c2h / 2, PALETTE_BORDER);

  // Bigger metric tile renderer: label font 2, value font 4
  auto metricBig = [&](int x, int y, const char* label, const String& value) {
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString(label, x, y, 2);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    tft.drawString(value, x+45, y, 4);
  };

  int lx = c2x + 10;
  int rx = c2x + c2w / 2 + 10;
  int ty = c2y + 12;
  int by = c2y + c2h / 2 + 8;

  metricBig(lx, ty, "HUM", String(currentWeather.humidityPct, 0) + "%");
  metricBig(rx, ty, "CLOUD",  String(currentWeather.cloudCoverPct == 0 ? "0" : String(currentWeather.cloudCoverPct)) + " %");
  metricBig(lx, by, "RAIN",  String(currentWeather.precipInLastHour, 1) + " in");
  metricBig(rx, by, "PRESS", String(currentWeather.pressureMsl, 0) );

  // ---- Footer ----
  String footerText;
  uint16_t footerColor = PALETTE_MUTE;

  if (!currentWeather.isValid || !everFetchedSuccessfully) {
    footerText = "Weather unavailable";
    footerColor = TFT_RED;
  } else if (currentWeather.fetchedAtUtc > 0) {
    time_t t = currentWeather.fetchedAtUtc;
    struct tm tmLocal;
    if (localtime_r(&t, &tmLocal)) {
      char buf[18];
      strftime(buf, sizeof(buf), "Updated %H:%M", &tmLocal);
      footerText = String(buf);
      // Turn red if data is older than 15 minutes
      if (time(nullptr) - currentWeather.fetchedAtUtc > 15 * 60) {
        footerColor = TFT_RED;
      }
    } else {
      footerText = "Updated";
    }
  } else {
    footerText = "Updated";
  }

  tft.setTextColor(footerColor, PALETTE_BG);
  tft.drawCentreString(footerText, centerX, tft.height() - 16, 2);

}

void updateBlinkIndicator(bool blinkState) {
  // Draw a small blinking dot in top-right corner
  int dotX = tft.width() - 15;
  int dotY = 15;
  int dotSize = 5;
  
  if (blinkState) {
    tft.fillCircle(dotX, dotY, dotSize, TFT_YELLOW);
  } else {
    tft.fillCircle(dotX, dotY, dotSize, TFT_BLACK);
  }
}

void showErrorScreen() {
  tft.fillScreen(PALETTE_BG);
  drawHeader(false);
  tft.drawCentreString("Weather Error", centerX, centerY - 20, FONT_SIZE);
  tft.drawCentreString("Check WiFi Connection", centerX, centerY + 20, FONT_SIZE);
}


void renderScreen(int screen) {
  switch (screen) {
    case 0: displayWeather();        break;
    case 1: displayForecastPage(1);  break;
    case 2: displayForecastPage(2);  break;
    case 3: displayForecastPage(3);  break;
    default: displayWeather();       break;
  }
}

void drawTempArrow(int x, int y, bool up, uint16_t bg) {
  uint16_t color = up ? TFT_RED : TFT_BLUE;
  if (up) {
    // Up arrow
    tft.fillTriangle(x, y, x + 8, y + 10, x - 8, y + 10, color);
  } else {
    // Down arrow
    tft.fillTriangle(x, y + 10, x + 8, y, x - 8, y, color);
  }
}

void drawHeader(bool showDayNightIcon) {
  tft.fillRect(0, 0, tft.width(), 30, HEADER_BG);
  tft.fillRect(0, 30, tft.width(), 4, HEADER_SEP);

  if (showDayNightIcon) {
    if (currentWeather.isDay) {
      drawSunIcon(16, 17, 6, TFT_YELLOW, HEADER_BG);
    } else {
      drawMoonIcon(16, 17, 6, TFT_WHITE, HEADER_BG);
    }
  }

  tft.setTextColor(TFT_WHITE, HEADER_BG);
  tft.drawString(LOCATION_NAME, 30, 8, 2);

  lastClockStr[0] = '\0';  // force drawClockInAppBar to redraw
  drawClockInAppBar();
}

void displayForecastPage(int page) {
  tft.fillScreen(PALETTE_BG);
  drawHeader(true);

  if (!dailyForecast.isValid) {
    tft.setTextColor(PALETTE_MUTE, PALETTE_BG);
    tft.drawCentreString("No forecast data", centerX, centerY, 2);
    return;
  }

  // page 1 = days 0-2, page 2 = days 3-5, page 3 = days 6-8
  int startIdx = (page - 1) * 3;

  const int pad    = 4;
  const int yTop   = 38;
  const int colW   = (tft.width() - (pad * 4)) / 3;
  const int cardH  = tft.height() - yTop - pad;
  const int r      = 8;

  for (int col = 0; col < 3; col++) {
    int idx = startIdx + col;
    if (idx >= dailyForecast.count) break;

    int cx = pad + col * (colW + pad);
    int cy = yTop;

    // Card background
    tft.fillRoundRect(cx, cy, colW, cardH, r, PALETTE_CARD);
    tft.drawRoundRect(cx, cy, colW, cardH, r, PALETTE_BORDER);

    int tx = cx + 5; // text left edge with small inner pad

    // Day name + date on same line
    struct tm t;
    localtime_r(&dailyForecast.dateUtc[idx], &t);
    char dayBuf[4];
    strftime(dayBuf, sizeof(dayBuf), "%a", &t);
    char dateBuf[6];
    strftime(dateBuf, sizeof(dateBuf), "%m/%d", &t);

    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    tft.drawString(dayBuf, tx, cy + 6, 4);
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString(dateBuf, tx, cy + 28, 2);  // font 2 instead of 1

    // Divider
    tft.drawLine(cx + 4, cy + 44, cx + colW - 4, cy + 44, PALETTE_BORDER);

    // High / Low on same line, font 4
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    String hiStr = String(dailyForecast.tempMaxF[idx], 0) + "F/";
    tft.drawString(hiStr, tx, cy + 52, 4);

    

    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    String loStr = String(dailyForecast.tempMinF[idx], 0) + "F";
    // Place lo temp after hi — need to know hi text width
    int hiW = tft.textWidth(hiStr, 4);
    tft.drawString(loStr, tx + hiW + 4, cy + 52, 2);  // slightly lower baseline, smaller

    // Temp trend arrow (skip day 0)
    if (idx > 0) {
      float todayMax = dailyForecast.tempMaxF[idx];
      float prevMax  = dailyForecast.tempMaxF[idx - 1];
      float delta    = todayMax - prevMax;

      int arrowX = tx + hiW - 10;
      int arrowY = cy + 74;

      if (delta > 1.0f) {
        drawTempArrow(arrowX, arrowY, true, PALETTE_CARD);
      } else if (delta < -1.0f) {
        drawTempArrow(arrowX, arrowY, false, PALETTE_CARD);
      } else {
        // Green dash
        int dashY = arrowY + 5;
        tft.drawLine(arrowX - 8, dashY, arrowX + 8, dashY, TFT_GREEN);
        tft.drawLine(arrowX - 8, dashY + 1, arrowX + 8, dashY + 1, TFT_GREEN); // 2px thick
        tft.drawLine(arrowX - 8, dashY + 2, arrowX + 8, dashY + 2, TFT_GREEN); // 3px thick
      }
    }

    // Divider
    tft.drawLine(cx + 4, cy + 88, cx + colW - 4, cy + 88, PALETTE_BORDER);

    // Precip + Wind columns
    int rx = cx + colW / 2 + 2;  // right column start

    // Left: Prob
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString("Prob", tx, cy + 94, 1);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    String probStr = String(dailyForecast.precipProbMax[idx]) + "%";
    tft.drawString(probStr, tx, cy + 104, 2);

    // Right: Wind speed
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString("Wind", rx, cy + 94, 1);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    String windStr = String(dailyForecast.windMaxMph[idx], 0);
    tft.drawString(windStr, rx, cy + 104, 2);

    // Left: Rain
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString("Rain", tx, cy + 124, 1);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    String rainStr = String(dailyForecast.precipSumIn[idx], 2) + "\"";
    tft.drawString(rainStr, tx, cy + 134, 2);

    // Right: Wind direction
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString("Dir", rx, cy + 124, 1);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    tft.drawString(windDirShort(dailyForecast.windDirDom[idx]), rx, cy + 134, 2);

    // Divider
    tft.drawLine(cx + 4, cy + 154, cx + colW - 4, cy + 154, PALETTE_BORDER);

    // ETo
    tft.setTextColor(PALETTE_MUTE, PALETTE_CARD);
    tft.drawString("ET", tx, cy + 160, 1);
    tft.setTextColor(PALETTE_TEXT, PALETTE_CARD);
    String etStr = String(dailyForecast.et0Evapo[idx], 2);
    tft.drawString(etStr, tx, cy + 172, 2);  // font 2 instead of whatever was there
  }
}