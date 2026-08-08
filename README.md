# RRD-102 ESP32 FM Radio Project

NodeMCU-32S + RDA5807(RRD-102) + SSD1306 OLED 之 FM 收音機專案，包含多個版本演進。

## 版本列表

### v1 — 基礎 FM 收音機

> 資料夾：[v1/](v1/) · [詳細 README](v1/README.md)

基本 FM 收音機功能，無網路依賴：

- FM 87.5–108.0 MHz 全頻段接收
- 手動調諧 / 預設頻道 雙模式
- 5 級硬體音量映射（UI 顯示 0–15）
- 全頻段自動掃描（Peak Detection 過濾旁波帶）
- 頻道儲存（最多 20 個，斷電 NVS 保存）
- OLED 選單系統（選擇、刪除、清空、自動掃描）
- 雙電位器（頻率 + 音量）+ 雙按鈕操作

### v2 — FM 收音機 + WiFi / NTP / 天氣

> 資料夾：[v2/](v2/) · [詳細 README](v2/README.md)

繼承 v1 全部功能，新增網路功能：

- **WiFi 配網** — Captive Portal 自動連線，無憑證時開 AP 讓手機設定
- **NTP 網路對時** — 開機同步，每小時校正，大字顯示日期時間
- **OpenWeatherMap 天氣** — 每 30 秒更新，圖形化天氣圖示
- **OLED 雙頁輪播** — 時鐘頁 ↔ 天氣頁每 6 秒切換

### v3 — FM 收音機 + RDS + Web 遙控 + OTA

> 資料夾：[v3/](v3/) · [詳細 README](v3/README.md)

繼承 v2 全部功能，新增：

- **RDS 數位廣播資訊** — 解碼電台名稱 (PS)、電台文字 (RT)、節目類型 (PTY)、交通資訊 (TP/TA)、時間 (CT)
- **Web 遠端遙控面板** — 瀏覽器操控調頻/音量/掃描/頻道管理，含完整 JSON API
- **OTA 無線韌體更新** — 網頁上傳 `firmware.bin` 即可更新，雙分割區 (app0/app1) 設計
- **OLED 三頁輪播** — 時鐘 / 天氣 / RDS 每 6 秒切換

> **重要修復**：PU2CLR RDA5807 函式庫已改為專案內 vendor 版（`v3/lib/PU2CLR RDA5807/`），
> 移除了原廠 1.1.9 `getStatusRegisters()` 中多餘的 `Wire.endTransmission()`（該呼叫在 ESP32 core 3.x
> 會送出幽靈 I2C 寫入、破壞 SSD1306 定址，造成 OLED 左上角掃描殘影）。詳細說明見 [v3 README 已知修復](v3/README.md)。

### 未來版本

若有新增版本，將依此目錄結構擴充（v4/、v5/ ...），每個版本獨立目錄，
各自包含完整的 `platformio.ini`、`src/` 與 `README.md`。

## 硬體共用

所有版本共用相同的硬體接線：

| 元件 | 數量 | 說明 |
|------|------|------|
| NodeMCU-32S (ESP32) | 1 | 主控核心 |
| RRD-102 V2.0 (RDA5807) | 1 | FM 收音機模組（I2C + 音訊輸出） |
| SSD1306 OLED (128×64) | 1 | I2C 顯示螢幕 |
| PAM8403 音訊放大器 | 1 | 3W 立體聲 D 類功放 |
| 2 吋喇叭 | 2 | 左右聲道 |
| 5KΩ 電位器 | 2 | 頻率 & 音量旋鈕 |
| 按鈕開關 | 2 | 操作按鍵 |
| TP4056 充電模組 | 1 | 18650 鋰電池充電 |
| MT3608 DC-DC 升壓模組 | 1 | 18650 3.7V → 5V |
| 18650 鋰電池 | 1 | 電源 |
| 自鎖開關 | 1 | 電源總開關 |

## 接線圖
**電路板正面**：
![電路板正面](電路板正面.jpg)
**電路板背面**：
![電路板背面](電路板背面.jpg)
**RRD-102 V2.0接腳圖**：
![RRD-102 V2.0接腳圖](RRD-102%20V2.0接腳圖.png)

### 電源

```
      USB 供電 (5V)
           │
           ▼
     TP4056 充電模組(帶保護板版本)
     B+ / B-       OUT+ / OUT-
       │               │
       ▼               ▼
   18650 電池       實體開關
                       │
                       ▼
                 DC-DC 升壓模組
                       │
      ┌────────────────┴────────────────┐ 
      ▼                                 ▼
 ESP32 Vin (5V)                  PAM8403 VCC (5V)
      │
      ▼
 ESP32 3.3V 腳位
      │
      ▼
RDA5807 、 0.96吋OLED
```

> 注意：RRD-102 與 OLED 由 ESP32 的 3.3V pin 供電，**不可接 5V**。

### 主電路

| ESP32 腳位 | 連接對象 |
|-------------|---------|
| GPIO21 (SDA) | OLED SDA、RRD-102 SDA |
| GPIO22 (SCL) | OLED SCL、RRD-102 SCL |
| GPIO34 | 頻率電位器中間抽頭 |
| GPIO35 | 音量電位器中間抽頭 |
| GPIO19 | 上方按鈕 (另一端接 GND) |
| GPIO18 | 下方按鈕 (另一端接 GND) |
| 3.3V | OLED VCC、RRD-102 VCC |
| GND | 所有元件的 GND 共接 |
| Vin (5V) | DC-DC 升壓模組輸出 |

### 音訊

```
RRD-102 Audio OUT (L/R)
   │
   ▼
PAM8403 Input (L/R, 串聯 1~10µF 電容)
   │
   ▼
2 吋喇叭 (L/R, 左右各一)
```

> RRD-102 音訊輸出需要串聯電容 (隔直) 再接 PAM8403 輸入。
> 建議使用 1µF ~ 10µF 無極性電容。

## 開發環境

- **IDE**: PlatformIO（VS Code 擴充）
- **Framework**: Arduino（ESP32）
- **序列埠速率**: 115200 baud

## 快速開始

```bash
# 編譯 v1
cd v1
pio run

# 編譯 v2
cd ../v2
pio run

# 編譯 v3
cd ../v3
pio run

# 上傳到 ESP32（以 v2 為例）
pio run --target upload

# 開啟序列監控
pio device monitor
```

## 版本間差異摘要

| 功能 | v1 | v2 | v3 |
|------|:--:|:--:|:--:|
| FM 收音機（87.5–108 MHz） | ✓ | ✓ | ✓ |
| 手動 / 預設模式 | ✓ | ✓ | ✓ |
| 自動掃描（Peak Detection） | ✓ | ✓ | ✓ |
| 頻道儲存（NVS） | ✓ | ✓ | ✓ |
| OLED 選單系統 | ✓ | ✓ | ✓ |
| WiFi 自動連線 / 配網 | — | ✓ | ✓ |
| NTP 網路對時 | — | ✓ | ✓ |
| OpenWeatherMap 天氣 | — | ✓ | ✓ |
| 時鐘 / 天氣頁輪播 | — | ✓ | — |
| 時鐘 / 天氣 / RDS 三頁輪播 | — | — | ✓ |
| RDS 數位廣播資訊 | — | — | ✓ |
| Web 遠端遙控面板 + JSON API | — | — | ✓ |
| OTA 無線韌體更新 | — | — | ✓ |

## 授權

專案基於 PU2CLR/RDA5807 函式庫開發，感謝原作者 pu2clr 的貢獻。
v3 使用該函式庫的 vendor 版（`v3/lib/PU2CLR RDA5807/`），僅修正 ESP32 core 3.x 下的 I2C 相容性問題，
未更動任何功能與 API，仍沿用原 MIT 授權。
