#ifndef WIFI_MANAGER_H    // 防止重複編譯
#define WIFI_MANAGER_H    // 定義 WIFI_MANAGER_H

#include <Arduino.h>      // Arduino 核心：提供 String、Serial、pinMode、digitalRead 等
#include <WiFi.h>         // ESP32 WiFi 函式庫：提供 WiFi 模式設定、連線、狀態查詢

// ============================================================
//  WiFiManager — ESP32 WiFi 配網高階封裝
// ------------------------------------------------------------
//  將「自動連線 → 配網入口 → 斷線重連 → BOOT 鍵長按重置」全部封裝，
//  主程式只需以下三行即可完成所有 WiFi 管理：
//
//      WiFiMgr.begin();      // setup() 中呼叫，自動完成連線或進入配網模式
//      WiFiMgr.handle();     // loop() 中呼叫，處理斷線重連與重置鍵
//      WiFiMgr.isConnected() // 隨時查詢 WiFi 連線狀態
//
//  配網機制（Captive Portal）：
//    若無已存憑證或連線失敗，自動開啟 AP 熱點 + DNS 攔截 + WebServer，
//    使用者連上 ESP32 的 AP 後，開啟瀏覽器即可看到配網頁面。
//
//  重置機制：
//    長按 BOOT 鍵（GPIO0）3 秒，清除已存憑證並重新啟動。
//
//  全域單例 WiFiMgr 已建立於 WiFiManager.cpp
// ============================================================
class WiFiManager {      // WiFi 管理類別
 public:                  // 公開成員

  // --- 可調參數（於 begin() 前修改）---
  unsigned long autoConnectMs = 15000;  // 自動連線等待時間上限（毫秒），逾時視為失敗
  int          retryTimes     = 2;      // 自動連線失敗後的額外重試次數
  unsigned long portalTimeout = 0;      // 配網入口逾時時間（0 = 永不逾時）
  int          resetPin       = 0;      // 設定重置用的 GPIO 腳位（0 = GPIO0 = BOOT 鍵）

  // 初始化：嘗試自動連線（已存憑證）→ 失敗則進入配網入口（Captive Portal）
  // 配網成功後會自動 ESP.restart()，下次開機即可用新憑證連線
  void begin();

  // 主迴圈處理（需在 loop() 中定期呼叫）：
  // 1. 若 WiFi 斷線，自動嘗試重連
  // 2. 檢查 BOOT 鍵是否長按 3 秒（清除設定並重啟）
  void handle();

  // 查詢目前 WiFi 是否已連線
  bool isConnected() { return WiFi.status() == WL_CONNECTED; }

  // 清除已儲存的 WiFi 憑證（恢復出廠配網狀態）
  void clearCredentials();

 private:                 // 私有成員

  bool _wasConnected = false;  // 記錄上一次檢查時是否處於連線狀態（用於斷線時只印一次訊息）
  bool _firstDone    = false;  // 標記是否已完成首次初始化

  // 使用已儲存的 SSID/密碼嘗試自動連線
  bool tryAutoConnect();

  // 嘗試連線 Wokwi 模擬器提供的內建虛擬 WiFi（Wokwi-GUEST）
  bool tryWokwiGuestConnect();

  // 啟動 Captive Portal 配網入口（純 AP 模式）
  bool runPortal();

  // 檢查重置按鈕是否長按 3 秒，若是則清除憑證並重啟
  void checkResetButton();
};

// 全域單例（全域變數宣告），在 WiFiManager.cpp 中定義實體
extern WiFiManager WiFiMgr;

#endif  // WIFI_MANAGER_H
