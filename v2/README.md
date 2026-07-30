# RRD-102 ESP32 FM Radio + WiFi/NTP/Weather v2

NodeMCU-32S + RDA5807(RRD-102) + SSD1306 OLED 之 FM 收音機 — v2 新增網路功能

## v2 新增功能

| 功能 | 說明 |
|------|------|
| **WiFi 配網 (Captive Portal)** | 開機自動連線，無憑證時開啟 AP 讓手機設定，長按 BOOT 鍵 3 秒重置 |
| **NTP 網路對時** | 開機自動同步，每小時校正一次，OLED 大字顯示日期與時間 |
| **OpenWeatherMap 天氣** | 每 30 秒更新，顯示溫度、濕度及圖形化天氣圖示 |
| **OLED 雙頁輪播** | 時鐘頁 ↔ 天氣頁每 6 秒切換，FM 頻率固定顯示於頂部 |

## 硬體接線

與 v1 完全共用，接線不變：

| ESP32 腳位 | 連接對象 |
|-------------|---------|
| GPIO21 (SDA) | OLED SDA、RRD-102 SDA |
| GPIO22 (SCL) | OLED SCL、RRD-102 SCL |
| GPIO34 | 頻率電位器中間抽頭 |
| GPIO35 | 音量電位器中間抽頭 |
| GPIO19 | 上方按鈕 (另一端接 GND) |
| GPIO18 | 下方按鈕 (另一端接 GND) |
| GPIO0 (BOOT) | WiFi 重置鍵 (長按 3 秒清除憑證) |
| 3.3V | OLED VCC、RRD-102 VCC |
| GND | 所有元件 GND 共接 |
| Vin (5V) | DC-DC 升壓模組輸出 |

> 電源與音訊細節請參閱 v1 README。

## 操作方式

與 v1 相同，請參閱 v1 README 的「操作方式」與「模式」章節。

## 專案結構

```
v2/
├── platformio.ini           # PlatformIO 專案設定
├── src/
│   ├── main.cpp             # 主程式 (含 FM 收音機、UI、選單、掃描)
│   ├── NTPTime/
│   │   ├── NTPTime.h        # NTP 網路對時類別宣告
│   │   └── NTPTime.cpp      # NTP 網路對時實作
│   ├── Weather/
│   │   ├── Weather.h        # OpenWeatherMap 天氣查詢類別宣告
│   │   └── Weather.cpp      # OpenWeatherMap 天氣查詢實作
│   └── WiFiManager/
│       ├── WiFiManager.h    # WiFi 管理類別宣告
│       ├── WiFiManager.cpp  # WiFi 管理實作 (自動連線、配網入口)
│       ├── ConfigStore.h    # WiFi 憑證 NVS 儲存類別宣告
│       ├── ConfigStore.cpp  # WiFi 憑證 NVS 儲存實作
│       ├── ConfigPortal.h   # Captive Portal 配網入口類別宣告
│       └── ConfigPortal.cpp # Captive Portal 配網入口實作 (含 HTML 頁面)
└── README.md                # 本檔案
```

## 相依函式庫

| 函式庫 | 版本 | 用途 |
|--------|------|------|
| `adafruit/Adafruit SSD1306` | ^2.5.17 | OLED 顯示驅動 |
| `adafruit/Adafruit GFX Library` | ^1.12.6 | 圖形繪製底層 |
| `pu2clr/PU2CLR RDA5807` | ^1.1.9 | FM 收音機晶片控制 |
| `mathertel/OneButton` | ^2.6.2 | 按鍵事件處理 |
| `bblanchon/ArduinoJson` | ^7.4.3 | JSON 解析 (OpenWeatherMap) |

## 開發環境

- **IDE**: PlatformIO (VS Code 擴充)
- **Framework**: Arduino (ESP32)
- **Serial Monitor**: 115200 baud

```bash
# 在 v2 目錄下執行
pio run                  # 編譯
pio run --target upload  # 編譯並上傳到 ESP32
pio device monitor       # 開啟序列監控
```

## 初次使用設定

1. 燒錄程式後開機，OLED 顯示 `WiFi Connecting...`
2. 手機搜尋 WiFi，找到 `ESP32_SETUP_XXXX` 熱點並連線（無密碼）
3. 連線後自動彈出配網頁面（若未彈出，瀏覽器開啟 `http://192.168.4.1`）
4. 選擇您的 WiFi 並輸入密碼，按「連線」
5. ESP32 會自動重啟，之後開機就會自動連線

> **OpenWeatherMap API 金鑰**：請自行至 [openweathermap.org](https://openweathermap.org) 申請免費 API key，並修改 `src/main.cpp:291` 的 `Weather.apiKey`。

## WiFi 重置

長按 BOOT 鍵（GPIO0）3 秒，OLED 會顯示清除訊息並重啟，再次進入配網模式。

## 已知限制

1. **天氣資料需連網** — 無 WiFi 時僅顯示時鐘與 FM 功能，天氣頁顯示 "WiFi off"
2. **API 金鑰寫在源碼** — 建議 Fork 後自行管理
3. **雙頁輪播固定 6 秒** — 可修改 `updateDisplay()` 中的 `lastPageSwitch` 邏輯調整
