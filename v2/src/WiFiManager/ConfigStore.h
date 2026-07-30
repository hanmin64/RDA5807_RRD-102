#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <Arduino.h>

// 憑證儲存模組：使用 ESP32 的 NVS (Preferences) 持久化保存 WiFi 帳密
// 配網成功後寫入，下次開機可直接讀取連線，不需重新配網
class ConfigStore {
 public:
  // 檢查是否已有已儲存的憑證
  bool hasCredentials();
  // 取得已儲存的 SSID（無則回傳空字串）
  String getSSID();
  // 取得已儲存的密碼（無則回傳空字串）
  String getPassword();
  // 儲存一組新的憑證
  void saveCredentials(const String& ssid, const String& password);
  // 清除已儲存的憑證（恢復原廠配網狀態）
  void clearCredentials();

 private:
  static const char* NS_NAMESPACE;  // NVS 命名空間
  static const char* KEY_SSID;      // SSID 鍵名
  static const char* KEY_PASS;      // 密碼鍵名
};

// 全域單例，方便各處呼叫
extern ConfigStore Config;

#endif  // CONFIG_STORE_H
