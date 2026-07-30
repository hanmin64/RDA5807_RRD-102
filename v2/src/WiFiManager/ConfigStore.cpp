#include "ConfigStore.h"  // 引入對應的標頭檔

#include <Preferences.h>  // ESP32 NVS 操作函式庫：提供 key-value 持久化儲存

// --- 靜態常數定義 ---
// Preferences 的命名空間名稱（相當於資料庫的 table 名稱）
const char* ConfigStore::NS_NAMESPACE = "wifi_cfg";
// 儲存 SSID 用的鍵名
const char* ConfigStore::KEY_SSID     = "ssid";
// 儲存密碼用的鍵名
const char* ConfigStore::KEY_PASS     = "pass";

// 全域單例實體
ConfigStore Config;

// ------------------------------------------------------------
// 檢查是否已有已儲存的 WiFi 憑證
// 回傳 true 表示 NVS 中存在 SSID 且不為空字串
// ------------------------------------------------------------
bool ConfigStore::hasCredentials() {
  Preferences prefs;                            // 建立 Preferences 物件
  prefs.begin(NS_NAMESPACE, true);              // 以唯讀模式開啟 "wifi_cfg" 命名空間
  // 檢查 KEY_SSID 是否存在且對應的值不為空字串
  bool has = prefs.isKey(KEY_SSID) && (prefs.getString(KEY_SSID, "").length() > 0);
  prefs.end();                                  // 關閉 Preferences，釋放資源
  return has;                                   // 回傳檢查結果
}

// ------------------------------------------------------------
// 取得已儲存的 SSID
// 若 NVS 中無資料則回傳空字串
// ------------------------------------------------------------
String ConfigStore::getSSID() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, true);              // 唯讀模式開啟
  String ssid = prefs.getString(KEY_SSID, "");   // 讀取 SSID，無資料則回傳 ""
  prefs.end();
  return ssid;
}

// ------------------------------------------------------------
// 取得已儲存的 WiFi 密碼
// 若 NVS 中無資料則回傳空字串
// ------------------------------------------------------------
String ConfigStore::getPassword() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, true);              // 唯讀模式開啟
  String pass = prefs.getString(KEY_PASS, "");   // 讀取密碼，無資料則回傳 ""
  prefs.end();
  return pass;
}

// ------------------------------------------------------------
// 儲存 WiFi 憑證到 NVS
// 參數 ssid：WiFi 名稱
// 參數 password：WiFi 密碼
// ------------------------------------------------------------
void ConfigStore::saveCredentials(const String& ssid, const String& password) {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, false);             // 讀寫模式開啟 false=readWrite
  prefs.putString(KEY_SSID, ssid);               // 寫入 SSID
  prefs.putString(KEY_PASS, password);           // 寫入密碼
  prefs.end();                                   // 關閉並確保資料寫入 NVS
}

// ------------------------------------------------------------
// 清除 NVS 中所有已儲存的憑證
// 使用 prefs.clear() 清除整個 "wifi_cfg" 命名空間下的所有鍵值
// ------------------------------------------------------------
void ConfigStore::clearCredentials() {
  Preferences prefs;
  prefs.begin(NS_NAMESPACE, false);             // 讀寫模式開啟
  prefs.clear();                                 // 清除整個命名空間
  prefs.end();
}
