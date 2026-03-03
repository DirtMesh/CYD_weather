#include <SPI.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <XPT2046_Touchscreen.h> // Touch screen library
#include <WiFi.h>
#include "wifi_config.h" // WiFi credentials
#include "weather_api.h" // Weather API wrapper
#include "gps_config.h" // GPS coordinates for weather API
#include <time.h>
#include <sys/time.h>
#include <math.h>


// Function declarations
void connectToWiFi();
void showSplashScreen();
void displayWeather();
void updateBlinkIndicator(bool blinkState);
void showErrorScreen();
void fetchWeather();
bool syncTime(uint32_t timeout_ms);

TFT_eSPI tft = TFT_eSPI();

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;
int centerX, centerY;

// Weather API
WeatherAPI* weatherAPI = nullptr;

// Timing
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_UPDATE_INTERVAL = 5 * 60 * 1000; // 5 minutes
const unsigned long CLOCK_UPDATE_INTERVAL = 20 * 1000; // 20 seconds
unsigned long lastBlink = 0;
const unsigned long BLINK_INTERVAL = 500; // 500ms blink
bool blinkState = false;

// Current weather (Now page)
CurrentConditions currentWeather = {
  .temperatureF     = 0.0f,
  .humidityPct      = 0.0f,
  .windSpeedMph     = 0.0f,
  .windDirDeg       = 0,
  .cloudCoverPct    = 0,
  .precipInLastHour = 0.0f,
  .weatherCode      = -1,
  .conditionText    = "Loading",
  .isDay            = false,
  .isValid          = false,
  .fetchedAtUtc     = 0
};

// NTP Server settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;                    // GMT offset in seconds (change this)
const int daylightOffset_sec = 0;                // Daylight saving offset in seconds

unsigned long lastClockTick = 0;
char lastClockStr[6] = ""; // "HH:MM"

bool syncTime(uint32_t timeout_ms = 30000) {
  Serial.println("Syncing time with NTP server...");

  // Configure TZ and NTP servers
  configTzTime(
    TIMEZONE_STR,
    "pool.ntp.org",
    "time.nist.gov",
    "time.google.com"
  );

  // Wait for time to be set
  struct tm timeinfo;
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    if (getLocalTime(&timeinfo, 1000)) {
      Serial.println("Time synced!");
      Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S %Z");
      return true;
    }
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Failed to sync time");
  return false;
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");

  WiFi.mode(WIFI_STA);
  if (strlen(WIFI_PASSWORD) > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    WiFi.begin(WIFI_SSID);
  }
  
  int barWidth = 200;
  int barHeight = 10;
  int barX = (tft.width() - barWidth) / 2;
  int barY = tft.height() - 40;

  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ Connected!");
  } else {
    Serial.println("✗ Failed to connect");
    delay(3000);
  }
}

void showSplashScreen() {
  tft.fillScreen(TFT_BLACK);

  // ---- App bar ----
  tft.fillRect(0, 0, tft.width(), 28, TFT_DARKGREY);
  tft.drawLine(0, 27, tft.width(), 27, TFT_CYAN);

  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.drawString("Weather Station", 8, 6, 2);

  // Status dot (feels alive)
  tft.fillCircle(tft.width() - 14, 14, 4, TFT_GREEN);

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

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, skipping weather fetch");
    return;
  }
  
  Serial.println("Fetching weather...");
  
  currentWeather = weatherAPI->getCurrentWeather();
  
  if (currentWeather.isValid) {
    Serial.println("Weather fetched successfully");
    displayWeather();
  } else {
    Serial.println("Failed to fetch weather");
    showErrorScreen();
  }
  
}

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

  const uint16_t APPBAR = 0x039F; // Teal-ish color for app bar background

  // Clear just the clock region
  int clockW = 60;
  int clockH = 18;
  int clockBuff = 30;
  int clockX = tft.width() - clockBuff - clockW;
  int clockY = 5;
  tft.fillRect(clockX, clockY, clockW, clockH, APPBAR);

  // Draw the new time
  tft.setTextColor(TFT_WHITE, APPBAR);
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

void displayWeather() {
  // ---- Modern palette (brighter, consumer) ----
  const uint16_t BG        = 0xF7BE;   // very light blue-grey background
  const uint16_t HEADER1   = 0x039F;   // teal-ish
  const uint16_t HEADER2   = 0x01F3;   // slightly darker teal band
  const uint16_t CARD      = TFT_WHITE;
  const uint16_t BORDER    = 0xD69A;   // light border
  const uint16_t TEXT_DARK = 0x18E3;   // near-black, softer than pure black
  const uint16_t TEXT_MUTE = 0x6B4D;   // muted grey
  const uint16_t ACCENT    = 0xFD20;   // warm accent (orange) for highlights

  tft.fillScreen(BG);

  // ---- Header (two-band "gradient") ----
  tft.fillRect(0, 0, tft.width(), 30, HEADER1);
  tft.fillRect(0, 30, tft.width(), 4, HEADER2);

  // Day/Night icon next to location
  int iconX = 16;
  int iconY = 17;
  if (currentWeather.isDay) {
    drawSunIcon(iconX, iconY, 6, TFT_YELLOW, HEADER1);
  } else {
    drawMoonIcon(iconX, iconY, 6, TFT_WHITE, HEADER1);
  }

  // Location text (shifted right to make room for icon)
  tft.setTextColor(TFT_WHITE, HEADER1);
  tft.drawString(LOCATION_NAME, 30, 8, 2);

  // Clock
  drawClockInAppBar();

  // ---- Layout ----
  const int pad = 5;
  const int r   = 12;
  const int yTop = 40;

  // ---- Card 1: Primary conditions ----
  int c1x = pad;
  int c1y = yTop;
  int c1w = tft.width() - 2 * pad;
  int c1h = 98;

  tft.fillRoundRect(c1x, c1y, c1w, c1h, r, CARD);
  tft.drawRoundRect(c1x, c1y, c1w, c1h, r, BORDER);

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
  tft.setTextColor(TEXT_DARK, CARD);
  // Big numeric
  tft.drawString(tempNum, leftX, c1y + 16, 6);

  // Medium unit
  tft.drawString("F", leftX + tft.textWidth(tempNum, 6) + 2, c1y + 12, 4);

  // Wind block (right column)
  tft.setTextColor(TEXT_MUTE, CARD);
  tft.drawString("WIND", windX, windY, 2);

  tft.setTextColor(TEXT_DARK, CARD);
  String windStr = String(currentWeather.windSpeedMph, 0) + " mph";
  tft.drawString(windStr, windX, windY + 22, 4);

  tft.setTextColor(TEXT_MUTE, CARD);
  String dirStr = windDirShort(currentWeather.windDirDeg) + " " + String(currentWeather.windDirDeg) + " deg";
  tft.drawString(dirStr, windX, windY + 58, 2);

  // Condition line, keep it within left column
  String cond = currentWeather.conditionText;
  if (cond.length() > 24) cond = cond.substring(0, 24) + "...";
  tft.setTextColor(TEXT_DARK, CARD);
  tft.drawString(cond, leftX, c1y + c1h - 30, 4);


  // ---- Card 2: Metrics ----
  int c2x = pad;
  int c2y = c1y + c1h + 5;
  int c2w = c1w;
  int c2h = 78;

  tft.fillRoundRect(c2x, c2y, c2w, c2h, r, CARD);
  tft.drawRoundRect(c2x, c2y, c2w, c2h, r, BORDER);

  // Grid separators
  tft.drawLine(c2x + c2w / 2, c2y + 10, c2x + c2w / 2, c2y + c2h - 10, BORDER);
  tft.drawLine(c2x + 10, c2y + c2h / 2, c2x + c2w - 10, c2y + c2h / 2, BORDER);

  // Bigger metric tile renderer: label font 2, value font 4
  auto metricBig = [&](int x, int y, const char* label, const String& value) {
    tft.setTextColor(TEXT_MUTE, CARD);
    tft.drawString(label, x, y, 2);
    tft.setTextColor(TEXT_DARK, CARD);
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
  tft.setTextColor(TEXT_MUTE, BG);

  String footerText;

  if (!currentWeather.isValid) {
    footerText = "Weather unavailable";
  } else if (currentWeather.fetchedAtUtc > 0) {
    time_t t = currentWeather.fetchedAtUtc;
    struct tm tmLocal;
    if (localtime_r(&t, &tmLocal)) {
      char buf[18];
      strftime(buf, sizeof(buf), "Updated %H:%M", &tmLocal);
      footerText = String(buf);
    } else {
      footerText = "Updated";
    }
  } else {
    footerText = "Updated";
  }

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
  tft.fillScreen(TFT_BLACK);
  tft.drawCentreString("Weather Error", centerX, centerY - 20, FONT_SIZE);
  tft.drawCentreString("Check WiFi Connection", centerX, centerY + 20, FONT_SIZE);
}



void setup() {
  Serial.begin(115200);

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 3: touchscreen.setRotation(3);
  touchscreen.setRotation(1);

  // Start the tft display
  tft.init();
  // Set the TFT display rotation in landscape mode
  tft.setRotation(1);

  // Clear the screen before writing to it
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  
  // Set X and Y coordinates for center of display
  centerX = SCREEN_WIDTH / 2;
  centerY = SCREEN_HEIGHT / 2;

  // Show initial screen
  showSplashScreen();

  // WiFi connection
  connectToWiFi();

  // Sync time with NTP
  bool timeOk = syncTime();

  // Initialize weather API
  weatherAPI = new WeatherAPI(GPS_LATITUDE, GPS_LONGITUDE);

  // Fetch initial weather
  fetchWeather();
}

void loop() {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z) info on the TFT display and Serial Monitor
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;

  }

  // Check for weather update (every 5 minutes)
  if (millis() - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {
    fetchWeather();
    lastWeatherUpdate = millis();
  }

  if (millis() - lastClockTick >= CLOCK_UPDATE_INTERVAL) {
    drawClockInAppBar();
    lastClockTick = millis();
  }
  
  // Update blinking indicator
  if (millis() - lastBlink >= BLINK_INTERVAL) {
    blinkState = !blinkState;
    updateBlinkIndicator(blinkState);
    lastBlink = millis();
  }
  delay(10);
}

