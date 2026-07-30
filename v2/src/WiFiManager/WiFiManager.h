#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

// ============================================================
//  WiFiManager — ESP32 配網高階封裝
// ------------------------------------------------------------
//  將「自動連線 → 配網入口 → 斷線重連 → BOOT 鍵重置」全部封裝，
//  主程式只需：
//      #include "WiFiManager.h"
//      WiFiManager wm;
//      wm.begin();           // setup() 中呼叫，自動完成連線/配網
//      wm.handle();          // loop() 中呼叫，處理重連與重置鍵
//
//  全域單例 WiFiMgr 已建立，可直接使用 WiFiMgr.begin() / .handle()
// ============================================================
class WiFiManager {
 public:
  // 設定參數（可於 begin() 前修改）
  unsigned long autoConnectMs = 15000;  // 自動連線等待時間
  int          retryTimes     = 2;      // 連線失敗重試次數
  unsigned long portalTimeout = 0;      // 配網入口逾時（0=不逾時）
  int          resetPin       = 0;      // 重置鍵 GPIO（BOOT 鍵）

  // 初始化：嘗試自動連線，失敗則進入配網入口。
  // 配網成功後內部會 ESP.restart()，重啟後用新憑證自動連線。
  void begin();

  // 主迴圈處理：斷線自動重連 + 重置鍵長按檢查。請在 loop() 呼叫。
  void handle();

  // 是否已連線
  bool isConnected() { return WiFi.status() == WL_CONNECTED; }

  // 清除已存憑證（恢復原廠配網狀態）
  void clearCredentials();

 private:
  bool _wasConnected = false;
  bool _firstDone    = false;

  bool tryAutoConnect();
  bool tryWokwiGuestConnect();
  bool runPortal();
  void checkResetButton();
};

// 全域單例
extern WiFiManager WiFiMgr;

#endif  // WIFI_MANAGER_H
