#ifndef NTPTIME_H
#define NTPTIME_H

#include <Arduino.h>
#include <time.h>

// ============================================================
//  NTPClock — ESP32 NTP 網路對時封裝
// ------------------------------------------------------------
//  功能說明：
//    1. 開機後連上 WiFi 後取得一次 NTP 時間
//    2. 之後以 millis() 維持本地時鐘，每秒可更新顯示
//    3. 每一小時重新校正一次 NTP 時間
//
//  使用方式：
//    NTP.begin();          // setup() 中呼叫
//    NTP.handle();         // loop() 中呼叫，負責每小時校時
//    NTP.print();          // 顯示目前時間
//    NTP.getDateTime("%H:%M:%S");
// ============================================================
class NTPClock {
 public:
  // 設定參數（可於 begin() 前修改）
  const char* ntpServer1 = "pool.ntp.org";          // NTP 伺服器 1
  const char* ntpServer2 = "time.nist.gov";         // NTP 伺服器 2
  const char* ntpServer3 = "time.stdtime.gov.tw";   // NTP 伺服器 3（台灣）
  long  gmtOffset_sec      = 28800;   // GMT 偏移秒數（28800 = GMT+8）
  int   daylightOffset_sec = 0;       // 日光節約時間偏移秒數

  // 每次校時的間隔，預設 1 小時
  unsigned long syncIntervalMs = 3600UL * 1000UL;

  // 首次同步時最多等待多久，避免卡住太久
  unsigned long initialSyncTimeoutMs = 10000UL;

  // 初始化：設定 NTP 伺服器並立即嘗試取得時間。
  void begin();

  // 於 loop() 中呼叫：
  // 1. 若尚未同步，嘗試同步一次
  // 2. 若已同步且超過一小時，重新校時一次
  void handle();

  // 立即執行一次 NTP 同步
  bool syncNow(bool printLog = true);

  // 取得目前已維持的時鐘時間（基於最後一次同步 + millis）
  time_t now();

  // 印出目前本地時間（兩種格式）
  void print();

  // 依指定格式取得時間字串（strftime 格式，例如 "%F %T"）
  String getDateTime(const char* format);

  // 是否已成功取得過時間資料
  bool isSynced();

 private:
  bool _synced = false;
  time_t _baseEpoch = 0;
  unsigned long _baseMillis = 0;
  unsigned long _lastSyncMs = 0;
};

// 全域單例
extern NTPClock NTP;

#endif  // NTPTIME_H
