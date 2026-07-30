#include "Weather.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>

// 全域單例實體
WeatherClient Weather;

// ------------------------------------------------------------
// 查詢天氣資料（OpenWeatherMap）並以序列埠印出結果
// ------------------------------------------------------------
bool WeatherClient::fetch() {
  if (apiKey.length() == 0) {
    Serial.println(F("[Weather] 尚未設定 apiKey，跳過查詢"));
    return false;
  }

  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + city + "," +
               countryCode + "&units=metric&appid=" + apiKey;

  Serial.println(F("[Weather] 查詢天氣資料中..."));

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  bool ok = false;
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    DynamicJsonDocument doc(payload.length() * 2);
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      Serial.print(F("[Weather] JSON 解析失敗: "));
      Serial.println(err.c_str());
    } else {
      // 解析成功 → 儲存天氣資料到內部成員
      _description = doc["weather"][0]["description"].as<String>();
      _temp        = doc["main"]["temp"].as<String>();
      _pressure    = doc["main"]["pressure"].as<String>();
      _humidity    = doc["main"]["humidity"].as<String>();
      _weatherId   = doc["weather"][0]["id"].as<int>();
      _fetched     = true;

      Serial.println(F("----------------------------------"));
      Serial.print(F("Weather description: "));
      Serial.println(_description);
      Serial.print(F("Temp: "));
      Serial.print(_temp);
      Serial.println(F(" °C"));
      Serial.print(F("Pressure: "));
      Serial.print(_pressure);
      Serial.println(F(" hPa"));
      Serial.print(F("Humidity: "));
      Serial.print(_humidity);
      Serial.println(F(" %"));
      Serial.println(F("----------------------------------"));
      ok = true;
    }
  } else {
    Serial.print(F("[Weather] HTTP GET 失敗，錯誤碼: "));
    Serial.println(httpCode);
  }
  http.end();
  return ok;
}

// ------------------------------------------------------------
// JSON 字元跳脫（保留供未來擴充使用）
// ------------------------------------------------------------
String WeatherClient::escapeJSON(const String& s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '"':  r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += " ";    break;
      case '\r': r += " ";    break;
      default:   r += c;
    }
  }
  return r;
}
