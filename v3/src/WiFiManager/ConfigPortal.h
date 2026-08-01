#ifndef CONFIG_PORTAL_H   // 防止重複編譯
#define CONFIG_PORTAL_H   // 定義 CONFIG_PORTAL_H

#include <Arduino.h>      // Arduino 核心：提供 String 型別

// ============================================================
//  ConfigPortal — WiFi 配網入口（Captive Portal）
// ------------------------------------------------------------
//  設計理念：避免 AP+STA 同時運作（在 ESP32 Arduino core 上容易導致 panic）。
//  採用純 AP 架構，分階段進行配網。
//
//  工作流程：
//    1. 純 AP 模式開啟熱點 + DNS 攔截 + WebServer
//    2. 使用者連上 AP 後，任何瀏覽器請求都被 DNS 攔截導向 ESP32
//    3. 使用者從網頁選擇/輸入 SSID 與密碼並送出
//    4. 關閉 AP/DNS/WebServer → 切換為純 STA 模式 → 同步驗證連線
//    5. 連線成功 → 儲存憑證 → ESP.restart()
//    6. 連線失敗 → ESP.restart()（重啟後回到步驟 1 重新配網）
//
//  注意：start() 內部最終都會呼叫 ESP.restart()，不會正常 return。
//
//  使用方法：
//    ConfigPortal portal;
//    portal.start(apSSID, apPassword, timeoutMs);
//
//  全域單例 Portal 已建立於 ConfigPortal.cpp
// ============================================================
class ConfigPortal {     // 配網入口類別
 public:                  // 公開成員

  // 啟動配網入口 Captive Portal
  //    apSSID：ESP32 開放的 AP 名稱
  //    apPassword：AP 密碼（可為空字串表示無密碼）
  //    timeoutMs：配網逾時時間（0 = 永不逾時）
  //  注意：內部一定會呼叫 ESP.restart()，不會正常 return
  bool start(const String& apSSID, const String& apPassword = "", unsigned long timeoutMs = 0);

 private:                 // 私有成員

  // 處理 HTTP GET "/"：回傳配網頁面 HTML
  void handleRoot();

  // 處理 HTTP GET "/scan"：掃描可用 WiFi 並回傳 JSON 清單
  void handleScan();

  // 處理 HTTP POST "/connect"：接收 SSID/密碼，驗證連線後重啟
  void handleConnect();

  // 處理 HTTP POST "/reset"：清除已儲存的 WiFi 憑證
  void handleReset();

  // 處理所有未知路徑請求（Captive Portal 關鍵）：重導向到首頁
  void handleNotFound();

  // 關閉 AP、切換純 STA、嘗試連線、成功則儲存憑證
  // 連線結果決定後續要 restart() 重新配網或套用新設定
  bool verifyAndSave();

  // 將字串中的 HTML 特殊字元跳脫，防止 XSS 或格式破壞
  String escapeHTML(const String& s);

  // 產生配網頁面的完整 HTML 字串（含 CSS + JavaScript）
  String portalHTML();
};

// 全域單例（全域變數宣告），在 ConfigPortal.cpp 中定義實體
extern ConfigPortal Portal;

#endif  // CONFIG_PORTAL_H
