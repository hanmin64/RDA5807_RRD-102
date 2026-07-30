#include "Weather.h"     // 引入對應的標頭檔

#include <HTTPClient.h>  // ESP32 HTTP 客戶端函式庫：提供 HTTP GET/POST 請求功能
#include <ArduinoJson.h> // ArduinoJson 函式庫 v7：用於解析 OpenWeatherMap 回傳的 JSON

// 全域單例實體：所有主程式檔案都可透過 Weather 變數存取此物件
WeatherClient Weather;

// ------------------------------------------------------------
// 查詢 OpenWeatherMap 天氣資料
// 流程：組 URL → HTTP GET → JSON 解析 → 儲存至內部成員
// 回傳 true 表示全部成功
// ------------------------------------------------------------
bool WeatherClient::fetch() {
  // 檢查 API 金鑰是否已設定，若無則跳過查詢
  if (apiKey.length() == 0) {
    Serial.println(F("[Weather] 尚未設定 apiKey，跳過查詢"));
    return false;
  }

  // 組合 OpenWeatherMap API 的請求 URL
  // 格式：https://api.openweathermap.org/data/2.5/weather?q=城市,國家代碼&units=metric&appid=金鑰
  // units=metric 表示以攝氏度回傳溫度
  String url = "https://api.openweathermap.org/data/2.5/weather?q=" + city + "," +
               countryCode + "&units=metric&appid=" + apiKey;

  Serial.println(F("[Weather] 查詢天氣資料中..."));

  HTTPClient http;        // 建立 HTTP 客戶端物件
  http.begin(url);        // 設定目標 URL（會自動處理 HTTPS 憑證）
  int httpCode = http.GET();  // 執行 HTTP GET 請求，回傳狀態碼

  bool ok = false;        // 記錄是否成功取得與解析資料
  // 檢查 HTTP 回應狀態碼是否為 200（OK）
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();  // 取得回應主體字串（JSON 格式）

    JsonDocument doc;                   // ArduinoJson v7 JsonDocument（自動管理記憶體）
    DeserializationError err = deserializeJson(doc, payload);  // 將 JSON 字串解析到 doc

    // 檢查 JSON 解析是否發生錯誤
    if (err) {
      Serial.print(F("[Weather] JSON 解析失敗: "));
      Serial.println(err.c_str());     // 印出錯誤訊息
    } else {
      // --- 解析成功：從 JSON 文件中取出各欄位值 ---
      // 路徑說明：weather[0] 為第一個天氣狀況（通常只有一個）
      //           main 物件包含溫度、壓力、濕度等數據
      _description = doc["weather"][0]["description"].as<String>();  // 天氣概況文字
      _temp        = doc["main"]["temp"].as<String>();                // 當前溫度
      _pressure    = doc["main"]["pressure"].as<String>();            // 大氣壓力
      _humidity    = doc["main"]["humidity"].as<String>();            // 相對濕度
      _weatherId   = doc["weather"][0]["id"].as<int>();               // 天氣代碼
      _fetched     = true;                                            // 標記已成功取得資料

      // 將結果輸出到序列埠以供除錯查看
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
      ok = true;                       // 標記成功
    }
  } else {
    // HTTP 請求失敗（可能是網路問題或 API 金鑰錯誤）
    Serial.print(F("[Weather] HTTP GET 失敗，錯誤碼: "));
    Serial.println(httpCode);          // 印出 HTTP 狀態碼（如 401=未授權, 404=找不到城市）
  }
  http.end();            // 關閉 HTTP 連線，釋放資源
  return ok;             // 回傳是否成功
}

// ------------------------------------------------------------
// JSON 字元跳脫（保留供未來擴充使用）
// 將字串中的特殊字元（引號、反斜線、換行）替換為安全的跳脫形式
// ------------------------------------------------------------
String WeatherClient::escapeJSON(const String& s) {
  String r;              // 儲存跳脫後的結果字串
  r.reserve(s.length()); // 預先分配足夠的記憶體，避免重複擴充
  for (size_t i = 0; i < s.length(); ++i) {  // 逐字元處理
    char c = s[i];
    switch (c) {
      case '"':  r += "\\\""; break;  // 雙引號 → \"
      case '\\': r += "\\\\"; break;  // 反斜線 → \\
      case '\n': r += " ";    break;  // 換行符 → 空白
      case '\r': r += " ";    break;  // 回車符 → 空白
      default:   r += c;              // 一般字元直接保留
    }
  }
  return r;              // 回傳跳脫後的字串
}
