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