#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <Arduino.h>

// WiFi 配網入口（Captive Portal）— 純 AP 架構
//
// 設計理念：避免 AP+STA 同時運作（在 ESP32 Arduino core 上容易 panic）。
//
// 工作流程：
//   1. 純 AP 模式開熱點 + DNS 攔截 + WebServer，提供登錄頁面
//   2. 使用者送出 SSID/密碼 → 關閉 AP/DNS/Web → 切純 STA → 同步驗證連線
//   3. 連線成功 → 儲存憑證 → ESP.restart()
//   4. 連線失敗 → ESP.restart()（重啟後回到步驟 1 重新配網）
//
// 注意：start() 內部最終都會 ESP.restart()，不會正常 return。
//
// 使用方法：
//   ConfigPortal portal;
//   portal.start(apSSID, apPassword, timeoutMs);
class ConfigPortal {
 public:
  // 啟動配網入口。內部最終會 ESP.restart()，不會正常 return。
  bool start(const String& apSSID, const String& apPassword = "", unsigned long timeoutMs = 0);

 private:
  void   handleRoot();
  void   handleScan();        // 回傳可用的 WiFi 清單 (JSON)
  void   handleConnect();     // 接收 SSID/密碼 → 驗證連線 → 重啟
  void   handleReset();       // 清除已存憑證
  void   handleNotFound();    // 攔截所有未知請求 → 跳轉到根目錄（Captive Portal 關鍵）
  bool   verifyAndSave();     // 關閉 AP、切純 STA、驗證連線、成功則儲存憑證
  String escapeHTML(const String& s);
  String portalHTML();
};

// 全域單例
extern ConfigPortal Portal;

#endif  // CONFIG_PORTAL_H
