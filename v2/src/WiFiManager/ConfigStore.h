#ifndef CONFIG_STORE_H    // 防止重複編譯
#define CONFIG_STORE_H    // 定義 CONFIG_STORE_H

#include <Arduino.h>      // Arduino 核心：提供 String 型別

// ============================================================
//  ConfigStore — WiFi 憑證持久化儲存模組
// ------------------------------------------------------------
//  使用 ESP32 的 NVS（Non-Volatile Storage / Preferences）儲存 WiFi 帳密，
//  配網成功後寫入，下次開機可直接讀取連線，不需重新配網。
//
//  儲存結構（NVS 命名空間 "wifi_cfg"）：
//    Key "ssid" → WiFi 名稱（字串）
//    Key "pass" → WiFi 密碼（字串）
//
//  使用方法：
//    Config.saveCredentials("MyWiFi", "password123");  // 儲存
//    Config.getSSID();                                 // 讀取 SSID
//    Config.hasCredentials();                          // 檢查是否有憑證
//    Config.clearCredentials();                        // 清除
//
//  全域單例 Config 已建立於 ConfigStore.cpp
// ============================================================
class ConfigStore {      // 憑證儲存類別
 public:                  // 公開成員

  // 檢查 NVS 中是否已有已儲存的 WiFi 憑證（SSID 不為空）
  bool hasCredentials();

  // 取得已儲存的 SSID（若無則回傳空字串）
  String getSSID();

  // 取得已儲存的密碼（若無則回傳空字串）
  String getPassword();

  // 儲存一組新的 WiFi 憑證到 NVS
  void saveCredentials(const String& ssid, const String& password);

  // 清除 NVS 中所有已儲存的憑證（用於恢復出廠配網狀態）
  void clearCredentials();

 private:                 // 私有成員

  // NVS 命名空間名稱（固定為 "wifi_cfg"）
  static const char* NS_NAMESPACE;
  // 儲存 SSID 的鍵名（固定為 "ssid"）
  static const char* KEY_SSID;
  // 儲存密碼的鍵名（固定為 "pass"）
  static const char* KEY_PASS;
};

// 全域單例（全域變數宣告），在 ConfigStore.cpp 中定義實體
extern ConfigStore Config;

#endif  // CONFIG_STORE_H
