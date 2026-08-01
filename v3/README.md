# RRD-102 ESP32 FM Radio + RDS + Web Remote + OTA v3

NodeMCU-32S + RDA5807(RRD-102) + SSD1306 OLED 之 FM 收音機 — **v3 新增 RDS 數位廣播資訊、Web 遠端遙控面板、OTA 無線韌體更新**。

硬體接線與 v1 / v2 完全相同，**不需更動任何接線**。

---

## v3 新增功能

| 功能 | 說明 |
|------|------|
| **RDS 數位廣播資訊解碼** | 解碼電台名稱 (PS)、電台文字 (RT)、節目類型 (PTY)、交通資訊 (TP/TA)、RDS 時間 (CT)，OLED 新增 RDS 頁並即時顯示 |
| **Web 遠端遙控面板** | 手機/電腦瀏覽器即可操控收音機：調頻、調音量、掃描、頻道管理、RDS/天氣/時鐘資訊、靜音等，含完整 JSON API |
| **OTA 無線韌體更新** | 透過網頁上傳 `firmware.bin` 即可更新韌體，不需接 USB 線；採用雙分割區 (app0/app1) 設計 |

另外 v2 全部功能（FM 收音、選單、自動掃描、頻道儲存、WiFi 配網、NTP 對時、天氣）皆完整保留，OLED 顯示擴充為 **時鐘 / 天氣 / RDS 三頁輪播**（每 6 秒切換）。

---

## Web 遙控面板

開機連上 WiFi 後，在手機或電腦瀏覽器開啟：

```
http://<ESP32 的 IP 位址>
```
或使用 mDNS 網址（若路由器支援 multicast DNS）：
```
http://esp32-radio.local
```

> ESP32 的 IP 位址會印在序列監控（Serial Monitor），也會顯示在網頁與 OLED 資訊中。

### 面板功能
- **頻率**：滑桿直接調諧、+0.1 / -0.1 微調
- **音量**：滑桿、＋/−、靜音
- **自動掃描**：一鍵開始 / 停止全頻段掃描
- **預設頻道**：點選收聽、刪除單一頻道、上一台 / 下一台、切換模式、清空全部
- **儲存目前頻道**：將目前頻率加入預設清單
- **RDS 資訊**：電台名稱、電台文字、節目類型、交通資訊、時間
- **系統資訊**：時鐘、WiFi SSID / IP / 訊號、天氣
- **OTA 更新**：選擇 `.bin` 檔上傳，附上傳進度條

### JSON API（供進階使用）

| 方法 | 路徑 | 說明 |
|------|------|------|
| GET | `/api/status` | 取得收音機完整狀態 (JSON) |
| POST | `/api/freq` | 設定頻率，body: `freq=8750`（x10，87.5MHz） |
| POST | `/api/vol` | 設定音量，body: `vol=5`（0~15） |
| POST | `/api/cmd` | 控制命令，body: `cmd=scan` 等 |

`cmd` 支援：`scan`、`cancel`、`mode`、`next`、`prev`、`save`、`delete`(需 `idx`)、`deleteall`、`preset`(需 `idx`)、`fup`、`fdn`、`mute`、`reboot`。

範例：
```
curl http://192.168.1.100/api/status
curl -d "freq=9830" http://192.168.1.100/api/freq
curl -d "cmd=preset&idx=2" http://192.168.1.100/api/cmd
```

---

## OTA 無線韌體更新

### 方式一：網頁更新（推薦，免接線）
1. 使用 **序列埠** 燒錄一次 v3（`pio run --target upload`），此步驟會同時寫入含 OTA 分割區的韌體。
2. 之後每次更新，在遙控面板最下方「OTA 韌體更新」區塊，選擇 v3 建置產生的
   `.pio/build/nodemcu-32s/firmware.bin` 並上傳。
3. 上傳完成後 ESP32 自動重啟並套用新韌體（等待約 30 秒）。

> 注意：OTA 更新的 `.bin` **必須**使用相同的 OTA 分割表建置，否則上傳會被拒絕或開機失敗。

### 方式二：PlatformIO 命令列
若使用 USB 連接，也可用指令直接更新：
```bash
# 序列埠燒錄（首次或無法 OTA 時）
pio run --target upload

# 透過已連線的 OTA 服務更新（需先設定上傳位址，見下）
```
若要從 PC 透過 WiFi OTA 上傳，可先將 `platformio.ini` 增加一組 OTA 環境：
```ini
upload_protocol = espota
upload_port = 192.168.x.x   ; ESP32 的 IP
```
然後執行 `pio run --target upload`。

### 更新失敗的復原
OTA 採用雙分割區設計：若新版韌體無法開機，ESP32 仍會保留舊版。萬一需要救援，接上 USB 使用 `pio run --target upload` 序列埠重燒即可。

---

## RDS 數位廣播資訊

- RDS（Radio Data System）是 FM 電台隨聲音一起廣播的數位資料，台灣許多電台（如 ICRT、中廣等）都有提供。
- 本專案透過 RDA5807 晶片內建 RDS 解調，經 I2C 讀取，**不需任何額外硬體**。
- OLED 的「RDS 頁」每 6 秒輪播一次，顯示：
  - **PS 電台名稱**（如 `ICRT`）
  - **PTY 節目類型**（如 `Pop Music`）
  - **TP / TA 交通資訊旗標**
  - **RT 電台文字**（跑馬燈捲動）
- 收不到 RDS 時，畫面顯示 `No RDS`（表示該電台未提供 RDS 服務或訊號太弱）。
- Web 面板的「RDS 廣播資訊」區塊即時顯示相同資訊。

> 若某些電台 RDS 無法解碼，可在 `src/main.cpp` 中將 `rx.setRdsFifo(true)` 改為 `rx.setRdsFifo(false)`（一般模式）再測試。

---

## 硬體接線（與 v1 / v2 完全相同）

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

> 電源與音訊細節請參閱 v1 README。OLED 與 RDA5807 共用同一組 I2C（GPIO21/22），位址分別為 0x3C 與 0x11，不衝突。

---

## 操作方式（硬體按鍵，與 v1/v2 相同）

| 操作 | 按鍵 | 效果 |
|------|------|------|
| 短按 | 上方按鈕 | 預設模式：下一台；選單：向上 |
| 短按 | 下方按鈕 | 預設模式：上一台；選單：向下；掃描中：取消 |
| 長按 | 上方按鈕 | 手動模式：儲存頻道；預設模式：進入選單；選單：確認 |
| 長按 | 下方按鈕 | 手動/預設模式切換；選單：離開 |
| 旋轉 | 頻率旋鈕 | 手動模式調整頻率 / 選單子畫面選擇頻道 |
| 旋轉 | 音量旋鈕 | 調整音量 (0~15) |

選單內容：`Select Channel`（選擇頻道）、`Delete Channel`（刪除頻道）、`Delete All`（清空全部）、`Auto Scan`（自動掃描）、`Exit`（離開）。

---

## 專案結構

```
v3/
├── partitions.csv              # OTA 分割表 (app0/app1 雙分割區)
├── platformio.ini              # PlatformIO 專案設定
├── lib/
│   └── PU2CLR RDA5807/         # vendor 版 RDA5807 函式庫 (1.1.9 + 修復，見下方「已知修復」)
│       ├── library.properties
│       └── src/
│           ├── RDA5807.h
│           └── RDA5807.cpp     # 已移除 getStatusRegisters() 的多餘 Wire.endTransmission()
├── src/
│   ├── main.cpp                # 主程式 (FM 收音機、UI、選單、掃描、命令 API、OLED 三頁輪播)
│   ├── RadioControl.h          # 收音機共用狀態與命令 API (供 WebPanel 使用)
│   ├── RDS/
│   │   ├── RDS.h               # RDS 解碼類別宣告
│   │   └── RDS.cpp             # RDS 解碼實作 (PS/RT/PTY/TP/TA/CT)
│   ├── WebPanel/
│   │   ├── WebPanel.h          # Web 遙控面板類別宣告
│   │   └── WebPanel.cpp        # Web 遙控面板實作 (含網頁、JSON API、OTA)
│   ├── NTPTime/                # NTP 網路對時 (與 v2 相同)
│   ├── Weather/                # OpenWeatherMap 天氣 (與 v2 相同)
│   └── WiFiManager/            # WiFi 配網 (與 v2 相同)
│       ├── WiFiManager.*       # 自動連線 / 斷線重連 / 重置鍵
│       ├── ConfigPortal.*      # Captive Portal 配網入口 (AP + DNS 攔截)
│       └── ConfigStore.*       # WiFi 憑證 NVS 儲存
└── README.md                   # 本檔案
```

## 相依函式庫

| 函式庫 | 版本 | 用途 |
|--------|------|------|
| `adafruit/Adafruit SSD1306` | ^2.5.17 | OLED 顯示驅動 |
| `adafruit/Adafruit GFX Library` | ^1.12.6 | 圖形繪製底層 |
| `pu2clr/PU2CLR RDA5807` | 1.1.9 (**vendor 版**) | FM 收音機晶片控制（含 RDS） |
| `mathertel/OneButton` | ^2.6.2 | 按鍵事件處理 |
| `bblanchon/ArduinoJson` | ^7.4.3 | JSON 解析/產生 |

`WebServer`、`Update`、`ESPmDNS`、`HTTPClient`、`DNSServer` 均為 ESP32 Arduino 核心內建，無需額外安裝。

> **注意**：PU2CLR RDA5807 已從 `lib_deps` 移至專案內 `lib/PU2CLR RDA5807/`（vendor 版），
> 原因與修復內容請見下方「已知修復」。

---

## 已知修復（v3 開發過程）

### 1. OLED 左上角「漸層掃描」殘影（重要）
- **現象**：標頭列（PRESET/MANUAL 白底區塊）會週期性出現由左到右的掃描/撕裂殘影，v2 無此問題。
- **根因**：原廠 PU2CLR RDA5807 1.1.9 的 `getStatusRegisters()` 在 `Wire.requestFrom()`（已送出 STOP）之後
  又呼叫一次多餘的 `Wire.endTransmission()`。在 ESP32 Arduino core 3.x 中，這會用上一個 I2C 交易的
  殘留 `txAddress`/`txBuffer` 再送出一筆「幽靈寫入」到 OLED 位址 0x3C，破壞 SSD1306 的內部定址狀態，
  導致下一次整幅重繪時畫面左上角出錯。
- **修復**：將函式庫 vendor 至 `lib/PU2CLR RDA5807/` 並移除該多餘的 `Wire.endTransmission()`。
  同時將 RDS 輪詢間隔調整為 250ms（減少共用 I2C 匯流排流量，FIFO 模式不漏資料）。

### 2. 網頁顯示 HTML 實體亂碼
- **現象**：網頁按鈕顯示 `&#128269; 自動掃描`、天氣顯示 `31.52&#176;C` 等原始實體碼。
- **根因**：前端 JS 使用 `textContent` 設定內容，瀏覽器不會解碼 HTML 實體。
- **修復**：`WebPanel.cpp` 中改用 `innerHTML`（掃描按鈕與天氣欄位）。

---

## 開發環境

- **IDE**: PlatformIO（VS Code 擴充）
- **Framework**: Arduino（ESP32）
- **Serial Monitor**: 115200 baud

```bash
cd v3
pio run                  # 編譯
pio run --target upload  # 編譯並透過序列埠燒錄（首次需用此方式）
pio device monitor       # 開啟序列監控（可看到 ESP32 的 IP）
```

## 已知限制

1. **RDS 依賴電台** — 部分電台不提供 RDS 或訊號太弱，會顯示 `No RDS`
2. **首次燒錄需 USB** — OTA 前必須先用序列埠燒錄一次含 OTA 分割區的韌體
3. **mDNS 需路由器支援** — 不支援時請改用 IP 位址存取面板
4. **OTA 韌體需同分割表** — 上傳的 `.bin` 必須使用 v3 的 `partitions.csv` 建置
