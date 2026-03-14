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
#include "display.h" // Display functions


// Function declarations
void connectToWiFi();
void fetchCurrentWeather();
void fetchDailyForecast();
bool syncTime(uint32_t timeout_ms);
void renderScreen(int screen);
bool ensureWiFi();

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

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;
int centerX, centerY;

int currentScreen = 0; // 0=Now, 1=days1-3, 2=days4-6, 3=7-9
unsigned long lastTouchTime = 0;
const unsigned long TOUCH_DEBOUNCE = 400; //ms


// Weather API
WeatherAPI* weatherAPI = nullptr;

// Timing
unsigned long lastCurrentUpdate = 0;
unsigned long lastDailyUpdate = 0;
const unsigned long CURRENT_UPDATE_INTERVAL = 5 * 60 * 1000;
const unsigned long DAILY_UPDATE_INTERVAL   = 60 * 60 * 1000;
const unsigned long CLOCK_UPDATE_INTERVAL = 20 * 1000; // 20 seconds
unsigned long lastBlink = 0;
const unsigned long BLINK_INTERVAL = 500; // 500ms blink
bool blinkState = false;
bool everFetchedSuccessfully = false;

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

// Forecast
DailyForecast dailyForecast;

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
    Serial.println("✗ Failed to connect, will retry");
  }
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;

  int attempt = 1;
  unsigned long retryDelay = 5000;  // 5s first retry

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi reconnect attempt ");
    Serial.println(attempt);

    WiFi.disconnect();
    if (strlen(WIFI_PASSWORD) > 0) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
      WiFi.begin(WIFI_SSID);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✓ Reconnected!");
      return true;
    }

    Serial.print("Waiting ");
    Serial.print(retryDelay / 1000);
    Serial.println("s before next attempt");
    delay(retryDelay);
    retryDelay = 30000;  // 30s for all subsequent retries
    attempt++;
  }
  return true;
}

void handleTouch() {
  if (!touchscreen.tirqTouched() || !touchscreen.touched()) return;
  
  // Debounce
  if (millis() - lastTouchTime < TOUCH_DEBOUNCE) return;
  lastTouchTime = millis();

  TS_Point p = touchscreen.getPoint();
  x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
  y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
  z = p.z;

  //Serial.print("Touch: x="); Serial.print(x);
  //Serial.print(" y="); Serial.println(y);

  if (x > SCREEN_WIDTH / 2) {
    // Right half — forward
    if (currentScreen < 3) {
      currentScreen++;
      renderScreen(currentScreen);
    }
  } else {
    // Left half — back
    if (currentScreen > 0) {
      currentScreen--;
      renderScreen(currentScreen);
    }
  }
}

void fetchCurrentWeather() {
  if (!ensureWiFi()) return;

  Serial.println("Fetching current weather...");
  CurrentConditions newWeather = weatherAPI->getCurrentWeather();

  if (newWeather.isValid) {
    currentWeather = newWeather;
    everFetchedSuccessfully = true;
    Serial.println("Current weather fetch successful");
  } else {
    Serial.println("Current weather fetch failed — keeping existing data");
  }

  renderScreen(currentScreen);
}

void fetchDailyForecast() {
  if (!ensureWiFi()) return;

  Serial.println("Fetching daily forecast...");
  DailyForecast newDaily = weatherAPI->getDailyForecast();

  if (newDaily.isValid) {
    dailyForecast = newDaily;
    Serial.println("Daily forecast fetch successful");
  } else {
    Serial.println("Daily forecast fetch failed — keeping existing data");
  }

  renderScreen(currentScreen);
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
  if (!syncTime()) Serial.println("Continuing without time sync");

  // Initialize weather API
  weatherAPI = new WeatherAPI(GPS_LATITUDE, GPS_LONGITUDE);

  // Fetch initial weather
  fetchCurrentWeather();
  fetchDailyForecast();
  lastCurrentUpdate = millis();
  lastDailyUpdate   = millis();
  drawClockInAppBar();
}

void loop() {
  handleTouch();

  if (millis() - lastCurrentUpdate >= CURRENT_UPDATE_INTERVAL) {
    fetchCurrentWeather();
    lastCurrentUpdate = millis();
  }

  if (millis() - lastDailyUpdate >= DAILY_UPDATE_INTERVAL) {
    fetchDailyForecast();
    lastDailyUpdate = millis();
  }

  if (millis() - lastClockTick >= CLOCK_UPDATE_INTERVAL) {
    drawClockInAppBar();
    lastClockTick = millis();
  }

  if (millis() - lastBlink >= BLINK_INTERVAL) {
    blinkState = !blinkState;
    updateBlinkIndicator(blinkState);
    lastBlink = millis();
  }

  delay(10);
}

