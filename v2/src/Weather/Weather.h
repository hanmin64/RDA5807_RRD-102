#ifndef WEATHER_H       // 防止重複編譯：若未定義 WEATHER_H 則編譯以下內容
#define WEATHER_H       // 定義 WEATHER_H，確保此標頭檔只被編譯一次

#include <Arduino.h>    // Arduino 核心函式庫：提供 String、Serial 等基礎型別

// ============================================================
//  WeatherClient — OpenWeatherMap 天氣查詢封裝
// ------------------------------------------------------------
//  將「組 URL → HTTP GET → JSON 解析 → 儲存結果」全部封裝為單一類別，
//  主程式只需簡單的設定與呼叫即可取得天氣資料：
//
//      Weather.apiKey  = "你的金鑰";   // 於 fetch() 前設定 API 金鑰
//      Weather.city    = "Taipei";     // 可選：變更城市名稱
//      Weather.fetch();                // 執行查詢並儲存結果
//
//  查詢成功後可透過 getter 取得最新天氣資料：
//      Weather.getDescription()   // 天氣概況（如 "clear sky"）
//      Weather.getTemp()          // 溫度（如 "25.3"）
//      Weather.getHumidity()      // 濕度（如 "68"）
//      Weather.getWeatherId()     // OpenWeatherMap 天氣代碼（用於繪製圖示）
//
//  全域單例 Weather 已建立於 Weather.cpp，可直接使用
// ============================================================
class WeatherClient {   // 天氣查詢客戶端類別
 public:                  // 公開成員：外部可存取的函式與變數

  // --- 設定參數（可於 fetch() 前修改）---
  String city        = "Taipei";           // 查詢城市名稱（預設台北）
  String countryCode = "TW";               // 國家代碼（ISO 3166-1 alpha-2）
  String apiKey      = "";                 // OpenWeatherMap API 金鑰（須自行申請）

  // 執行天氣查詢（HTTP GET → JSON 解析 → 儲存內部成員）
  // 回傳 true 表示查詢與解析皆成功
  bool fetch();

  // --- 取得最近一次查詢的結果（getter）---
  String getDescription() const { return _description; }  // 天氣概況文字說明
  String getTemp()        const { return _temp; }         // 當前溫度 (°C)
  String getHumidity()    const { return _humidity; }     // 相對濕度 (%)
  String getPressure()    const { return _pressure; }     // 大氣壓力 (hPa)

  // 取得 OpenWeatherMap 天氣狀況代碼（主程式用於繪製對應的天氣圖示）
  // 重要分類：2xx=雷雨, 3xx=毛毛雨, 5xx=雨, 6xx=雪, 7xx=霧, 800=晴天, 80x=多雲
  int getWeatherId() const { return _weatherId; }

  // 是否已成功取得過至少一次天氣資料（用於 UI 判斷）
  bool isFetched() const { return _fetched; }

 private:                 // 私有成員：僅類別內部可存取

  // --- 內部儲存：最近一次查詢的天氣資料 ---
  String _description = "";   // 天氣概況文字（如 "clear sky"、"light rain"）
  String _temp        = "";   // 溫度（攝氏，字串形式如 "25.3"）
  String _humidity    = "";   // 濕度百分比（字串形式如 "68"）
  String _pressure    = "";   // 大氣壓力值（hPa，字串形式如 "1013"）
  int    _weatherId   = 800;  // OpenWeatherMap 天氣代碼（預設 800 = 晴天）
  bool   _fetched     = false; // 是否已成功取得過資料
};

// 全域單例（全域變數宣告），在 Weather.cpp 中定義實體
extern WeatherClient Weather;

#endif  // WEATHER_H     // 條件編譯結束
