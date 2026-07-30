#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

// ============================================================
//  WeatherClient — OpenWeatherMap 天氣查詢封裝
// ------------------------------------------------------------
//  將「組 URL → HTTP GET → JSON 解析 → 儲存結果」全部封裝，
//  主程式只需：
//      #include "src/Weather.h"
//      Weather.apiKey  = "你的金鑰";   // 可於 fetch() 前修改
//      Weather.city    = "Taipei";
//      Weather.fetch();                 // 查詢並儲存天氣
//
//  查詢成功後可透過 getter 取得最新天氣資料：
//      Weather.getDescription()   // 天氣概況 (如 "clear sky")
//      Weather.getTemp()          // 溫度 (如 "25.3")
//      Weather.getHumidity()      // 濕度 (如 "68")
//      Weather.getPressure()      // 大氣壓力 (如 "1013")
//      Weather.isFetched()        // 是否已成功取得過資料
//
//  全域單例 Weather 已建立，可直接使用
// ============================================================
class WeatherClient {
 public:
  // 設定參數（可於 fetch() 前修改）
  String city        = "Taipei";           // 城市名稱
  String countryCode = "TW";               // 國家代碼
  String apiKey      = "";                 // OpenWeatherMap API 金鑰

  // 查詢天氣資料（OpenWeatherMap），解析後儲存至內部成員，
  // 並以序列埠印出結果。回傳 true 表示查詢成功。
  bool fetch();

  // --- 取得最近一次查詢的天氣資料 ---
  String getDescription() const { return _description; }  // 天氣概況
  String getTemp()        const { return _temp; }         // 溫度 (°C)
  String getHumidity()    const { return _humidity; }     // 濕度 (%)
  String getPressure()    const { return _pressure; }     // 大氣壓力 (hPa)

  // OpenWeatherMap 天氣狀況代碼 (如 800=晴天, 801=多雲, 5xx=雨)
  int getWeatherId() const { return _weatherId; }

  // 是否已成功取得過至少一次天氣資料
  bool isFetched() const { return _fetched; }

 private:
  // 將字串中的 JSON 特殊字元跳脫，避免破壞輸出（保留供未來擴充）
  String escapeJSON(const String& s);

  // 儲存最近一次查詢結果
  String _description = "";   // 天氣概況 (如 "clear sky")
  String _temp        = "";   // 溫度 (攝氏)
  String _humidity    = "";   // 濕度 (%)
  String _pressure    = "";   // 大氣壓力 (hPa)
  int    _weatherId   = 800;  // OpenWeatherMap 天氣狀況代碼
  bool   _fetched     = false; // 是否已成功取得過資料
};

// 全域單例
extern WeatherClient Weather;

#endif  // WEATHER_H
