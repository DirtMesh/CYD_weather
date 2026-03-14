#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct CurrentConditions {
  float temperatureF;
  float humidityPct;
  float windSpeedMph;
  int   windDirDeg;
  int   cloudCoverPct;
  float precipInLastHour;   // Open-Meteo "precipitation" is typically an interval sum
  int   weatherCode;
  float pressureMsl;

  String conditionText;
  bool  isDay;              // from is_day
  bool  isValid;
  time_t fetchedAtUtc;      // optional, helps UI + debugging
};

struct DailyAstronomy {
  // Store minutes since midnight local for easy day-bar math
  int sunriseMin;           // 0..1439
  int sunsetMin;            // 0..1439
  int dayOfYear;            // to know when it is stale
  bool isValid;
};

struct HourlyForecast {
  // Keep this simple at first: next N hours
  static const int N = 24;
  int count;
  time_t tUtc[N];

  float tempF[N];
  int   precipProbPct[N];
  float precipIn[N];
  float windMph[N];
  int   windDirDeg[N];
  int   weatherCode[N];

  bool isValid;
};

struct DailyForecast {
  static const int N = 9;
  int count;

  time_t dateUtc[N];
  float  tempMaxF[N];
  float  tempMinF[N];
  float  precipSumIn[N];
  int    precipProbMax[N];
  float  et0Evapo[N];
  int    weatherCode[N];
  float  windMaxMph[N];
  int    windDirDom[N];  // dominant wind direction in degrees

  bool isValid;
};

class WeatherAPI {
public:
  WeatherAPI(float lat, float lon) : latitude(lat), longitude(lon) {}
    
  CurrentConditions getCurrentWeather() {
  CurrentConditions data{};
  data.isValid = false;
  data.conditionText = "Error";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping current weather");
    return data;
  }

  // Build URL: current-only, US units, auto timezone
  String url =
    "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) +
    "&longitude=" + String(longitude, 4) +
    "&current="
      "temperature_2m,"
      "relative_humidity_2m,"
      "precipitation,"
      "weather_code,"
      "wind_speed_10m,"
      "wind_direction_10m,"
      "cloud_cover,"
      "pressure_msl,"
      "is_day"
    "&temperature_unit=fahrenheit"
    "&wind_speed_unit=mph"
    "&precipitation_unit=inch"
    "&timezone=auto";

  HTTPClient http;
  http.setTimeout(8000); // ms

  if (!http.begin(url)) {
    Serial.println("HTTP begin() failed");
    return data;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("Weather HTTP error: ");
    Serial.println(httpCode);
    http.end();
    return data;
  }

  String payload = http.getString();
  http.end();

  // Parse JSON into CurrentConditions
  data = parseCurrentJSON(payload);  // rename your parser accordingly

  if (!data.isValid) {
    Serial.println("Weather parse failed or missing fields");
  }

  return data;
}
  
  DailyForecast getDailyForecast() {
    DailyForecast data{};
    data.isValid = false;
    data.count = 0;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected, skipping daily forecast");
      return data;
    }

    String url =
      "https://api.open-meteo.com/v1/forecast?latitude=" + String(latitude, 4) +
      "&longitude=" + String(longitude, 4) +
      "&daily="
        "temperature_2m_max,"
        "temperature_2m_min,"
        "precipitation_sum,"
        "precipitation_probability_max,"
        "et0_fao_evapotranspiration,"
        "weather_code,"
        "wind_speed_10m_max,"
        "wind_direction_10m_dominant"
      "&temperature_unit=fahrenheit"
      "&wind_speed_unit=mph"
      "&precipitation_unit=inch"
      "&timezone=auto"
      "&forecast_days=9";

    HTTPClient http;
    http.setTimeout(8000);

    if (!http.begin(url)) {
      Serial.println("Daily forecast HTTP begin() failed");
      return data;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.print("Daily forecast HTTP error: ");
      Serial.println(httpCode);
      http.end();
      return data;
    }

    String payload = http.getString();
    http.end();

    return parseDailyJSON(payload);
  }


private:
  float latitude;
  float longitude;
  
  CurrentConditions parseCurrentJSON(const String& json) {
    CurrentConditions data{};
    data.isValid = false;
    data.conditionText = "Error";
    data.fetchedAtUtc = 0;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return data;

    JsonVariant curV = doc["current"];
    if (curV.isNull() || !curV.is<JsonObject>()) return data;
    JsonObject cur = curV.as<JsonObject>();

    // Required-ish fields
    data.temperatureF     = cur["temperature_2m"]        | 0.0f;
    data.humidityPct      = cur["relative_humidity_2m"]  | 0.0f;
    data.windSpeedMph     = cur["wind_speed_10m"]        | 0.0f;
    data.windDirDeg       = cur["wind_direction_10m"]    | 0;
    data.cloudCoverPct    = cur["cloud_cover"]           | 0;
    data.precipInLastHour = cur["precipitation"]         | 0.0f;
    data.pressureMsl      = cur["pressure_msl"]          | 0.0f; 
    data.weatherCode      = cur["weather_code"]          | -1;
    data.isDay            = ((cur["is_day"]              | 0) == 1);

    // Condition text
    data.conditionText = getWeatherCondition(data.weatherCode);

    // Timestamp for UI
    time_t now = time(nullptr);
    if (now > 24 * 3600) data.fetchedAtUtc = now;

    // Sanity: if weatherCode missing, treat as invalid
    if (data.weatherCode < 0) return data;

    data.isValid = true;
    return data;
    }
  
  DailyForecast parseDailyJSON(const String& json) {
    DailyForecast data{};
    data.isValid = false;
    data.count = 0;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
      Serial.println("Daily JSON parse error");
      return data;
    }

    JsonVariant dailyV = doc["daily"];
    if (dailyV.isNull()) return data;
    JsonObject daily = dailyV.as<JsonObject>();

    JsonArray tempMax  = daily["temperature_2m_max"];
    JsonArray tempMin  = daily["temperature_2m_min"];
    JsonArray precip   = daily["precipitation_sum"];
    JsonArray precipP  = daily["precipitation_probability_max"];
    JsonArray windMax  = daily["wind_speed_10m_max"];
    JsonArray windDir  = daily["wind_direction_10m_dominant"];
    JsonArray et0      = daily["et0_fao_evapotranspiration"];
    JsonArray wCode    = daily["weather_code"];
    JsonArray dates    = daily["time"];

    if (tempMax.isNull() || dates.isNull()) return data;

    int n = min((int)dates.size(), (int)DailyForecast::N);
    for (int i = 0; i < n; i++) {
      // Parse date string "YYYY-MM-DD" into time_t
      struct tm t{};
      const char* dateStr = dates[i];
      if (dateStr) {
        sscanf(dateStr, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday);
        t.tm_year -= 1900;
        t.tm_mon  -= 1;
        data.dateUtc[i] = mktime(&t);
      }

      data.tempMaxF[i]     = tempMax[i]  | 0.0f;
      data.tempMinF[i]     = tempMin[i]  | 0.0f;
      data.precipSumIn[i]  = precip[i]   | 0.0f;
      data.precipProbMax[i]= precipP[i]  | 0;
      data.et0Evapo[i]     = et0[i]      | 0.0f;
      data.weatherCode[i]  = wCode[i]    | 0;
      data.windMaxMph[i]  = windMax[i]   | 0.0f;
      data.windDirDom[i]  = windDir[i]   | 0;
      data.count++;
    }

    data.isValid = (data.count > 0);
    return data;
  }
  
String getWeatherCondition(int code) {
    // Sky
    if (code == 0) return "Clear";
    if (code == 1) return "Mostly Clear";
    if (code == 2) return "Partly Cloudy";
    if (code == 3) return "Overcast";

    // Fog
    if (code == 45 || code == 48) return "Fog";

    // Drizzle
    if (code >= 51 && code <= 55) return "Drizzle";
    if (code == 56 || code == 57) return "Freezing Drizzle";

    // Rain
    if (code >= 61 && code <= 65) return "Rain";
    if (code == 66 || code == 67) return "Freezing Rain";

    // Snow
    if (code >= 71 && code <= 75) return "Snow";
    if (code == 77) return "Snow Grains";

    // Showers
    if (code >= 80 && code <= 82) return "Rain Showers";
    if (code == 85 || code == 86) return "Snow Showers";

    // Thunderstorm
    if (code == 95) return "Thunderstorm";
    if (code == 96 || code == 99) return "Thunderstorm (Hail)";

    return "Unknown";
    }
};

#endif