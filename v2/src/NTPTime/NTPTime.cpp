#include "NTPTime.h"   // 引入對應的標頭檔，確保類別宣告與實作一致

// 全域單例實體：所有主程式檔案都可透過 NTP 變數存取此物件
// extern 宣告在 NTPTime.h 中，此處為實際的定義
NTPClock NTP;

// ------------------------------------------------------------
// 初始化：設定 NTP 伺服器位址，並立即嘗試取得時間
// 應在 WiFi 已連線後呼叫（通常在 setup() 中）
// ------------------------------------------------------------
void NTPClock::begin() {
  // configTime(GMT偏移秒數, 日光節約偏移秒數, 主要伺服器, 備用伺服器1, 備用伺服器2)
  // 此函式由 ESP32 Arduino core 提供，設定全域系統時間的 NTP 來源
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  Serial.println(F("[NTP] 已設定 NTP 伺服器，開始首次對時..."));  // F() 將字串儲存在 Flash 以節省 RAM

  // 啟動後立即同步一次，讓系統時鐘有初始值
  syncNow(true);
}

// ------------------------------------------------------------
// 每次 loop() 中呼叫，負責維持時鐘與定時校正
// ------------------------------------------------------------
void NTPClock::handle() {
  // 若尚未同步過，持續嘗試直到成功
  if (!_synced) {
    syncNow(true);
    return;  // 離開函式，下次 loop() 再試
  }

  // 若已同步，檢查是否超過一小時未校正
  // 因 ESP32 的晶體震盪器有漂移，長時間不校正會產生秒差
  if ((millis() - _lastSyncMs) >= syncIntervalMs) {
    Serial.println(F("[NTP] 超過一小時，重新校正時間..."));
    syncNow(true);  // 重新 NTP 查詢，更新 _baseEpoch 與 _baseMillis
  }
}

// ------------------------------------------------------------
// 立即執行一次 NTP 同步
// 回傳 true 表示成功取得 NTP 時間
// ------------------------------------------------------------
bool NTPClock::syncNow(bool printLog) {
  // 重新設定 NTP 來源（ESP32 實作需求），確保每次同步都會重新查詢 DNS
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);

  struct tm timeinfo;  // 宣告 tm 結構，用於存放分解後的日期時間
  // getLocalTime(&timeinfo, timeoutMs)：等待 NTP 回應，timeout 後若無回應回傳 false
  if (!getLocalTime(&timeinfo, initialSyncTimeoutMs)) {
    if (printLog) {
      Serial.println(F("[NTP] 無法取得時間資料，稍後再試"));
    }
    return false;  // 同步失敗，保留之前的 _synced 狀態
  }

  // 同步成功：將 tm 結構轉為 time_t（Unix epoch 秒數）並記錄基準點
  _baseEpoch = mktime(&timeinfo);       // 轉換為自 1970-01-01 以來的秒數
  _baseMillis = millis();               // 記錄此刻的 millis() 作為後續推算基準
  _lastSyncMs = millis();               // 更新「最後同步時間」用於定時校時
  _synced = true;                       // 標記為已同步

  if (printLog) {
    Serial.println(F("[NTP] 已完成時間同步"));
    Serial.print(F("[NTP] 目前時間: "));
    Serial.println(getDateTime("%Y-%m-%d %H:%M:%S"));
  }

  return true;  // 同步成功
}

// ------------------------------------------------------------
// 取得目前時鐘時間（最後一次 NTP 同步 + 已經過的秒數）
// 不需每次都發 NTP 請求，用 millis() 差值推算即可
// ------------------------------------------------------------
time_t NTPClock::now() {
  if (!_synced) {
    return 0;  // 尚未同步，回傳 0 表示無效時間
  }
  // 目前時間 = 最後同步時的 epoch + (現在 - 基準毫秒數) / 1000
  return _baseEpoch + (time_t)((millis() - _baseMillis) / 1000UL);
}

// ------------------------------------------------------------
// 依指定 strftime 格式取得時間字串
// 例如 getDateTime("%H:%M:%S") → "14:30:00"
// ------------------------------------------------------------
String NTPClock::getDateTime(const char* format) {
  time_t currentTime = now();  // 取得目前時間
  if (currentTime <= 0) {
    return "";  // 尚未對時，回傳空字串
  }

  // 轉換為 tm 結構
  struct tm* timeinfo = localtime(&currentTime);
  if (timeinfo == nullptr) {
    return "";  // 轉換失敗
  }

  // strftime：將 tm 依 format 格式化寫入 buf
  char buf[64];                               // 64 bytes 足夠容納大多數時間格式
  strftime(buf, sizeof(buf), format, timeinfo); // 格式化時間字串
  return String(buf);                          // 轉為 Arduino String 回傳
}

// ------------------------------------------------------------
// 是否已成功對時（供外部判斷用）
// ------------------------------------------------------------
bool NTPClock::isSynced() {
  return _synced;  // 回傳同步旗標
}
