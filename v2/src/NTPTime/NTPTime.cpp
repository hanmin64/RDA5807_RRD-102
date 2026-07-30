#include "NTPTime.h"

// 全域單例實體
NTPClock NTP;

// ------------------------------------------------------------
// 初始化：設定 NTP 伺服器，並立即嘗試取得時間
// ------------------------------------------------------------
void NTPClock::begin() {
  // configTime(GMT 偏移秒數, 日光節約偏移秒數, 伺服器網址...)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  Serial.println(F("[NTP] 已設定 NTP 伺服器，開始首次對時..."));

  // 啟動後立即同步一次，讓系統時鐘有初始值
  syncNow(true);
}

// ------------------------------------------------------------
// 每次 loop() 中呼叫，負責維持時鐘與定時校正
// ------------------------------------------------------------
void NTPClock::handle() {
  if (!_synced) {
    syncNow(true);
    return;
  }

  // 超過一小時後重新同步一次，避免長時間漂移
  if ((millis() - _lastSyncMs) >= syncIntervalMs) {
    Serial.println(F("[NTP] 超過一小時，重新校正時間..."));
    syncNow(true);
  }
}

// ------------------------------------------------------------
// 立即執行一次 NTP 同步
// ------------------------------------------------------------
bool NTPClock::syncNow(bool printLog) {
  // 重新設定 NTP 來源，確保每次同步都會重新查詢
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, initialSyncTimeoutMs)) {
    if (printLog) {
      Serial.println(F("[NTP] 無法取得時間資料，稍後再試"));
    }
    return false;
  }

  _baseEpoch = mktime(&timeinfo);
  _baseMillis = millis();
  _lastSyncMs = millis();
  _synced = true;

  if (printLog) {
    Serial.println(F("[NTP] 已完成時間同步"));
    Serial.print(F("[NTP] 目前時間: "));
    Serial.println(getDateTime("%Y-%m-%d %H:%M:%S"));
  }

  return true;
}

// ------------------------------------------------------------
// 取得目前時鐘時間（最後一次同步 + 已經過的秒數）
// ------------------------------------------------------------
time_t NTPClock::now() {
  if (!_synced) {
    return 0;
  }
  return _baseEpoch + (time_t)((millis() - _baseMillis) / 1000UL);
}

// ------------------------------------------------------------
// 印出目前本地時間（兩種常用格式）
// ------------------------------------------------------------
void NTPClock::print() {
  time_t currentTime = now();
  if (currentTime <= 0) {
    Serial.println(F("[NTP] 尚未取得時間資料"));
    return;
  }

  struct tm* timeinfo = localtime(&currentTime);
  if (timeinfo == nullptr) {
    Serial.println(F("[NTP] 時間格式轉換失敗"));
    return;
  }

  // %A 星期, %B 月份名 %d 日期 %Y 年 %H:%M:%S 時:分:秒
  Serial.println(&(*timeinfo), "[NTP] %A, %B %d %Y %H:%M:%S");
  // %F = YYYY-MM-DD, %r = 12 小時制 HH:MM:SS AM/PM
  Serial.println(&(*timeinfo), "[NTP] %F, %r");
}

// ------------------------------------------------------------
// 依指定格式取得時間字串（strftime 格式）
// ------------------------------------------------------------
String NTPClock::getDateTime(const char* format) {
  time_t currentTime = now();
  if (currentTime <= 0) {
    return "";  // 尚未對時
  }

  struct tm* timeinfo = localtime(&currentTime);
  if (timeinfo == nullptr) {
    return "";
  }

  char buf[64];
  strftime(buf, sizeof(buf), format, timeinfo);
  return String(buf);
}

// ------------------------------------------------------------
// 是否已成功對時
// ------------------------------------------------------------
bool NTPClock::isSynced() {
  return _synced;
}
