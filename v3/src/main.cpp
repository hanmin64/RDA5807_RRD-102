/*
 * RRD-102 ESP32 FM Radio + RDS + Web Remote + OTA v3
 * ==================================================
 * 功能概述：
 * - FM 收音機 (87.5-108.0 MHz)          (繼承 v1/v2)
 * - 手動調諧 / 預設頻道 雙模式            (繼承 v1/v2)
 * - 音量控制 (0-15 級，0級完全靜音)       (繼承 v1/v2)
 * - 頻道儲存 (最多 20 個，斷電保存於 NVS) (繼承 v1/v2)
 * - 全頻段自動掃描 (Peak Detection)       (繼承 v1/v2)
 * - 專屬選單系統                          (繼承 v1/v2)
 * - 狀態列 UI：模式、立體聲、RSSI         (繼承 v1/v2)
 * - WiFi 配網 (Captive Portal)            (繼承 v2)
 * - NTP 網路對時                         (繼承 v2)
 * - OpenWeatherMap 天氣                  (繼承 v2)
 * - ★ RDS 數位廣播資訊解碼：電台名稱(PS)、電台文字(RT)、
 *      節目類型(PTY)、交通資訊(TP/TA)、時間(CT)
 * - ★ Web 遠端遙控面板：手機/電腦瀏覽器即時操控收音機
 *      (頻率、音量、掃描、頻道、RDS、天氣…)，含 JSON API
 * - ★ OTA 無線韌體更新：透過網頁上傳 firmware.bin 即可更新，
 *      不需接 USB 線
 * - OLED 三頁輪播：時鐘頁 + 天氣頁 + RDS 頁
 *
 * 硬體平台：ESP32 NodeMCU-32S + RDA5807 (RRD-102) + SSD1306 OLED
 * 開發框架：Arduino (PlatformIO)
 * 作者/維護者：hanmin64
 */

//==============================================================================
//  引用函式庫
//==============================================================================
#include <Arduino.h>          // Arduino 核心函式庫
#include <Wire.h>             // I2C 通訊函式庫，用於 OLED 和 RDA5807 通訊
#include <Adafruit_GFX.h>     // Adafruit 圖形底層函式庫
#include <Adafruit_SSD1306.h> // SSD1306 OLED 驅動函式庫 (128x64 解析度)
#include <RDA5807.h>          // RDA5807 FM 收音機晶片控制函式庫 (PU2CLR 版本)
#include <OneButton.h>        // 按鍵處理函式庫，支援短按/長按/雙擊
#include <Preferences.h>      // ESP32 NVS 持久化儲存

#include <WiFi.h>             // ESP32 WiFi 函式庫

#include "WiFiManager/WiFiManager.h" // WiFi 配網模組 (自動連線 + Captive Portal)
#include "NTPTime/NTPTime.h"         // NTP 網路對時模組
#include "Weather/Weather.h"         // OpenWeatherMap 天氣查詢模組
#include "RDS/RDS.h"                 // RDS 數位廣播資訊解碼模組
#include "WebPanel/WebPanel.h"       // Web 遠端遙控面板 + OTA 無線更新模組
#include "RadioControl.h"            // 收音機共用狀態與命令 API

//==============================================================================
//  OLED 設定 (SSD1306, 128x64, I2C 介面)
//==============================================================================
#define SCREEN_WIDTH   128    // OLED 螢幕寬度 (像素)
#define SCREEN_HEIGHT 64     // OLED 螢幕高度 (像素)
#define OLED_RESET    -1     // OLED 重置腳位 (-1 = 共用系統重置)
#define OLED_ADDR     0x3C   // SSD1306 OLED 的 I2C 位址

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//==============================================================================
//  RDA5807 FM 收音機晶片物件
//==============================================================================
RDA5807 rx;  // RDA5807 控制物件；I2C 位址 0x11，與 OLED (0x3C) 共用匯流排

//==============================================================================
//  I2C 腳位定義 (ESP32 硬體 I2C，可自定義)
//==============================================================================
#define I2C_SDA  21  // I2C 資料線 → 同時連接 OLED SDA 與 RDA5807 SDA
#define I2C_SCL  22  // I2C 時脈線 → 同時連接 OLED SCL 與 RDA5807 SCL

//==============================================================================
//  輸入腳位定義
//==============================================================================
#define FREQ_PIN      34   // 頻率旋鈕 (電位器) 類比輸入  --> GPIO34 (ADC1_CH6)
#define VOL_PIN       35   // 音量旋鈕 (電位器) 類比輸入  --> GPIO35 (ADC1_CH7)
#define BTN_NEXT_PIN  19   // 上方按鈕                     --> GPIO19
#define BTN_PREV_PIN  18   // 下方按鈕                     --> GPIO18

//==============================================================================
//  按鍵物件 (使用 OneButton 函式庫)
//==============================================================================
OneButton btnPrev(BTN_PREV_PIN, true, true);  // 下方按鈕
OneButton btnNext(BTN_NEXT_PIN, true, true);  // 上方按鈕

//==============================================================================
//  ADC 濾波與防抖參數
//==============================================================================
#define FREQ_EMA_ALPHA     0.10   // 頻率旋鈕 ADC 的 EMA 濾波係數
#define VOL_EMA_ALPHA      0.10   // 音量旋鈕 ADC 的 EMA 濾波係數
#define WEATHER_INTERVAL_MS  30000UL // 天氣資料更新間隔 (30 秒)

#define FREQ_DEADZONE      15     // 頻率旋鈕死區
#define GENERAL_DEADZONE   40     // 一般旋鈕死區
#define ADC_MAX            4095   // ESP32 ADC 最大輸出值 (12-bit)
#define FREQ_UPDATE_DELAY  50     // 兩次頻率更新之間的最小間隔 (毫秒)

//==============================================================================
//  自動掃描參數
//==============================================================================
#define RSSI_THRESHOLD  22    // RSSI 門檻值
#define SCAN_SETTLE_MS  60    // 設定頻率後的等待時間 (毫秒)
#define SCAN_PAUSE_MS   100   // 找到有效頻道後的暫停時間 (毫秒)
#define FREQ_MIN        8750  // 最低頻率 87.5 MHz (x10 表示)
#define FREQ_MAX        10800 // 最高頻率 108.0 MHz (x10 表示)
#define FREQ_STEP       10    // 自動掃描頻率步進 (0.1 MHz)

//==============================================================================
//  系統狀態列舉
//==============================================================================

// 自動掃描狀態機步驟 (ScanStep)
enum ScanStep {
  SCAN_STEP_SET,   // 步驟 1：將掃描頻率寫入 RDA5807 晶片
  SCAN_STEP_WAIT,  // 步驟 2：等待頻率鎖定，然後讀取 RSSI 判斷是否為有效電台
  SCAN_STEP_PAUSE  // 步驟 3：找到有效電台後短暫暫停，讓使用者看到發現的頻率
};

// 選單選項列舉 (MenuOption)，對應 5 個功能選項
enum MenuOption {
  MENU_SELECT_CH,   // 選項 1：選擇已儲存的頻道
  MENU_DELETE_CH,   // 選項 2：刪除單一頻道
  MENU_DELETE_ALL,  // 選項 3：清空所有頻道
  MENU_AUTO_SCAN,   // 選項 4：自動全頻段掃描
  MENU_EXIT         // 選項 5：離開選單
};

//==============================================================================
//  旋鈕互斥機制 (Knob Focus / Mutex)
//==============================================================================
enum ActiveKnob {
  KNOB_NONE,  // 沒有任何旋鈕正被操作
  KNOB_VOL,   // 音量旋鈕正被操作 (頻率旋鈕進入高死區)
  KNOB_FREQ   // 頻率旋鈕正被操作 (音量旋鈕進入高死區)
};

ActiveKnob activeKnob = KNOB_NONE;
unsigned long lastKnobTime = 0;
const unsigned long KNOB_FOCUS_TIMEOUT = 1000;
const int CROSS_FOCUS_DEADZONE = 150;

//==============================================================================
//  全域變數 (extern 宣告於 RadioControl.h，此處為實際定義)
//==============================================================================

// --- 頻道儲存陣列 ---
int presets[MAX_PRESETS];    // 儲存已收藏的頻道值 (x10 表示)
int presetCount = 0;         // 目前已儲存的頻道數量
int currentPresetIdx = 0;    // 當前在預設模式中選擇的頻道索引

// --- 系統模式 ---
SystemMode currentMode = MODE_MANUAL;  // 系統初始模式：手動調諧

// --- ADC 濾波相關 ---
float smoothedFreqADC = 0;   // 頻率旋鈕的 EMA 濾波後平滑值
float smoothedVolADC = 0;    // 音量旋鈕的 EMA 濾波後平滑值
int lastRawFreq = 0;         // 最後一次觸發頻率更新的原始 ADC 值
int lastRawVol = 0;          // 最後一次觸發音量更新的原始 ADC 值

// --- 當前播放參數 (extern 於 RadioControl.h) ---
int currentFreq = FREQ_MIN;  // 當前 FM 頻率 (x10)
int currentVol = 0;          // 當前音量值 (0~15，0=靜音)
bool isStereo = false;       // 是否接收立體聲

// --- 顯示更新控制 ---
unsigned long lastDisplayUpdate = 0;
bool forceDisplayUpdate = true;
unsigned long lastFreqUpdateTime = 0;

// --- 掃描狀態機 ---
bool isScanning = false;
int scanFreq = FREQ_MIN;
ScanStep scanStep = SCAN_STEP_SET;
unsigned long scanStepTimer = 0;
int prevFreqBeforeScan = 0;
SystemMode prevModeBeforeScan;

int presetsBeforeScan = 0;
int presetsBackup[MAX_PRESETS];
int lastScanSavedRSSI = 0;

// --- 通用 Popup 提示系統 ---
bool showPopup = false;
char popupMsg[16] = "";
unsigned long popupTimer = 0;

// --- 選單系統 ---
MenuOption currentMenuOption = MENU_SELECT_CH;
bool isSubMenu = false;
int selectingChannelIdx = 0;

//==============================================================================
//  函式宣告 (Forward Declarations)
//==============================================================================
int safeAnalogRead(int pin);
void updateKnobFocus();
void enterMenu();
void exitMenu();
void handleMenuNav(int direction);
void handleMenuSelect();
void deleteSpecificPreset(int idx);
void deleteAllPresets();
void sortPresets();
void triggerPopup(const char* msg);
void autoScan();
void cancelScan();
void finishScan();
void handleScanStep();
void advanceScanStep();
void savePresets();
void loadPresets();
void processVolume();
void processFrequency();
void processMenuKnob();
void updateDisplayTask();
void updateDisplay();
void drawWeatherIcon(int x, int y, int code);

void btnPrevClick();
void btnNextClick();
void btnPrevLongPress();
void btnNextLongPress();

//==============================================================================
//  防串擾 ADC 讀取函式
//==============================================================================
int safeAnalogRead(int pin) {
  analogRead(pin);                          // 第 1 次：切換 ADC 多工器通道 (丟棄)
  analogRead(pin);                          // 第 2 次：讓內部取樣保持電容充電 (丟棄)
  delay(2);                                 // 等待 2ms 確保電壓穩定
  return analogRead(pin);                   // 第 3 次：回傳穩定的類比讀取值
}

//==============================================================================
//  setup() — 出廠初始化，只執行一次
//==============================================================================
void setup() {
  Serial.begin(115200);

  // 設定 ADC：ADC_11db → 0~3.6V；12-bit → 0~4095
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  // 啟動 I2C 匯流排：SDA=GPIO21, SCL=GPIO22
  // 維持預設 100kHz：RDA5807 的 RDS 讀取與 SSD1306 整幅重繪共用此匯流排，
  // 100kHz 較能容忍匯流排時序誤差 (若上拉電阻偏弱)，且 v2 在 100kHz 下無此問題。
  Wire.begin(I2C_SDA, I2C_SCL);

  // 初始化 OLED 螢幕
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED init failed!"));
    for (;;);
  }
  display.setTextColor(SSD1306_WHITE);
  delay(300);

  // 初始化 RDA5807 FM 收音機晶片
  rx.setup();

  // --- 啟用 RDS 數位廣播資訊解碼 ---
  rx.setRDS(true);       // 開啟 RDS
  rx.setRdsFifo(true);   // RDS FIFO 模式 (與函式庫官方範例一致)
  rx.clearRdsBuffer();   // 清除晶片內部 RDS 緩衝區
  RDS.begin();           // 初始化 RDS 解碼模組

  // WiFi 配網：自動連線或進入配網入口 (Captive Portal)
  WiFiMgr.begin();

  // NTP 網路對時與天氣查詢 (僅在 WiFi 連線成功時執行)
  if (WiFiMgr.isConnected()) {
    NTP.begin();
    Weather.apiKey = "cdaa15a68f4c0daa14d7bbd691dfb2c8";
    Weather.fetch();
    // 啟動 Web 遠端遙控面板 (需 WiFi)
    Web.begin();
  }

  // 從 ESP32 NVS 載入先前儲存的頻道
  loadPresets();

  // 根據載入結果決定初始模式
  if (presetCount == 0) {
    currentMode = MODE_MANUAL;
    currentFreq = FREQ_MIN;
  } else {
    currentMode = MODE_PRESET;
    currentFreq = presets[0];
    currentPresetIdx = 0;
  }

  // 音量初始為 0 (靜音)，避免開機時爆音
  if (currentVol == 0) {
    rx.setMute(true);
  } else {
    rx.setMute(false);
    rx.setVolume(currentVol);
  }

  // 設定 FM 晶片到初始頻率
  rx.setFrequency(currentFreq);

  // 初始化 ADC 平滑濾波器：連續取樣 10 次取平均作為起點
  long freqSum = 0, volSum = 0;
  const int INIT_SAMPLES = 10;
  for (int i = 0; i < INIT_SAMPLES; i++) {
    freqSum += safeAnalogRead(FREQ_PIN);
    volSum  += safeAnalogRead(VOL_PIN);
    delay(5);
  }
  smoothedFreqADC = freqSum / (float)INIT_SAMPLES;
  smoothedVolADC  = volSum / (float)INIT_SAMPLES;
  lastRawFreq = (int)smoothedFreqADC;
  lastRawVol  = (int)smoothedVolADC;

  // 設定按鍵防抖時間 (40ms)
  btnPrev.setDebounceMs(40);
  btnNext.setDebounceMs(40);

  // 註冊按鍵事件的回呼函式
  btnNext.attachClick(btnNextClick);
  btnNext.attachLongPressStart(btnNextLongPress);

  btnPrev.attachClick(btnPrevClick);
  btnPrev.attachLongPressStart(btnPrevLongPress);
}

//==============================================================================
//  loop() — 主程式循環，不斷重複執行
//==============================================================================
void loop() {
  // 1. 輪詢按鍵狀態
  btnPrev.tick();
  btnNext.tick();

  // 2. IoT 背景任務：WiFi 管理、NTP 對時、天氣更新
  //    若正在 OTA 更新，暫時跳過會阻塞的重連/網路請求，避免中斷韌體上傳
  if (!Web.isUpdating()) {
    WiFiMgr.handle();
    if (WiFiMgr.isConnected()) {
      NTP.handle();
      static unsigned long lastWeather = 0;
      if (millis() - lastWeather > WEATHER_INTERVAL_MS) {
        lastWeather = millis();
        Weather.fetch();
      }
    }
  }

  // 3. RDS 解碼 (不限 WiFi，隨時輪詢)
  RDS.handle();

  // 4. Web 遙控面板：處理 HTTP 請求 (含 OTA 分段上傳)
  Web.handle();

  // 5. 偵測頻率變更 → 清除 RDS 緩衝區 (換台時避免顯示舊電台資訊)
  static int lastFreqForRDS = -1;
  if (currentFreq != lastFreqForRDS) {
    lastFreqForRDS = currentFreq;
    RDS.clear();
    rx.clearRdsBuffer();
  }

  // 6. 檢查旋鈕焦點是否超時，超時則釋放焦點
  updateKnobFocus();

  // 7. 處理音量旋鈕
  processVolume();

  // 8. 依據目前模式處理不同操作邏輯
  if (isScanning) {
    handleScanStep();
  } else if (currentMode == MODE_MANUAL) {
    processFrequency();
  } else if (currentMode == MODE_MENU && isSubMenu) {
    processMenuKnob();
  }

  // 9. 更新 OLED 顯示
  updateDisplayTask();

  // 10. 主循環延遲 10ms
  delay(10);
}

//==============================================================================
//  旋鈕互斥與自動校準 (Knob Focus Management)
//==============================================================================
void updateKnobFocus() {
  if (activeKnob != KNOB_NONE && (millis() - lastKnobTime > KNOB_FOCUS_TIMEOUT)) {
    lastRawFreq = (int)smoothedFreqADC;
    lastRawVol  = (int)smoothedVolADC;
    activeKnob = KNOB_NONE;
  }
}

//==============================================================================
//  Popup 提示系統
//==============================================================================
void triggerPopup(const char* msg) {
  strncpy(popupMsg, msg, sizeof(popupMsg) - 1);
  popupMsg[sizeof(popupMsg) - 1] = '\0';
  showPopup = true;
  popupTimer = millis();
  forceDisplayUpdate = true;
}

//==============================================================================
//  音量控制邏輯
//==============================================================================
#define MAX_HW_VOL 5

void processVolume() {
  int rawVol = safeAnalogRead(VOL_PIN);

  smoothedVolADC = (VOL_EMA_ALPHA * rawVol) + ((1 - VOL_EMA_ALPHA) * smoothedVolADC);

  int effectiveDeadzone = (activeKnob == KNOB_FREQ) ? CROSS_FOCUS_DEADZONE : GENERAL_DEADZONE;

  if (abs(smoothedVolADC - lastRawVol) > effectiveDeadzone) {
    lastRawVol = (int)smoothedVolADC;
    activeKnob = KNOB_VOL;
    lastKnobTime = millis();

    float normalizedVol = smoothedVolADC / (float)ADC_MAX;
    int displayVol = (int)(normalizedVol * 15.0f + 0.5f);
    displayVol = constrain(displayVol, 0, 15);

    int hwVol = 0;
    if (displayVol > 0) {
      hwVol = ((displayVol - 1) * MAX_HW_VOL / 15) + 1;
    }

    if (displayVol != currentVol) {
      currentVol = displayVol;

      if (currentVol == 0) {
        rx.setMute(true);
      } else {
        rx.setMute(false);
        rx.setVolume(hwVol);
      }
      forceDisplayUpdate = true;
    }
  }
}

//==============================================================================
//  頻率控制邏輯 (僅在手動模式 MODE_MANUAL 下執行)
//==============================================================================
void processFrequency() {
  int rawFreq = safeAnalogRead(FREQ_PIN);

  smoothedFreqADC = (FREQ_EMA_ALPHA * rawFreq) + ((1 - FREQ_EMA_ALPHA) * smoothedFreqADC);

  int effectiveDeadzone = (activeKnob == KNOB_VOL) ? CROSS_FOCUS_DEADZONE : FREQ_DEADZONE;

  if (abs(smoothedFreqADC - lastRawFreq) > effectiveDeadzone) {
    if (millis() - lastFreqUpdateTime > FREQ_UPDATE_DELAY) {
      lastRawFreq = (int)smoothedFreqADC;
      activeKnob = KNOB_FREQ;
      lastKnobTime = millis();

      int mappedFreq = (int)(smoothedFreqADC * 205.0f / ADC_MAX + 875.0f + 0.5f);
      mappedFreq = constrain(mappedFreq, 875, 1080);

      int targetFreq = mappedFreq * 10;

      if (targetFreq != currentFreq) {
        currentFreq = targetFreq;
        rx.setFrequency(currentFreq);
        forceDisplayUpdate = true;
        lastFreqUpdateTime = millis();
      }
    }
  }
}

//==============================================================================
//  選單旋鈕控制 (在選單子畫面中用頻率旋鈕選擇頻道)
//==============================================================================
void processMenuKnob() {
  int rawFreq = safeAnalogRead(FREQ_PIN);
  smoothedFreqADC = (FREQ_EMA_ALPHA * rawFreq) + ((1 - FREQ_EMA_ALPHA) * smoothedFreqADC);

  int effectiveDeadzone = (activeKnob == KNOB_VOL) ? CROSS_FOCUS_DEADZONE : GENERAL_DEADZONE;

  if (abs(smoothedFreqADC - lastRawFreq) > effectiveDeadzone) {
    if (millis() - lastFreqUpdateTime > FREQ_UPDATE_DELAY) {
      lastRawFreq = (int)smoothedFreqADC;
      activeKnob = KNOB_FREQ;
      lastKnobTime = millis();

      int mappedIdx = (int)(smoothedFreqADC * presetCount / (ADC_MAX + 1.0f));
      mappedIdx = constrain(mappedIdx, 0, presetCount - 1);

      if (mappedIdx != selectingChannelIdx) {
        selectingChannelIdx = mappedIdx;
        forceDisplayUpdate = true;
        lastFreqUpdateTime = millis();
      }
    }
  }
}

//==============================================================================
//  排序演算法 (Bubble Sort)
//==============================================================================
void sortPresets() {
  for (int i = 0; i < presetCount - 1; i++) {
    for (int j = i + 1; j < presetCount; j++) {
      if (presets[i] > presets[j]) {
        int temp = presets[i];
        presets[i] = presets[j];
        presets[j] = temp;
      }
    }
  }
}

//==============================================================================
//  選單控制函式
//==============================================================================

void enterMenu() {
  currentMode = MODE_MENU;
  currentMenuOption = MENU_SELECT_CH;
  isSubMenu = false;
  forceDisplayUpdate = true;
}

void exitMenu() {
  if (presetCount > 0) {
    currentMode = MODE_PRESET;
  } else {
    currentMode = MODE_MANUAL;
  }
  isSubMenu = false;
  forceDisplayUpdate = true;
}

void handleMenuNav(int direction) {
  if (isSubMenu) {
    if (direction == -1) {
      isSubMenu = false;
      forceDisplayUpdate = true;
    }
    return;
  }
  int maxOption = 4;
  currentMenuOption = (MenuOption)((currentMenuOption + direction + maxOption + 1) % (maxOption + 1));
  forceDisplayUpdate = true;
}

void handleMenuSelect() {
  if (isSubMenu) {
    if (currentMenuOption == MENU_SELECT_CH) {
      currentPresetIdx = selectingChannelIdx;
      currentFreq = presets[currentPresetIdx];
      rx.setFrequency(currentFreq);
      exitMenu();
    } else if (currentMenuOption == MENU_DELETE_CH) {
      deleteSpecificPreset(selectingChannelIdx);
      isSubMenu = false;
      if (presetCount == 0) exitMenu();
    }
    return;
  }

  switch (currentMenuOption) {
    case MENU_SELECT_CH:
      if (presetCount > 0) {
        isSubMenu = true;
        selectingChannelIdx = currentPresetIdx;
        activeKnob = KNOB_NONE;
        lastRawFreq = (int)smoothedFreqADC;
        forceDisplayUpdate = true;
      } else {
        triggerPopup("NO SAVED CH");
      }
      break;

    case MENU_DELETE_CH:
      if (presetCount > 0) {
        isSubMenu = true;
        selectingChannelIdx = currentPresetIdx;
        activeKnob = KNOB_NONE;
        lastRawFreq = (int)smoothedFreqADC;
        forceDisplayUpdate = true;
      } else {
        triggerPopup("NO SAVED CH");
      }
      break;

    case MENU_DELETE_ALL:
      deleteAllPresets();
      exitMenu();
      break;

    case MENU_AUTO_SCAN:
      exitMenu();
      autoScan();
      break;

    case MENU_EXIT:
      exitMenu();
      break;
  }
}

//==============================================================================
//  刪除特定頻道 (依索引)
//==============================================================================
void deleteSpecificPreset(int idx) {
  if (presetCount <= 0 || idx >= presetCount) return;

  for (int i = idx; i < presetCount - 1; i++) {
    presets[i] = presets[i + 1];
  }
  presetCount--;

  if (currentPresetIdx == idx) {
    if (currentPresetIdx >= presetCount && presetCount > 0) {
      currentPresetIdx = presetCount - 1;
    }
    if (presetCount > 0) {
      currentFreq = presets[currentPresetIdx];
      rx.setFrequency(currentFreq);
    }
  } else if (currentPresetIdx > idx) {
    currentPresetIdx--;
  }

  savePresets();
  triggerPopup("DELETED!");
}

//==============================================================================
//  刪除所有頻道
//==============================================================================
void deleteAllPresets() {
  presetCount = 0;
  currentPresetIdx = 0;
  currentFreq = FREQ_MIN;
  savePresets();
  triggerPopup("CLEARED!");
}

//==============================================================================
//  按鍵事件回呼 — 短按
//==============================================================================

void btnNextClick() {
  if (isScanning) return;

  if (currentMode == MODE_MENU) {
    handleMenuNav(-1);
    return;
  }

  if (currentMode == MODE_PRESET && presetCount > 0) {
    currentPresetIdx++;
    if (currentPresetIdx >= presetCount) currentPresetIdx = 0;
    currentFreq = presets[currentPresetIdx];
    rx.setFrequency(currentFreq);
    forceDisplayUpdate = true;
  }
}

void btnPrevClick() {
  if (isScanning) {
    cancelScan();
    return;
  }

  if (currentMode == MODE_MENU) {
    handleMenuNav(1);
    return;
  }

  if (currentMode == MODE_PRESET && presetCount > 0) {
    currentPresetIdx--;
    if (currentPresetIdx < 0) currentPresetIdx = presetCount - 1;
    currentFreq = presets[currentPresetIdx];
    rx.setFrequency(currentFreq);
    forceDisplayUpdate = true;
  }
}

//==============================================================================
//  按鍵事件回呼 — 長按
//==============================================================================

void btnNextLongPress() {
  if (isScanning) return;

  if (currentMode == MODE_MENU) {
    handleMenuSelect();
    return;
  }

  if (currentMode == MODE_PRESET) {
    enterMenu();
    return;
  }

  // 手動模式：儲存當前頻道
  if (currentMode != MODE_MANUAL || presetCount >= MAX_PRESETS) return;

  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) return;
  }

  presets[presetCount] = currentFreq;
  presetCount++;

  sortPresets();

  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) {
      currentPresetIdx = i;
      break;
    }
  }

  currentMode = MODE_PRESET;
  savePresets();
  triggerPopup("SAVED!");
}

void btnPrevLongPress() {
  if (isScanning) {
    cancelScan();
    return;
  }

  if (currentMode == MODE_MENU) {
    exitMenu();
    return;
  }

  if (currentMode == MODE_MANUAL) {
    if (presetCount > 0) {
      currentMode = MODE_PRESET;
      currentFreq = presets[currentPresetIdx];
      rx.setFrequency(currentFreq);
    }
  } else {
    currentMode = MODE_MANUAL;
    activeKnob = KNOB_NONE;
    lastRawFreq = (int)smoothedFreqADC;
  }
  forceDisplayUpdate = true;
}

//==============================================================================
//  自動掃描邏輯
//==============================================================================

void autoScan() {
  prevFreqBeforeScan = currentFreq;
  prevModeBeforeScan = currentMode;
  presetsBeforeScan = presetCount;

  for (int i = 0; i < presetCount; i++) {
    presetsBackup[i] = presets[i];
  }
  presetCount = 0;

  isScanning = true;
  scanFreq = FREQ_MIN;
  scanStep = SCAN_STEP_SET;
  scanStepTimer = millis();
  lastScanSavedRSSI = 0;

  forceDisplayUpdate = true;
}

void cancelScan() {
  isScanning = false;
  currentFreq = prevFreqBeforeScan;
  currentMode = prevModeBeforeScan;
  currentPresetIdx = 0;

  presetCount = presetsBeforeScan;
  for (int i = 0; i < presetCount; i++) {
    presets[i] = presetsBackup[i];
  }

  rx.setFrequency(currentFreq);
  forceDisplayUpdate = true;
}

void finishScan() {
  isScanning = false;
  if (presetCount > 0) {
    sortPresets();
    currentFreq = presets[0];
    currentMode = MODE_PRESET;
    currentPresetIdx = 0;
    savePresets();
  } else {
    presetCount = 1;
    presets[0] = FREQ_MIN;
    currentFreq = FREQ_MIN;
    currentMode = MODE_MANUAL;
  }
  rx.setFrequency(currentFreq);
  forceDisplayUpdate = true;
}

void handleScanStep() {
  unsigned long now = millis();
  switch (scanStep) {
    case SCAN_STEP_SET:
      rx.setFrequency(scanFreq);
      scanStepTimer = now;
      scanStep = SCAN_STEP_WAIT;
      break;

    case SCAN_STEP_WAIT:
      if (now - scanStepTimer < SCAN_SETTLE_MS) return;

      {
        int rssi = rx.getRssi();

        if (rssi >= RSSI_THRESHOLD) {
          if (presetCount > 0 && (scanFreq - presets[presetCount - 1] <= 30)) {
            if (rssi > lastScanSavedRSSI) {
              presets[presetCount - 1] = scanFreq;
              lastScanSavedRSSI = rssi;

              scanStep = SCAN_STEP_PAUSE;
              scanStepTimer = now;
              forceDisplayUpdate = true;
              return;
            }
          } else if (presetCount < MAX_PRESETS) {
            presets[presetCount] = scanFreq;
            presetCount++;
            lastScanSavedRSSI = rssi;

            scanStep = SCAN_STEP_PAUSE;
            scanStepTimer = now;
            forceDisplayUpdate = true;
            return;
          }
        }
      }
      advanceScanStep();
      break;

    case SCAN_STEP_PAUSE:
      if (now - scanStepTimer < SCAN_PAUSE_MS) return;
      advanceScanStep();
      break;
  }
}

void advanceScanStep() {
  scanFreq += FREQ_STEP;
  if (scanFreq > FREQ_MAX) {
    finishScan();
  } else {
    scanStep = SCAN_STEP_SET;
    forceDisplayUpdate = true;
  }
}

//==============================================================================
//  NVS 持久化儲存 (Preferences)
//==============================================================================

void savePresets() {
  Preferences prefs;
  prefs.begin("radio", false);
  prefs.putInt("count", presetCount);
  for (int i = 0; i < presetCount; i++) {
    String key = "p" + String(i);
    prefs.putInt(key.c_str(), presets[i]);
  }
  prefs.end();
}

void loadPresets() {
  Preferences prefs;
  prefs.begin("radio", true);
  int savedCount = prefs.getInt("count", 0);
  prefs.end();

  if (savedCount == 0) {
    int defaultStations[] = {
      8830, 8970,  9010, 9130, 9170, 9210, 9270, 9430, 9630, 9770,
      9810, 9890,  9970, 10070, 10170, 10250, 10330, 10410, 10490, 10770
    };

    presetCount = sizeof(defaultStations) / sizeof(defaultStations[0]);
    if (presetCount > MAX_PRESETS) presetCount = MAX_PRESETS;

    for (int i = 0; i < presetCount; i++) {
      presets[i] = defaultStations[i];
    }
    sortPresets();
    savePresets();
  } else {
    prefs.begin("radio", true);
    presetCount = savedCount;
    if (presetCount <= 0 || presetCount > MAX_PRESETS) presetCount = 1;
    for (int i = 0; i < presetCount; i++) {
      String key = "p" + String(i);
      presets[i] = prefs.getInt(key.c_str(), FREQ_MIN);
    }
    prefs.end();
  }
}

//==============================================================================
//  OLED 顯示更新
//==============================================================================

void updateDisplayTask() {
  if (forceDisplayUpdate || millis() - lastDisplayUpdate > 500) {
    if (!isScanning) {
      isStereo = rx.isStereo();
    }
    updateDisplay();
    lastDisplayUpdate = millis();
    forceDisplayUpdate = false;
  }
}

void updateDisplay() {
  display.clearDisplay();

  // --- 1. Popup 提示畫面 (優先級最高) ---
  if (showPopup) {
    if (millis() - popupTimer < 800) {
      display.setTextSize(2);
      display.setCursor(10, 24);
      display.print(popupMsg);
      display.display();
      return;
    } else {
      showPopup = false;
    }
  }

  // --- 2. 掃描進行中畫面 ---
  if (isScanning) {
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print(F(" Auto Scanning..."));

    display.drawRect(14, 40, 100, 10, SSD1306_WHITE);
    int progress = map(scanFreq, FREQ_MIN, FREQ_MAX, 0, 96);
    progress = constrain(progress, 0, 96);
    if (progress > 0) display.fillRect(16, 42, progress, 6, SSD1306_WHITE);
    display.display();
    return;
  }

  // --- 3. 選單畫面 ---
  if (currentMode == MODE_MENU) {
    if (!isSubMenu) {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print(F("---- MAIN MENU ----"));

      const char* menuItems[] = {
        "1. Select Channel",
        "2. Delete Channel",
        "3. Delete All",
        "4. Auto Scan",
        "5. Exit"
      };
      for (int i = 0; i < 5; i++) {
        display.setCursor(5, 12 + (i * 10));
        if (currentMenuOption == i) {
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.print(menuItems[i]);
          display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        } else {
          display.print(menuItems[i]);
        }
      }
    } else {
      display.setTextSize(1);
      display.setCursor(0, 0);
      if (currentMenuOption == MENU_SELECT_CH) {
        display.print(F("--- SELECT CH ---"));
      } else {
        display.print(F("--- DELETE CH ---"));
      }

      display.setTextSize(2);
      display.setCursor(10, 24);
      display.print(presets[selectingChannelIdx] / 100.0f, 1);
      display.print(F(" MHz"));

      display.setTextSize(1);
      display.setCursor(0, 54);
      display.print(F("CH:"));
      display.print(selectingChannelIdx + 1);
      display.print('/');
      display.print(presetCount);

      display.setCursor(52, 54);
      display.print(F("Hold Up:OK"));
    }
    display.display();
    return;
  }

  // --- 4. 正常播放畫面 (MANUAL / PRESET) ---
  // 三頁輪播：Page 0 = 時鐘, Page 1 = 天氣, Page 2 = RDS，每 ~6 秒切換
  {
    // ---- 標頭列 (y=0-12)：模式 + 頻率 + 立體聲 ----
    display.setTextSize(1);
    if (currentMode == MODE_MANUAL) {
      display.fillRect(0, 0, 40, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(2, 2);
      display.print(F("MANUAL"));
    } else {
      display.fillRect(0, 0, 40, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(2, 2);
      display.print(F("PRESET"));
    }
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

    display.setCursor(48, 2);
    display.print(currentFreq / 100.0f, 1);
    display.print(F("MHz"));

    if (isStereo) {
      display.setCursor(102, 2);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.print(F(" ST "));
      display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    } else {
      display.setCursor(102, 2);
      display.print(F("MONO"));
    }

    display.drawLine(0, 13, 128, 13, SSD1306_WHITE);

    // ---- 頁面定時切換 (時鐘 / 天氣 / RDS) ----
    static unsigned long lastPageSwitch = 0;
    static int showPage = 0;   // 0=時鐘, 1=天氣, 2=RDS
    // 目前是否有可顯示的 RDS 資訊 (電台名稱或電台文字)
    bool rdsAvailable = RDS.hasStation() || strlen(RDS.getRadioText()) > 0;
    if (millis() - lastPageSwitch > 6000) {
      lastPageSwitch = millis();
      showPage = (showPage + 1) % 3;
    }
    // 沒有 RDS 資訊時不輪播 RDS 頁 (第 3 頁)；已在 RDS 頁但資訊消失時也切離
    if (showPage == 2 && !rdsAvailable) {
      showPage = 0;
    }

    if (showPage == 0) {
      // ======== Page 0：大字日期 + 大字時間 ========
      display.setTextSize(2);
      if (NTP.isSynced()) {
        display.setCursor(6, 18);
        display.print(NTP.getDateTime("%Y-%m-%d"));
        display.setCursor(18, 38);
        display.print(NTP.getDateTime("%H:%M:%S"));
      } else {
        display.setCursor(18, 18);
        display.print("Syncing");
        display.setCursor(18, 38);
        display.print("NTP...");
      }
    } else if (showPage == 1) {
      // ======== Page 1：天氣圖示(左) + 溫度℃(右上) + 濕度(右下) ========
      display.setTextSize(2);
      if (Weather.isFetched() && WiFiMgr.isConnected()) {
        drawWeatherIcon(16, 16, Weather.getWeatherId());
        display.setCursor(64, 18);
        {
          String t = Weather.getTemp();
          int dot = t.indexOf('.');
          if (dot > 0) t = t.substring(0, dot);
          display.print(t);
          display.drawCircle(64 + t.length() * 12 + 2, 19, 2, SSD1306_WHITE);
          display.setCursor(64 + t.length() * 12 + 7, 18);
          display.print("C");
        }
        display.setCursor(64, 38);
        display.print(Weather.getHumidity());
        display.print("%");
      } else {
        display.setCursor(16, 18);
        if (!WiFiMgr.isConnected()) display.print("WiFi off");
        else display.print("No data");
        display.setCursor(16, 38);
        display.print("weather");
      }
    } else {
      // ======== Page 2：RDS 數位廣播資訊 ========
      if (RDS.hasStation()) {
        // 電台名稱 (大字)
        display.setTextSize(2);
        display.setCursor(6, 16);
        display.print(RDS.getStation());

        // PTY 節目類型 + 交通旗標 (第二行)
        display.setTextSize(1);
        display.setCursor(4, 32);
        display.print(F("PTY:"));
        display.print(RDS.getPtyName());
        if (RDS.hasTrafficProgram()) display.print(F(" TP"));
        if (RDS.hasTrafficAnnouncement()) display.print(F(" *TA*"));

        // RT 電台文字跑馬燈 (第三行)
        const char* rt = RDS.getRadioText();
        int rtLen = (int)strlen(rt);
        if (rtLen > 0) {
          static unsigned long lastRdsScroll = 0;
          static int rdsScrollPos = 0;
          if (millis() - lastRdsScroll > 250) {
            lastRdsScroll = millis();
            rdsScrollPos++;
            if (rdsScrollPos > rtLen + 6) rdsScrollPos = 0;
          }
          display.setCursor(0, 46);
          const int VIEW_W = 21;   // 128px / 6px 每字元 = 21 字元
          for (int i = 0; i < VIEW_W; i++) {
            int idx = rdsScrollPos + i;
            char c = (idx >= 0 && idx < rtLen) ? rt[idx] : ' ';
            display.write(c);
          }
        }
      } else {
        // 無 RDS 資料
        display.setTextSize(2);
        display.setCursor(6, 20);
        display.print(F("No RDS"));
        display.setTextSize(1);
        display.setCursor(8, 38);
        display.print(F("Waiting for data..."));
      }
    }

    // ---- 底部列 (y=56)：左 Vol / 右 CH ----
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print(F("Vol:"));
    if (currentVol < 10) display.print(' ');
    display.print(currentVol);

    if (currentMode == MODE_PRESET) {
      display.setCursor(80, 56);
      display.print(F("CH:"));
      display.print(currentPresetIdx + 1);
      display.print('/');
      display.print(presetCount);
    } else {
      int savedIndex = -1;
      for (int i = 0; i < presetCount; i++) {
        if (presets[i] == currentFreq) { savedIndex = i; break; }
      }
      if (savedIndex != -1) {
        display.setCursor(56, 56);
        display.print(F("CH:"));
        display.print(savedIndex + 1);
      } else if (presetCount < MAX_PRESETS) {
        display.setCursor(84, 56);
        display.print(F("+Save"));
      }
    }

    display.display();
    return;
  }
}

//==============================================================================
//  天氣狀態圖示繪製 (約 32x32 像素)
//==============================================================================
void drawWeatherIcon(int x, int y, int code) {
  // OpenWeatherMap 天氣代碼分類：
  //   800 = 晴天, 80x = 多雲, 5xx = 雨, 2xx = 雷雨,
  //   6xx = 雪, 7xx = 霧/霾, 其餘 = 預設 (波浪底圖)
  if (code == 800) {
    // 晴天：實心太陽圓 + 外圈 + 8 條放射光芒線 (每 45 度一根)
    display.fillCircle(x + 16, y + 16, 12, SSD1306_WHITE);
    display.drawCircle(x + 16, y + 16, 14, SSD1306_WHITE);
    for (int a = 0; a < 360; a += 45) {
      float rad = a * 0.0174533;                       // 角度轉弧度 (180°=π)
      int x1 = x + 16 + cos(rad) * 11;                 // 光線內端 (貼近太陽)
      int y1 = y + 16 + sin(rad) * 11;
      int x2 = x + 16 + cos(rad) * 20;                 // 光線外端 (延伸出去)
      int y2 = y + 16 + sin(rad) * 20;
      display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
    }
  } else if (code >= 801 && code <= 804) {
    // 多雲：三個重疊的實心圓，疊成雲朵造型
    display.fillCircle(x + 10, y + 20, 10, SSD1306_WHITE);
    display.fillCircle(x + 22, y + 20, 10, SSD1306_WHITE);
    display.fillCircle(x + 16, y + 14, 10, SSD1306_WHITE);
  } else if (code >= 500 && code < 600) {
    // 下雨：雲朵造型 + 下方 3 滴雨點像素
    display.fillCircle(x + 10, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 22, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 16, y + 11, 9, SSD1306_WHITE);
    display.drawPixel(x + 8, y + 28, SSD1306_WHITE);   // 左雨滴 (上)
    display.drawPixel(x + 8, y + 29, SSD1306_WHITE);   // 左雨滴 (下)
    display.drawPixel(x + 16, y + 29, SSD1306_WHITE);  // 中雨滴 (上)
    display.drawPixel(x + 16, y + 30, SSD1306_WHITE);  // 中雨滴 (下)
    display.drawPixel(x + 24, y + 28, SSD1306_WHITE);  // 右雨滴 (上)
    display.drawPixel(x + 24, y + 29, SSD1306_WHITE);  // 右雨滴 (下)
  } else if (code >= 200 && code < 300) {
    // 雷雨：雲朵 + 黃色閃電形狀 (鋸齒折線)
    display.fillCircle(x + 10, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 22, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 16, y + 11, 9, SSD1306_WHITE);
    display.drawLine(x + 18, y + 24, x + 12, y + 31, SSD1306_WHITE);  // 閃電左斜線
    display.drawLine(x + 12, y + 31, x + 20, y + 31, SSD1306_WHITE);  // 閃電底部
    display.drawLine(x + 20, y + 31, x + 16, y + 28, SSD1306_WHITE);  // 閃電回勾
  } else if (code >= 600 && code < 700) {
    // 下雪：雲朵 + 3 滴冰點像素 + 垂直掉落線
    display.fillCircle(x + 10, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 22, y + 16, 9, SSD1306_WHITE);
    display.fillCircle(x + 16, y + 11, 9, SSD1306_WHITE);
    display.drawPixel(x + 8, y + 28, SSD1306_WHITE);
    display.drawPixel(x + 8, y + 29, SSD1306_WHITE);
    display.drawPixel(x + 16, y + 29, SSD1306_WHITE);
    display.drawPixel(x + 16, y + 30, SSD1306_WHITE);
    display.drawPixel(x + 24, y + 28, SSD1306_WHITE);
    display.drawPixel(x + 24, y + 29, SSD1306_WHITE);
    display.drawLine(x + 8, y + 27, x + 8, y + 30, SSD1306_WHITE);     // 左雪柱
    display.drawLine(x + 16, y + 28, x + 16, y + 31, SSD1306_WHITE);   // 中雪柱
    display.drawLine(x + 24, y + 27, x + 24, y + 30, SSD1306_WHITE);   // 右雪柱
  } else {
    // 其他 (7xx 霧/霾 等)：底部畫 3 道點狀波浪代表霧氣
    for (int i = 0; i < 3; i++) {
      int waveY = y + 8 + i * 10;                      // 每道波浪的 y 座標
      for (int px = 0; px < 28; px += 4) {
        display.drawPixel(x + 2 + px + ((i & 1) * 2), waveY, SSD1306_WHITE);  // 交錯點陣
      }
    }
  }
}

//==============================================================================
//  RadioControl.h 命令 API 實作 (供 WebPanel 遠端遙控呼叫)
//==============================================================================

// 直接設定頻率 (x10)。同時離開選單、切換到手動模式並校準旋鈕基準。
void radioSetFrequency(int freqX10) {
  if (isScanning || freqX10 <= 0) return;
  freqX10 = constrain(freqX10, FREQ_MIN, FREQ_MAX);

  if (currentMode == MODE_MENU) exitMenu();

  currentMode = MODE_MANUAL;
  currentFreq = freqX10;
  rx.setFrequency(currentFreq);

  // 校準旋鈕基準點，避免下次轉動旋鈕時頻率突然跳動
  lastRawFreq = (int)smoothedFreqADC;
  lastFreqUpdateTime = millis();
  forceDisplayUpdate = true;
}

// 直接設定音量 (0~15)。同步更新旋鈕基準。
void radioSetVolume(int vol) {
  vol = constrain(vol, 0, 15);
  currentVol = vol;

  if (currentVol == 0) {
    rx.setMute(true);
  } else {
    rx.setMute(false);
    int hwVol = ((currentVol - 1) * MAX_HW_VOL / 15) + 1;
    rx.setVolume(hwVol);
  }

  lastRawVol = (int)smoothedVolADC;
  forceDisplayUpdate = true;
}

// 以目前頻率為基準向上 / 向下微調 0.1 MHz
void radioStepFreq(int dir) {
  if (isScanning) return;
  int f = currentFreq + (dir > 0 ? FREQ_STEP : -FREQ_STEP);
  f = constrain(f, FREQ_MIN, FREQ_MAX);
  radioSetFrequency(f);
}

// 開始自動掃描 (先離開選單)
void radioStartScan() {
  if (isScanning) return;
  if (currentMode == MODE_MENU) exitMenu();
  autoScan();
}

// 取消自動掃描
void radioCancelScan() {
  if (isScanning) cancelScan();
}

// 切換到上一個預設頻道 (循環)
void radioNextPreset() {
  if (isScanning) return;
  if (presetCount > 0) {
    if (currentMode == MODE_MENU) exitMenu();
    currentMode = MODE_PRESET;
    currentPresetIdx++;
    if (currentPresetIdx >= presetCount) currentPresetIdx = 0;
    currentFreq = presets[currentPresetIdx];
    rx.setFrequency(currentFreq);
    forceDisplayUpdate = true;
  }
}

// 切換到下一個預設頻道 (循環)
void radioPrevPreset() {
  if (isScanning) return;
  if (presetCount > 0) {
    if (currentMode == MODE_MENU) exitMenu();
    currentMode = MODE_PRESET;
    currentPresetIdx--;
    if (currentPresetIdx < 0) currentPresetIdx = presetCount - 1;
    currentFreq = presets[currentPresetIdx];
    rx.setFrequency(currentFreq);
    forceDisplayUpdate = true;
  }
}

// 儲存目前頻道為預設頻道
void radioSavePreset() {
  if (isScanning || currentMode == MODE_MENU) return;
  if (presetCount >= MAX_PRESETS) return;

  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) return;
  }

  presets[presetCount] = currentFreq;
  presetCount++;
  sortPresets();

  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) {
      currentPresetIdx = i;
      break;
    }
  }

  currentMode = MODE_PRESET;
  savePresets();
  triggerPopup("SAVED!");
}

// 刪除指定索引的預設頻道
void radioDeletePreset(int idx) {
  deleteSpecificPreset(idx);
}

// 清空所有預設頻道
void radioDeleteAllPresets() {
  if (currentMode == MODE_MENU) exitMenu();
  deleteAllPresets();
}

// 切換模式：手動 <-> 預設
void radioToggleMode() {
  if (isScanning) return;

  if (currentMode == MODE_MENU) {
    exitMenu();
    return;
  }

  if (currentMode == MODE_MANUAL) {
    if (presetCount > 0) {
      currentMode = MODE_PRESET;
      currentFreq = presets[currentPresetIdx];
      rx.setFrequency(currentFreq);
    }
  } else {  // MODE_PRESET → MODE_MANUAL
    currentMode = MODE_MANUAL;
    activeKnob = KNOB_NONE;
    lastRawFreq = (int)smoothedFreqADC;
  }
  forceDisplayUpdate = true;
}

// 直接切換到指定索引的預設頻道
void radioSetPreset(int idx) {
  if (isScanning) return;
  if (idx < 0 || idx >= presetCount) return;

  if (currentMode == MODE_MENU) exitMenu();

  currentMode = MODE_PRESET;
  currentPresetIdx = idx;
  currentFreq = presets[currentPresetIdx];
  rx.setFrequency(currentFreq);
  forceDisplayUpdate = true;
}

// 靜音控制
void radioSetMute(bool mute) {
  rx.setMute(mute);
  // 取消靜音且音量為 0 時，自動設為最小可聽音量 (1)
  if (!mute && currentVol == 0) {
    currentVol = 1;
    int hwVol = ((currentVol - 1) * MAX_HW_VOL / 15) + 1;
    rx.setVolume(hwVol);
  }
  forceDisplayUpdate = true;
}

// 取得目前 RSSI 訊號強度
int radioGetRssi() {
  return rx.getRssi();
}

// 取得目前立體聲狀態
bool radioGetStereo() {
  return rx.isStereo();
}

// 取得目前是否靜音
bool radioGetMuted() {
  return rx.isMuted();
}
