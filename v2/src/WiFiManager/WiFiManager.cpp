#include "WiFiManager.h"  // 引入對應的標頭檔

#include <WiFi.h>          // ESP32 WiFi 函式庫
#include <Adafruit_SSD1306.h> // OLED 驅動（用於在配網過程顯示狀態訊息）

#include "ConfigStore.h"   // 憑證儲存模組：讀寫 WiFi SSID/密碼到 NVS
#include "ConfigPortal.h"  // 配網入口模組：Captive Portal 實作

extern Adafruit_SSD1306 display;  // 引用 main.cpp 中的全域 OLED 顯示物件

// ------------------------------------------------------------
// 在 OLED 上顯示 WiFi 連線中的訊息
// line：第一行顯示的文字
// ------------------------------------------------------------
static void showConnectingOnOled(const String& line) {
  display.clearDisplay();                          // 清除 OLED 畫面緩衝區
  display.setTextColor(WHITE);                     // 設定文字顏色為白色
  display.setTextSize(1);                          // 使用最小字體（6x8 像素）
  display.setCursor(0, 0);                         // 將游標設到左上角
  display.println(line);                           // 印出第一行：連線狀態
  display.setCursor(0, 20);                        // 游標移到第 3 行
  display.println(F("Please wait..."));            // 顯示「請稍候」
  display.display();                               // 將緩衝區內容刷新到 OLED
}

// 全域單例實體
WiFiManager WiFiMgr;

// ------------------------------------------------------------
// 初始化：自動連線 → 失敗則進入配網入口
// 此函式為阻塞式（blocking），會等到連線成功或配網完成才回傳
// ------------------------------------------------------------
void WiFiManager::begin() {
  Serial.println(F("[WM] 啟動 WiFi 配網"));   // 序列埠輸出啟動訊息
  pinMode(resetPin, INPUT_PULLUP);               // 設定重置腳位為輸入並啟用內部上拉電阻

  bool connected = false;  // 連線旗標

  // 在 OLED 上顯示連線中
  showConnectingOnOled(F("*WiFi Connecting...*"));

  // 階段 1：用已儲存的 WiFi 憑證自動連線
  // 最多重試 retryTimes 次（第 1 次 + retry 次）
  for (int i = 0; i <= retryTimes && !connected; ++i) {
    if (i > 0) {  // 每次重試時輸出提示
      Serial.print(F("[WM] 重試 ("));
      Serial.print(i);
      Serial.println(F(")"));
      delay(500);  // 重試前短暫等待
    }
    connected = tryAutoConnect();  // 嘗試自動連線
  }

  // 階段 2：若無已存憑證，嘗試連線 Wokwi 虛擬 WiFi（僅在 Wokwi 模擬器環境有效）
  if (!connected) {
    connected = tryWokwiGuestConnect();
  }

  // 階段 3：所有連線方式都失敗 → 進入 Captive Portal 配網模式
  // 進入後會開啟 AP + DNS + WebServer，直到使用者完成配網
  // runPortal() 內部最終會呼叫 ESP.restart()，不會正常 return
  while (!connected) {
    connected = runPortal();  // 啟動配網入口（阻塞式）
    if (!connected) {
      Serial.println(F("[WM] 配網未完成，5 秒後再試"));
      delay(5000);  // 若配網失敗（理論上不會），等待 5 秒後重啟入口
    }
  }

  Serial.println(F("[WM] WiFi 就緒"));  // 連線成功
  _wasConnected = true;                 // 記錄已連線
}

// ------------------------------------------------------------
// 主迴圈處理：需在 loop() 中定期呼叫
// 負責：斷線自動重連 + 重置鍵長按檢查
// ------------------------------------------------------------
void WiFiManager::handle() {
  // --- 斷線自動重連 ---
  if (WiFi.status() != WL_CONNECTED) {  // 偵測到 WiFi 斷線
    if (_wasConnected) {                  // 若之前是連線狀態，印出斷線訊息
      Serial.println(F("[WM] 連線中斷，嘗試重連..."));
      _wasConnected = false;             // 更新狀態
    }
    // 若有已儲存憑證，嘗試重新連線
    if (Config.hasCredentials()) {
      showConnectingOnOled(F("*WiFi Connecting...*"));  // OLED 顯示重連中
      WiFi.reconnect();                                  // 使用上次的憑證重新連線
      delay(2000);                                       // 等待 2 秒讓連線建立
      if (WiFi.status() == WL_CONNECTED) {               // 檢查是否連線成功
        Serial.print(F("[WM] 已重新連線，IP: "));
        Serial.println(WiFi.localIP());
        _wasConnected = true;
      }
    }
  }

  // --- 長按 BOOT 鍵 3 秒重置 WiFi 設定 ---
  checkResetButton();
}

// ------------------------------------------------------------
// 清除已存 WiFi 憑證
// ------------------------------------------------------------
void WiFiManager::clearCredentials() {
  Config.clearCredentials();  // 委託 ConfigStore 清除 NVS 中的憑證
  Serial.println(F("[WM] 已清除憑證"));
}

// ------------------------------------------------------------
// 使用已儲存的 SSID/密碼嘗試自動連線
// 回傳 true 表示連線成功
// ------------------------------------------------------------
bool WiFiManager::tryAutoConnect() {
  // 檢查是否有已儲存的憑證
  if (!Config.hasCredentials()) {
    Serial.println(F("[WM] 無已儲存憑證，跳過自動連線"));
    return false;
  }

  // 從 NVS 讀取 SSID 與密碼
  String ssid = Config.getSSID();
  String pass = Config.getPassword();
  Serial.print(F("[WM] 嘗試自動連線到: "));
  Serial.println(ssid);

  // 設定為工作站（STA）模式並開始連線
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  // 等待連線完成（最多 autoConnectMs 毫秒）
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > autoConnectMs) {
      Serial.println(F("\n[WM] 自動連線逾時"));
      return false;  // 逾時，連線失敗
    }
    delay(300);       // 每 300ms 檢查一次
    Serial.print("."); // 在序列埠輸出進度點
  }

  // 連線成功：輸出網路設定資訊
  Serial.println();
  Serial.println(F("[WM] CONNECTED!"));
  Serial.print(F("[WM] SSID: "));
  Serial.println(WiFi.SSID());
  Serial.print(F("[WM] IP: "));
  Serial.println(WiFi.localIP());          // 本機 IP 位址
  Serial.print(F("[WM] Subnet Mask IP: "));
  Serial.println(WiFi.subnetMask());       // 子網路遮罩
  Serial.print(F("[WM] Gateway IP: "));
  Serial.println(WiFi.gatewayIP());        // 預設閘道
  Serial.print(F("[WM] DNS IP: "));
  Serial.println(WiFi.dnsIP());            // DNS 伺服器
  return true;  // 連線成功
}

// ------------------------------------------------------------
// 嘗試連線 Wokwi 模擬器內建的虛擬 WiFi（Wokwi-GUEST）
// 僅在 Wokwi 線上模擬環境中有效，實體硬體會跳過
// ------------------------------------------------------------
bool WiFiManager::tryWokwiGuestConnect() {
  Serial.println(F("[WM] 嘗試連上 Wokwi 的虛擬 WiFi (Wokwi-GUEST)"));
  WiFi.mode(WIFI_STA);
  // Wokwi 的內建 WiFi 無密碼，頻道為 6
  WiFi.begin("Wokwi-GUEST", "", 6);

  // 等待 8 秒看是否能連上
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 8000) {
      Serial.println(F("\n[WM] Wokwi WiFi 連線逾時"));
      return false;
    }
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.println(F("[WM] Wokwi WiFi CONNECTED!"));
  Serial.print(F("[WM] SSID: "));
  Serial.println(WiFi.SSID());
  Serial.print(F("[WM] IP: "));
  Serial.println(WiFi.localIP());
  return true;
}

// ------------------------------------------------------------
// 進入配網入口模式（Captive Portal）
// 會開啟 AP + DNS 攔截 + WebServer，讓使用者透過瀏覽器設定 WiFi
// ------------------------------------------------------------
bool WiFiManager::runPortal() {
  // 根據 ESP32 晶片的 MAC 位址產生唯一的 AP 名稱
  uint64_t chipid = ESP.getEfuseMac();           // 讀取晶片的 eFuse MAC 位址
  String apSSID = "ESP32_Setup_" + String((uint16_t)(chipid >> 32), HEX);  // 取後 16bit 轉十六進位
  apSSID.toUpperCase();                           // 轉為大寫字母
  String apPass = "";                             // AP 密碼（空字串 = 無密碼）

  Serial.println(F("[WM] 進入配網模式"));
  showConnectingOnOled(F("*WiFi Connecting...*")); // OLED 顯示提示
  // 啟動 Portal（內部會阻塞直到配網完成或逾時，最終 ESP.restart()）
  return Portal.start(apSSID, apPass, portalTimeout);
}

// ------------------------------------------------------------
// 重置鍵長按檢查
// 若 resetPin 被拉低（按鍵按下）超過 3 秒，清除 WiFi 憑證並重啟
// ------------------------------------------------------------
void WiFiManager::checkResetButton() {
  static unsigned long pressedAt = 0;  // 記錄按鍵開始按下的時間（static 保留跨呼叫的值）
  bool pressed = (digitalRead(resetPin) == LOW);  // 讀取按鍵狀態（低電位 = 按下）

  if (pressed && pressedAt == 0) {
    // 按鍵剛被按下：記錄按下的時間戳記
    pressedAt = millis();
  } else if (pressed && pressedAt && (millis() - pressedAt) > 3000) {
    // 按鍵持續按住超過 3 秒：執行清除並重啟
    Serial.println(F("\n[WM] 偵測到長按重置鍵，清除設定並重啟..."));
    Config.clearCredentials();  // 清除 NVS 中的 WiFi 憑證
    delay(200);
    ESP.restart();              // 重新啟動 ESP32（重啟後會進入配網模式）
  } else if (!pressed) {
    // 按鍵已放開：重置計時器
    pressedAt = 0;
  }
}
