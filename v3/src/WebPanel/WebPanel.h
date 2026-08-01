/*
 * WebPanel.h — RRD-102 ESP32 FM Radio v3 之 Web 遠端遙控面板
 * ==========================================================
 * 於 ESP32 上啟動一個 WebServer (port 80)，提供：
 *   - GET  /              手機/電腦用遙控面板 (HTML + JS 單一檔案)
 *   - GET  /api/status    以 JSON 回報收音機目前狀態 (供前端輪詢)
 *   - POST /api/freq      設定頻率 (freq=x10，如 8750 = 87.5MHz)
 *   - POST /api/vol       設定音量 (vol=0~15)
 *   - POST /api/cmd       執行命令 (scan/cancel/mode/next/prev/...)
 *   - POST /update        OTA 無線韌體更新 (multipart 上傳 .bin)
 *   - mDNS                http://esp32-radio.local 方便記憶的網址
 *
 * 依賴：WiFi 需已連線 (於 WiFiMgr.begin() 之後呼叫 Web.begin())。
 *       控制收音機透過 RadioControl.h 的命令 API，不直接操作硬體。
 */

#ifndef WEB_PANEL_H
#define WEB_PANEL_H

#include <Arduino.h>
#include <WebServer.h>

class WebPanelMgr {
 public:
  // 啟動 mDNS + WebServer (需在 WiFi 連線後呼叫)
  void begin();

  // 於 loop() 中定期呼叫，處理 HTTP 請求
  void handle();

  // 是否正在進行 OTA 更新 (供主程式避免中斷)
  bool isUpdating() const { return _updating; }

 private:
  WebServer _server{80};   // HTTP WebServer (port 80)
  bool _updating = false;  // OTA 進行中旗標

  // --- HTTP 請求處理 ---
  void handleRoot();        // GET /    遙控面板頁面
  void handleStatus();      // GET /api/status
  void handleSetFreq();     // POST /api/freq
  void handleSetVol();      // POST /api/vol
  void handleCommand();     // POST /api/cmd
  void handleUpdate();      // POST /update (OTA)
  void handleUpdateUpload();// POST /update 上傳分段接收 (OTA)
  void handleNotFound();    // 其他路徑 → 404

  // 建立狀態 JSON 字串
  String buildStatusJSON();

  // 遙控面板頁面 HTML (含 CSS + JavaScript)
  String uiHTML();
};

// 全域單例 (定義於 WebPanel.cpp)
extern WebPanelMgr Web;

#endif  // WEB_PANEL_H
