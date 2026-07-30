#ifndef NTPTIME_H      // 防止重複編譯：若未定義 NTPTIME_H 則編譯以下內容
#define NTPTIME_H      // 定義 NTPTIME_H，確保此標頭檔只被編譯一次

#include <Arduino.h>   // Arduino 核心函式庫：提供 String、Serial 等基礎型別與函式
#include <time.h>      // C 標準時間函式庫：提供 tm 結構體、time_t、strftime、mktime 等

// ============================================================
//  NTPClock — ESP32 NTP 網路對時封裝
// ------------------------------------------------------------
//  功能說明：
//    1. 開機後連上 WiFi 後取得一次 NTP 時間
//    2. 之後以 millis() 維持本地時鐘，每秒可更新顯示
//    3. 每一小時重新校正一次 NTP 時間
//
//  使用方式：
//    NTP.begin();          // setup() 中呼叫，初始化 NTP 設定並立即同步
//    NTP.handle();         // loop() 中呼叫，負責每小時校時
//    NTP.getDateTime("%H:%M:%S"); // 取得格式化時間字串
// ============================================================
class NTPClock {         // NTP 時鐘類別，封裝所有網路對時相關邏輯
 public:                  // 公開成員：外部可存取的函式與變數

  // 設定參數（可於 begin() 前修改）
  const char* ntpServer1 = "pool.ntp.org";          // NTP 伺服器 1（全球 pool，自動分配）
  const char* ntpServer2 = "time.nist.gov";         // NTP 伺服器 2（美國國家標準技術局）
  const char* ntpServer3 = "time.stdtime.gov.tw";   // NTP 伺服器 3（台灣國家時間標準）
  long  gmtOffset_sec      = 28800;   // GMT 偏移秒數（28800 秒 = GMT+8 = 台灣/北京時間）
  int   daylightOffset_sec = 0;       // 日光節約時間偏移秒數（台灣未實施，設為 0）

  // 每次校時的間隔，預設 1 小時（3600 秒 * 1000 毫秒）
  unsigned long syncIntervalMs = 3600UL * 1000UL;

  // 首次同步時最多等待多久（毫秒），避免網路問題導致卡住太久
  unsigned long initialSyncTimeoutMs = 10000UL;

  // 初始化：設定 NTP 伺服器並立即嘗試取得時間，應在 setup() 中呼叫
  void begin();

  // 於 loop() 中呼叫，負責：
  // 1. 若尚未同步，嘗試同步一次
  // 2. 若已同步且超過一小時，重新校時一次以補償晶體漂移
  void handle();

  // 立即執行一次 NTP 同步，回傳 true 表示成功
  // printLog = true 時會在序列埠輸出同步過程
  bool syncNow(bool printLog = true);

  // 取得目前已維持的時鐘時間（基於最後一次 NTP 同步 + millis() 推算）
  // 回傳 time_t（從 1970-01-01 至今的秒數）
  time_t now();

  // 依指定 strftime 格式取得時間字串，例如 "%F %T" → "2026-07-30 14:30:00"
  String getDateTime(const char* format);

  // 是否已成功取得過時間資料（可用於 UI 判斷是否顯示時鐘）
  bool isSynced();

 private:                 // 私有成員：僅類別內部可存取

  bool _synced = false;            // 同步旗標：true 表示至少成功取得過一次 NTP 時間
  time_t _baseEpoch = 0;           // 最後一次成功同步時的 Unix epoch 時間戳（秒）
  unsigned long _baseMillis = 0;   // 最後一次成功同步時的 millis() 值（毫秒）
  unsigned long _lastSyncMs = 0;   // 最後一次嘗試同步的 millis() 值（用於定時校時）
};

// 全域單例（全域變數宣告），在 NTPTime.cpp 中定義實體
// 主程式可直接使用 NTP.begin()、NTP.handle()、NTP.getDateTime() 等
extern NTPClock NTP;

#endif  // NTPTIME_H    // 條件編譯結束
