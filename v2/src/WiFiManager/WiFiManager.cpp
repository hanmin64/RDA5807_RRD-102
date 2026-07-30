#include "WiFiManager.h"

#include <WiFi.h>
#include <Adafruit_SSD1306.h>

#include "ConfigStore.h"
#include "ConfigPortal.h"

extern Adafruit_SSD1306 display;

static void showConnectingOnOled(const String& line) {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line);
  display.setCursor(0, 20);
  display.println(F("Please wait..."));
  display.display();
}

// 全域單例實體
WiFiManager WiFiMgr;

// ------------------------------------------------------------
// 初始化：自動連線 → 失敗則配網
// ------------------------------------------------------------
void WiFiManager::begin() {
  Serial.println(F("[WM] 啟動 WiFi 配網"));
  pinMode(resetPin, INPUT_PULLUP);

  bool connected = false;

  showConnectingOnOled(F("*WiFi Connecting...*"));

  // 1) 先嘗試用已存憑證自動連線（最多重試 retryTimes 次）
  for (int i = 0; i <= retryTimes && !connected; ++i) {
    if (i > 0) {
      Serial.print(F("[WM] 重試 ("));
      Serial.print(i);
      Serial.println(F(")"));
      delay(500);
    }
    connected = tryAutoConnect();
  }

  // 2) 若沒有已存憑證，先嘗試 Wokwi 的內建虛擬 WiFi（Wokwi-GUEST）
  if (!connected) {
    connected = tryWokwiGuestConnect();
  }

  // 3) 連不上則進入配網入口（內部會 ESP.restart()）
  while (!connected) {
    connected = runPortal();
    if (!connected) {
      Serial.println(F("[WM] 配網未完成，5 秒後再試"));
      delay(5000);
    }
  }

  Serial.println(F("[WM] WiFi 就緒"));
  _wasConnected = true;
}

// ------------------------------------------------------------
// 主迴圈：斷線重連 + 重置鍵檢查
// ------------------------------------------------------------
void WiFiManager::handle() {
  // 斷線自動重連
  if (WiFi.status() != WL_CONNECTED) {
    if (_wasConnected) {
      Serial.println(F("[WM] 連線中斷，嘗試重連..."));
      _wasConnected = false;
    }
    if (Config.hasCredentials()) {
      showConnectingOnOled(F("*WiFi Connecting...*"));
      WiFi.reconnect();
      delay(2000);
      if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("[WM] 已重新連線，IP: "));
        Serial.println(WiFi.localIP());
        _wasConnected = true;
      }
    }
  }

  // 長按重置鍵清除設定
  checkResetButton();
}

// ------------------------------------------------------------
// 清除已存憑證
// ------------------------------------------------------------
void WiFiManager::clearCredentials() {
  Config.clearCredentials();
  Serial.println(F("[WM] 已清除憑證"));
}

// ------------------------------------------------------------
// 用已存憑證自動連線
// ------------------------------------------------------------
bool WiFiManager::tryAutoConnect() {
  if (!Config.hasCredentials()) {
    Serial.println(F("[WM] 無已儲存憑證，跳過自動連線"));
    return false;
  }

  String ssid = Config.getSSID();
  String pass = Config.getPassword();
  Serial.print(F("[WM] 嘗試自動連線到: "));
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > autoConnectMs) {
      Serial.println(F("\n[WM] 自動連線逾時"));
      return false;
    }
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.println(F("[WM] CONNECTED!"));
  Serial.print(F("[WM] SSID: "));
  Serial.println(WiFi.SSID());
  Serial.print(F("[WM] IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("[WM] Subnet Mask IP: "));
  Serial.println(WiFi.subnetMask());
  Serial.print(F("[WM] Gateway IP: "));
  Serial.println(WiFi.gatewayIP());
  Serial.print(F("[WM] DNS IP: "));
  Serial.println(WiFi.dnsIP());
  return true;
}

// ------------------------------------------------------------
// 嘗試連上 Wokwi 內建的虛擬 WiFi（Wokwi-GUEST）
// ------------------------------------------------------------
bool WiFiManager::tryWokwiGuestConnect() {
  Serial.println(F("[WM] 嘗試連上 Wokwi 的虛擬 WiFi (Wokwi-GUEST)"));
  WiFi.mode(WIFI_STA);
  WiFi.begin("Wokwi-GUEST", "", 6);

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
// 進入配網入口
// ------------------------------------------------------------
bool WiFiManager::runPortal() {
  uint64_t chipid = ESP.getEfuseMac();
  String apSSID = "ESP32_Setup_" + String((uint16_t)(chipid >> 32), HEX);
  apSSID.toUpperCase();
  String apPass = "";

  Serial.println(F("[WM] 進入配網模式"));
  showConnectingOnOled(F("*WiFi Connecting...*"));
  return Portal.start(apSSID, apPass, portalTimeout);  // 內部會 ESP.restart()
}

// ------------------------------------------------------------
// 重置鍵長按檢查（清除設定並重啟）
// ------------------------------------------------------------
void WiFiManager::checkResetButton() {
  static unsigned long pressedAt = 0;
  bool pressed = (digitalRead(resetPin) == LOW);

  if (pressed && pressedAt == 0) {
    pressedAt = millis();
  } else if (pressed && pressedAt && (millis() - pressedAt) > 3000) {
    Serial.println(F("\n[WM] 偵測到長按重置鍵，清除設定並重啟..."));
    Config.clearCredentials();
    delay(200);
    ESP.restart();
  } else if (!pressed) {
    pressedAt = 0;
  }
}
