/*
 * RRD-102 ESP32 FM Radio  v1
 * ==========================
 *
 * 功能概述：
 * - FM 收音機 (87.5-108.0 MHz)
 * - 手動調諧 / 預設頻道 雙模式
 * - 音量控制 (0-15 級，0級完全靜音)
 * - 頻道儲存 (最多 20 個，斷電保存於 NVS)
 * - 全頻段自動掃描 (具備 Peak Detection 過濾旁波帶，掃描前清空舊頻道以防重複)
 * - 頻道自動排序 (掃描後或手動收藏後，頻道皆會依頻率由低至高排列)
 * - 專屬選單系統：選擇、刪除、清空、自動掃描
 * - 狀態列 UI：顯示模式、立體聲(ST)與 RSSI 訊號強度
 * - 智慧提示：手動調諧時動態提示該頻道是否已被收藏
 * - OLED 顯示 (128x64, SSD1306)
 *
 * 硬體平台：ESP32 NodeMCU-32S + RDA5807 FM 晶片
 * 開發框架：Arduino (PlatformIO)
 * 作者/維護者：hanmin64
 */

//==============================================================================
//  引用函式庫
//==============================================================================
#include <Arduino.h>          // Arduino 核心函式庫
#include <Wire.h>             // I2C 通訊函式庫，用於 OLED 和 RDA5807 通訊
#include <Adafruit_GFX.h>     // Adafruit 圖形底層函式庫，提供繪圖基礎功能 (點、線、矩形、文字)
#include <Adafruit_SSD1306.h> // SSD1306 OLED 驅動函式庫 (128x64 解析度)
#include <RDA5807.h>          // RDA5807 FM 收音機晶片控制函式庫 (PU2CLR 版本)
#include <OneButton.h>        // 按鍵處理函式庫，支援短按/長按/雙擊/多種事件
#include <Preferences.h>      // ESP32 NVS (Non-Volatile Storage) 函式庫，斷電保存頻道資料

//==============================================================================
//  OLED 設定 (SSD1306, 128x64, I2C 介面)
//==============================================================================
#define SCREEN_WIDTH  128    // OLED 螢幕寬度 (像素)
#define SCREEN_HEIGHT 64     // OLED 螢幕高度 (像素)
#define OLED_RESET    -1     // OLED 重置腳位 (-1 表示不連接到 ESP32 的任何 GPIO，共用系統重置)
#define OLED_ADDR     0x3C   // SSD1306 OLED 的 I2C 位址

// 建立全域 OLED 顯示物件，底層使用 Wire (I2C) 進行通訊
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//==============================================================================
//  RDA5807 FM 收音機晶片物件
//==============================================================================
RDA5807 rx;  // RDA5807 控制物件，封裝了所有 FM 操作：調頻、音量、RSSI、立體聲偵測等
// RDA5807 的 I2C 位址固定為 0x11 (7-bit)，與 OLED 的 0x3C 不衝突，可共用同一條 I2C 匯流排

//==============================================================================
//  I2C 腳位定義 (ESP32 硬體 I2C，可自定義)
//==============================================================================
#define I2C_SDA  21  // I2C 資料線 (Serial DAta) -> 同時連接 OLED SDA 與 RDA5807 SDA
#define I2C_SCL  22  // I2C 時脈線 (Serial CLock) -> 同時連接 OLED SCL 與 RDA5807 SCL

//==============================================================================
//  輸入腳位定義
//==============================================================================
#define FREQ_PIN      34   // 頻率旋鈕 (電位器) 類比輸入  --> GPIO34 (ADC1_CH6)
#define VOL_PIN       35   // 音量旋鈕 (電位器) 類比輸入  --> GPIO35 (ADC1_CH7)
#define BTN_NEXT_PIN  19   // 上方按鈕 (頻道向上/選單向上/確定)   --> GPIO19 (數位輸入，啟用內部上拉)
#define BTN_PREV_PIN  18   // 下方按鈕 (頻道向下/選單向下/取消)   --> GPIO18 (數位輸入，啟用內部上拉)
//
// 注意：GPIO34、GPIO35 是純輸入腳位 (Input-Only)，沒有內部上拉電阻，適合電位器輸入。
//       GPIO18、GPIO19 是標準雙向 GPIO，啟用內部上拉後可直接接按鈕到 GND。

//==============================================================================
//  按鍵物件 (使用 OneButton 函式庫)
//==============================================================================
// 建構子參數：(pin, activeLow, pullupActive)
//   activeLow=true  -> 按下去為 LOW (接到 GND)
//   pullupActive=true -> 啟用 ESP32 內部上拉電阻
OneButton btnPrev(BTN_PREV_PIN, true, true);  // 下方按鈕
OneButton btnNext(BTN_NEXT_PIN, true, true);  // 上方按鈕

//==============================================================================
//  ADC 濾波與防抖參數
//==============================================================================
//  EMA (Exponential Moving Average) 濾波係數：值越小濾波越平滑，但反應越慢
#define FREQ_EMA_ALPHA     0.10   // 頻率旋鈕 ADC 的 EMA 濾波係數
#define VOL_EMA_ALPHA      0.10   // 音量旋鈕 ADC 的 EMA 濾波係數
#define RSSI_ALPHA         0.30   // RSSI 訊號強度的 EMA 濾波係數 (可稍大，反應快些)

// 死區 (Deadzone)：旋鈕變化量超過此值才觸發更新，可防止微小抖動造成頻繁切換
#define FREQ_DEADZONE      15     // 頻率旋鈕死區 (旋鈕專用，較小值以獲得高靈敏度)
#define GENERAL_DEADZONE   40     // 一般旋鈕死區 (在其他模式或非活躍旋鈕時使用)
#define ADC_MAX            4095   // ESP32 ADC 最大輸出值 (12-bit 解析度，0~4095)
#define FREQ_UPDATE_DELAY  50     // 兩次頻率更新之間的最小間隔 (毫秒)，避免過度寫入 I2C

//==============================================================================
//  自動掃描參數
//==============================================================================
#define RSSI_THRESHOLD  22    // RSSI 門檻值：高於此值才視為有效的廣播電台
#define MAX_PRESETS     20    // 最大可儲存的預設頻道數
#define SCAN_SETTLE_MS  60    // 每次設定頻率後的等待時間 (毫秒)，等待 RDA5807 鎖定訊號
#define SCAN_PAUSE_MS   100   // 找到一個有效頻道後的暫停時間 (毫秒)，讓使用者看到結果
#define FREQ_MIN        8750  // 最低頻率 87.5 MHz (內部以 x10 表示，即 87.5 * 10 = 875)
#define FREQ_MAX        10800 // 最高頻率 108.0 MHz (內部以 x10 表示)
#define FREQ_STEP       10    // 自動掃描頻率步進 (0.1 MHz，即 10 代表 0.1)

//==============================================================================
//  系統狀態列舉
//==============================================================================

// 系統運作模式 (SystemMode)
enum SystemMode {
  MODE_MANUAL,  // 手動調諧模式：旋鈕直接控制 FM 頻率
  MODE_PRESET,  // 預設頻道模式：按上下按鈕切換已儲存的頻道
  MODE_MENU     // 選單模式：進入設定選單進行操作
};

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
// 由於兩個類比旋鈕共享同一組 ADC 並在 loop() 中同時被輪詢，若不加以區分，
// 調頻時可能會影響音量、調音量時也可能影響頻率。此機制用於判斷目前使用者
// 正在操作哪一個旋鈕，使另一個旋鈕的死區擴大，防止誤觸。

enum ActiveKnob {
  KNOB_NONE,  // 沒有任何旋鈕正被操作
  KNOB_VOL,   // 音量旋鈕正被操作 (頻率旋鈕進入高死區)
  KNOB_FREQ   // 頻率旋鈕正被操作 (音量旋鈕進入高死區)
};

ActiveKnob activeKnob = KNOB_NONE;                    // 當前活躍的旋鈕
unsigned long lastKnobTime = 0;                        // 最後一次偵測到旋鈕動作的時間戳記
const unsigned long KNOB_FOCUS_TIMEOUT = 1000;         // 旋鈕專注超時 (毫秒)：若超過此時間無動作，自動釋放焦點
const int CROSS_FOCUS_DEADZONE = 150;                  // 跨旋鈕防干擾死區：當另一個旋鈕活躍時，此值比 GENERAL_DEADZONE 更大

//==============================================================================
//  全域變數
//==============================================================================
SystemMode currentMode = MODE_MANUAL;  // 系統初始模式：手動調諧

// --- 頻道儲存陣列 ---
int presets[MAX_PRESETS];    // 儲存已收藏的頻道值 (單位：0.1MHz，如 8750 = 87.5 MHz)
int presetCount = 0;         // 目前已儲存的頻道數量 (0 ~ MAX_PRESETS)
int currentPresetIdx = 0;    // 當前在預設模式中選擇的頻道索引

// --- ADC 濾波相關 (用於平滑電位器輸入) ---
float smoothedFreqADC = 0;   // 頻率旋鈕的 EMA 濾波後平滑值
float smoothedVolADC = 0;    // 音量旋鈕的 EMA 濾波後平滑值
int lastRawFreq = 0;         // 最後一次觸發頻率更新的原始 ADC 值 (用於死區比較)
int lastRawVol = 0;          // 最後一次觸發音量更新的原始 ADC 值 (用於死區比較)

// --- 當前播放參數 ---
int currentFreq = FREQ_MIN;  // 當前 FM 頻率
int currentVol = 0;          // 當前音量值 (0~15，0=靜音)
int currentRSSI = 0;         // 經四捨五入後的 RSSI 強度
float smoothedRSSI = 0;      // RSSI 的 EMA 濾波平滑值
bool isStereo = false;       // RDA5807 目前是否接收立體聲訊號

// --- 顯示更新控制 ---
unsigned long lastDisplayUpdate = 0;  // 上一次更新 OLED 的時間戳記
bool forceDisplayUpdate = true;       // 設為 true 時強制在下一輪 loop() 更新螢幕 (用於狀態變化時的即時反應)
unsigned long lastFreqUpdateTime = 0; // 上次頻率寫入 RDA5807 的時間戳記 (配合 FREQ_UPDATE_DELAY 限流)

// --- 掃描狀態機 ---
bool isScanning = false;             // 當前是否正在執行自動掃描
int scanFreq = FREQ_MIN;             // 掃描過程中的當前頻率
ScanStep scanStep = SCAN_STEP_SET;   // 掃描狀態機目前在第幾步
unsigned long scanStepTimer = 0;     // 掃描狀態機的計時器 (用於步驟間的延遲控制)
int prevFreqBeforeScan = 0;          // 開始掃描前的頻率，用於取消掃描時還原
SystemMode prevModeBeforeScan;       // 開始掃描前的模式，用於取消掃描時還原

int presetsBeforeScan = 0;           // 掃描前的頻道數量 (備份用)
int presetsBackup[MAX_PRESETS];      // 掃描前的頻道陣列備份 (使用者取消掃描時可完整還原)
int lastScanSavedRSSI = 0;           // 上一個掃描儲存頻道的 RSSI 值，用於 Peak Detection 峰值偵測

// --- 通用 Popup 提示系統 ---
bool showPopup = false;              // 是否正在顯示彈出提示
char popupMsg[16] = "";              // 彈出提示訊息內容 (最多 15 字元 + 結尾 '\0')
unsigned long popupTimer = 0;        // 彈出提示開始顯示的時間戳記

// --- 選單系統 ---
MenuOption currentMenuOption = MENU_SELECT_CH;  // 主選單中目前反白的選項
bool isSubMenu = false;                         // 是否進入子選單 (選擇/刪除頻道的子畫面)
int selectingChannelIdx = 0;                    // 子選單中正在選取的頻道索引

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
void drawRSSI(int rssi, int x, int y);

void btnPrevClick();
void btnNextClick();
void btnPrevLongPress();
void btnNextLongPress();

//==============================================================================
//  防串擾 ADC 讀取函式
//==============================================================================
// ESP32 的 ADC 在切換通道後，取樣保持電容需要時間充電到正確電壓。
// 直接在切換後讀取會得到前一個通道的殘留值，因此：
//   1. 第一次讀取 → 讓 ADC 多工器切換到目標通道 (丟棄)
//   2. 第二次讀取 → 讓取樣保持電容充電 (丟棄)
//   3. 延遲 2ms   → 等待電壓完全穩定
//   4. 第三次讀取 → 回傳穩定值
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
  // 初始化序列埠，用於除錯輸出
  Serial.begin(115200);

  // 設定 ADC：
  //   ADC_11db → 輸入電壓範圍 0~3.6V (最大化解析度)
  //   12-bit   → 讀取值範圍 0~4095
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

  // 啟動 I2C 匯流排：SDA=GPIO21, SCL=GPIO22
  //   OLED (0x3C) 與 RDA5807 (0x11) 共用此匯流排
  Wire.begin(I2C_SDA, I2C_SCL);

  // 初始化 OLED 螢幕
  //   SSD1306_SWITCHCAPVCC → 使用內建電荷泵產生 OLED 驅動電壓
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED init failed!"));  // 若初始化失敗，印出錯誤訊息
    for (;;);                                 // 無窮迴圈停止程式 (需重置 ESP32)
  }
  display.setTextColor(SSD1306_WHITE);       // 設定文字預設顏色：白色像素
  delay(300);                                // 等待 OLED 完全啟動

  // 初始化 RDA5807 FM 收音機晶片 (包含 I2C 通訊檢測與晶片設定)
  rx.setup();

  // 從 ESP32 NVS (非揮發性記憶體) 載入先前儲存的頻道
  loadPresets();

  // 根據載入結果決定初始模式：
  //   若無儲存頻道 → 啟動於手動模式，頻率設為最低 (87.5 MHz)
  //   有儲存頻道   → 啟動於預設模式，從第一個頻道開始播放
  if (presetCount == 0) {
    currentMode = MODE_MANUAL;
    currentFreq = FREQ_MIN;
  } else {
    currentMode = MODE_PRESET;
    currentFreq = presets[0];
    currentPresetIdx = 0;
  }

  // 音量初始為 0 (靜音)，避免開機時爆音或干擾
  // 使用者必須轉動音量旋鈕才能聽到聲音
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

  // 註冊按鍵事件的回呼函式 (Callback)
  btnNext.attachClick(btnNextClick);                    // 短按上方按鈕
  btnNext.attachLongPressStart(btnNextLongPress);       // 長按上方按鈕

  btnPrev.attachClick(btnPrevClick);                    // 短按下方按鈕
  btnPrev.attachLongPressStart(btnPrevLongPress);       // 長按下方按鈕
}

//==============================================================================
//  loop() — 主程式循環，不斷重複執行
//==============================================================================
void loop() {
  // 1. 輪詢按鍵狀態 (OneButton 會自動處理防抖與事件觸發)
  btnPrev.tick();
  btnNext.tick();

  // 2. 檢查旋鈕焦點是否超時，超時則釋放焦點 (避免死佔)
  updateKnobFocus();

  // 3. 處理音量旋鈕 (所有模式下都持續偵測音量變化)
  processVolume();

  // 4. 依據目前模式處理不同操作邏輯：
  //    - 掃描中 → 執行掃描狀態機
  //    - 手動模式 → 處理頻率旋鈕
  //    - 選單的子選單模式 → 處理頻率旋鈕 (用於選擇頻道)
  if (isScanning) {
    handleScanStep();
  } else if (currentMode == MODE_MANUAL) {
    processFrequency();
  } else if (currentMode == MODE_MENU && isSubMenu) {
    processMenuKnob();
  }

  // 5. 更新 OLED 顯示 (若有需要)
  updateDisplayTask();

  // 6. 主循環延遲 10ms，約等於 100Hz 更新率
  //    這能有效降低 CPU 使用率，同時保持 UI 操作的反應速度
  delay(10);
}

//==============================================================================
//  旋鈕互斥與自動校準 (Knob Focus Management)
//==============================================================================
// 當某個旋鈕被操作後，activeKnob 會被設為對應值，並記錄最後操作時間。
// 若超過 KNOB_FOCUS_TIMEOUT (1 秒) 沒有任何旋鈕活動，則自動釋放焦點，
// 並將 lastRawFreq/lastRawVol 更新為目前的平滑值，避免下次操作時突然跳動。
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
// 觸發一個短暫的螢幕提示訊息 (例如 "SAVED!"、"DELETED!"、"NO SAVED CH")。
// 訊息會在顯示 800ms 後自動消失，由 updateDisplay() 管理。
void triggerPopup(const char* msg) {
  strncpy(popupMsg, msg, sizeof(popupMsg) - 1);      // 複製訊息 (防止緩衝區溢位)
  popupMsg[sizeof(popupMsg) - 1] = '\0';              // 確保字串結尾為 null
  showPopup = true;                                    // 啟動 Popup 顯示旗標
  popupTimer = millis();                               // 記錄顯示起始時間
  forceDisplayUpdate = true;                           // 強制立即更新螢幕
}

//==============================================================================
//  音量控制邏輯
//==============================================================================
// RDA5807 的 setVolume() 支援 0~15 級音量，但考量 PAM8403 放大器 + 小喇叭
// 在較高音量時容易失真，因此做了兩層映射：
//   - displayVol (UI 顯示音量)：0~15，與使用者直覺對應
//   - hwVol (實際寫入 RDA5807 的硬體音量)：1~5，等份映射
// 這樣當使用者轉到高音量區間時，硬體增益不會過高，避免喇叭破音。
#define MAX_HW_VOL 5

void processVolume() {
  // 讀取音量電位器的原始 ADC 值，經 3 次取樣穩定
  int rawVol = safeAnalogRead(VOL_PIN);

  // EMA 低通濾波：新值 = α * 原始值 + (1-α) * 舊平滑值
  // 效果等同於一階 RC 低通濾波器，可消除瞬間雜訊
  smoothedVolADC = (VOL_EMA_ALPHA * rawVol) + ((1 - VOL_EMA_ALPHA) * smoothedVolADC);

  // 若頻率旋鈕正活躍中，音量旋鈕的死區放大 (CROSS_FOCUS_DEADZONE)，
  // 避免調整頻率時無意間動到音量
  int effectiveDeadzone = (activeKnob == KNOB_FREQ) ? CROSS_FOCUS_DEADZONE : GENERAL_DEADZONE;

  // 只有當平滑值的變化量超過有效死區時才更新音量
  if (abs(smoothedVolADC - lastRawVol) > effectiveDeadzone) {
    lastRawVol = (int)smoothedVolADC;    // 記錄目前的 ADC 值作為下次比較基準
    activeKnob = KNOB_VOL;                // 宣告音量旋鈕為活躍狀態
    lastKnobTime = millis();              // 更新旋鈕活動時間

    // 將 0~4095 的 ADC 值映射到 UI 音量 0~15 (四捨五入)
    float normalizedVol = smoothedVolADC / (float)ADC_MAX;
    int displayVol = (int)(normalizedVol * 15.0f + 0.5f);
    displayVol = constrain(displayVol, 0, 15);

    // 將 UI 音量 (1~15) 映射到硬體音量 (1~MAX_HW_VOL)：
    //   公式：hwVol = ((displayVol - 1) * MAX_HW_VOL / 15) + 1
    //   確保每個硬體音量級別對應 3 個 UI 音量級別
    int hwVol = 0;
    if (displayVol > 0) {
      hwVol = ((displayVol - 1) * MAX_HW_VOL / 15) + 1;
    }
    // 當 displayVol=0 → hwVol=0 (靜音模式)
    //    displayVol=1 → hwVol=1, displayVol=5 → hwVol=2, ..., displayVol=15 → hwVol=5

    // 只有在音量值真正改變時才寫入 RDA5807 晶片與更新螢幕
    if (displayVol != currentVol) {
      currentVol = displayVol;

      if (currentVol == 0) {
        rx.setMute(true);      // 音量為 0 → 啟用靜音 (Mute)
      } else {
        rx.setMute(false);     // 解除靜音
        rx.setVolume(hwVol);   // 設定硬體音量 (限縮範圍以防失真)
      }
      forceDisplayUpdate = true;
    }
  }
}

//==============================================================================
//  頻率控制邏輯 (僅在手動模式 MODE_MANUAL 下執行)
//==============================================================================
void processFrequency() {
  // 讀取頻率電位器的原始 ADC 值，經 3 次取樣穩定
  int rawFreq = safeAnalogRead(FREQ_PIN);

  // EMA 低通濾波
  smoothedFreqADC = (FREQ_EMA_ALPHA * rawFreq) + ((1 - FREQ_EMA_ALPHA) * smoothedFreqADC);

  // 若音量旋鈕正活躍中，頻率旋鈕的死區放大，防止誤觸
  int effectiveDeadzone = (activeKnob == KNOB_VOL) ? CROSS_FOCUS_DEADZONE : FREQ_DEADZONE;

  // 變化量超過死區才處理，避免 ADC 微小抖動造成頻繁切換
  if (abs(smoothedFreqADC - lastRawFreq) > effectiveDeadzone) {
    // 加上更新間隔限制，避免短時間內連續寫入 I2C
    if (millis() - lastFreqUpdateTime > FREQ_UPDATE_DELAY) {
      lastRawFreq = (int)smoothedFreqADC;    // 記錄基底值
      activeKnob = KNOB_FREQ;                // 宣告頻率旋鈕為活躍狀態
      lastKnobTime = millis();

      // ADC 值 (0~4095) 映射到 FM 頻率 (87.5~108.0 MHz)，單位為 0.1 MHz：
      //   映射公式：mappedFreq = ADC × (1080-875) / 4095 + 875
      //   帶四捨五入：ADC × 205 / 4095 + 875 + 0.5
      //   ADC=0    → 875  (87.5 MHz)
      //   ADC=4095 → 1080 (108.0 MHz)
      int mappedFreq = (int)(smoothedFreqADC * 205.0f / ADC_MAX + 875.0f + 0.5f);
      mappedFreq = constrain(mappedFreq, 875, 1080);

      // 轉換為內部表示法 (x10)：如 875 → 8750 (代表 87.5 MHz)
      int targetFreq = mappedFreq * 10;

      // 若頻率確實有變才寫入晶片及更新畫面
      if (targetFreq != currentFreq) {
        currentFreq = targetFreq;
        rx.setFrequency(currentFreq);    // 透過 I2C 寫入 RDA5807
        forceDisplayUpdate = true;       // 強制更新螢幕
        lastFreqUpdateTime = millis();   // 記錄更新時間 (用於限流)
      }
    }
  }
}

//==============================================================================
//  選單旋鈕控制 (在選單子畫面中用頻率旋鈕選擇頻道)
//==============================================================================
// 在子選單中，頻率電位器被重新用途為頻道選擇器：
//   將 ADC 值映射到已儲存頻道的索引範圍 (0 ~ presetCount-1)
void processMenuKnob() {
  int rawFreq = safeAnalogRead(FREQ_PIN);
  smoothedFreqADC = (FREQ_EMA_ALPHA * rawFreq) + ((1 - FREQ_EMA_ALPHA) * smoothedFreqADC);

  int effectiveDeadzone = (activeKnob == KNOB_VOL) ? CROSS_FOCUS_DEADZONE : GENERAL_DEADZONE;

  if (abs(smoothedFreqADC - lastRawFreq) > effectiveDeadzone) {
    if (millis() - lastFreqUpdateTime > FREQ_UPDATE_DELAY) {
      lastRawFreq = (int)smoothedFreqADC;
      activeKnob = KNOB_FREQ;
      lastKnobTime = millis();

      // ADC 值等比例映射到頻道索引
      //   注意：分母使用 ADC_MAX + 1 確保索引值永遠小於 presetCount，避免溢界
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
// 使用氣泡排序法將 presets 陣列依頻率由低至高重新排列。
// 每次使用者新增頻道或自動掃描完成後都會呼叫此函式。
// 雖然氣泡排序的時間複雜度為 O(n²)，但 n ≤ 20，效能影響微乎其微。
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

// 進入主選單
void enterMenu() {
  currentMode = MODE_MENU;            // 切換為選單模式
  currentMenuOption = MENU_SELECT_CH; // 預設停在第一個選項
  isSubMenu = false;                   // 非子選單
  forceDisplayUpdate = true;
}

// 離開選單，回到先前的模式 (依有無儲存頻道決定回 PRESET 或 MANUAL)
void exitMenu() {
  if (presetCount > 0) {
    currentMode = MODE_PRESET;
  } else {
    currentMode = MODE_MANUAL;
  }
  isSubMenu = false;
  forceDisplayUpdate = true;
}

// 選單導航：direction = -1 表示往上 (Next 按鈕)，1 表示往下 (Prev 按鈕)
// 在子選單中，方向為 -1 (Next) 時回到主選單；方向為 1 (Prev) 時無作用
void handleMenuNav(int direction) {
  if (isSubMenu) {
    if (direction == -1) {
      isSubMenu = false;
      forceDisplayUpdate = true;
    }
    return;
  }
  // 主選單選項循環：0~4
  int maxOption = 4;
  currentMenuOption = (MenuOption)((currentMenuOption + direction + maxOption + 1) % (maxOption + 1));
  forceDisplayUpdate = true;
}

// 選單選擇確認 (長按 Next 按鈕觸發)
void handleMenuSelect() {
  // 若在子選單中：
  if (isSubMenu) {
    if (currentMenuOption == MENU_SELECT_CH) {
      // 選擇頻道 → 切換到該頻道並離開選單
      currentPresetIdx = selectingChannelIdx;
      currentFreq = presets[currentPresetIdx];
      rx.setFrequency(currentFreq);
      exitMenu();
    } else if (currentMenuOption == MENU_DELETE_CH) {
      // 刪除頻道 → 直接刪除，無二次確認 (v1 設計)
      deleteSpecificPreset(selectingChannelIdx);
      isSubMenu = false;
      if (presetCount == 0) exitMenu();  // 刪光所有頻道後自動離開
    }
    return;
  }

  // 主選單選項處理
  switch (currentMenuOption) {
    case MENU_SELECT_CH:
      if (presetCount > 0) {
        isSubMenu = true;
        selectingChannelIdx = currentPresetIdx;  // 從當前頻道開始選
        activeKnob = KNOB_NONE;                   // 重置旋鈕焦點
        lastRawFreq = (int)smoothedFreqADC;       // 校準基準點
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
      exitMenu();    // 先退出選單再開始掃描，讓掃描畫面完整顯示
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
// 刪除後會將後方所有頻道往前平移，並更新 currentPresetIdx。
// 若刪除的是當前播放的頻道，則自動切換到下一個可用頻道。
void deleteSpecificPreset(int idx) {
  // 邊界檢查
  if (presetCount <= 0 || idx >= presetCount) return;

  // 將後方元素往前平移，覆蓋掉要刪除的項目
  for (int i = idx; i < presetCount - 1; i++) {
    presets[i] = presets[i + 1];
  }
  presetCount--;

  // 更新當前頻道索引：
  //   - 若刪除的是當前的索引，則往後退一格 (或退到最後一格)
  //   - 若刪除的索引在當前之前，則當前索引減 1
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

  savePresets();              // 立即寫入 NVS 保存
  triggerPopup("DELETED!");
}

//==============================================================================
//  刪除所有頻道
//==============================================================================
void deleteAllPresets() {
  presetCount = 0;
  currentPresetIdx = 0;
  currentFreq = FREQ_MIN;     // 回到最低頻率
  savePresets();
  triggerPopup("CLEARED!");
}

//==============================================================================
//  按鍵事件回呼 — 短按
//==============================================================================

// 上方按鈕短按 (btnNextClick)：
//   - 選單模式 → 選單向上導航 (或子選單中回到主選單)
//   - 預設模式 → 切換到上一個頻道 (循環)
//   - 掃描中    → 忽略
//   - 手動模式 → 忽略
void btnNextClick() {
  if (isScanning) return;

  if (currentMode == MODE_MENU) {
    handleMenuNav(-1);    // -1 = 向上
    return;
  }

  if (currentMode == MODE_PRESET && presetCount > 0) {
    // 輪播到下一個頻道 (循環)
    currentPresetIdx++;
    if (currentPresetIdx >= presetCount) currentPresetIdx = 0;
    currentFreq = presets[currentPresetIdx];
    rx.setFrequency(currentFreq);
    forceDisplayUpdate = true;
  }
}

// 下方按鈕短按 (btnPrevClick)：
//   - 掃描中    → 取消掃描
//   - 選單模式 → 選單向下導航
//   - 預設模式 → 切換到上一個頻道 (循環)
//   - 手動模式 → 忽略
void btnPrevClick() {
  if (isScanning) {
    cancelScan();
    return;
  }

  if (currentMode == MODE_MENU) {
    handleMenuNav(1);    // 1 = 向下
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

// 上方按鈕長按 (btnNextLongPress)：
//   - 掃描中    → 忽略
//   - 選單模式 → 確認選取 (選擇/刪除/執行)
//   - 預設模式 → 進入選單
//   - 手動模式 → 儲存當前頻道 (若尚未儲存且未達上限)
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

  // 檢查是否已經儲存過此頻率，避免重複
  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) return;
  }

  // 新增頻道到陣列尾部
  presets[presetCount] = currentFreq;
  presetCount++;

  // 排序 (由低到高)
  sortPresets();

  // 尋找排序後當前頻率的新索引
  for (int i = 0; i < presetCount; i++) {
    if (presets[i] == currentFreq) {
      currentPresetIdx = i;
      break;
    }
  }

  // 自動切換到預設模式
  currentMode = MODE_PRESET;
  savePresets();
  triggerPopup("SAVED!");
}

// 下方按鈕長按 (btnPrevLongPress)：
//   - 掃描中    → 取消掃描
//   - 選單模式 → 離開選單
//   - 手動模式 → 若有不為空的頻道列表 → 切換到預設模式
//   - 預設模式 → 切換到手動模式
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
    // MODE_PRESET → MODE_MANUAL
    currentMode = MODE_MANUAL;
    activeKnob = KNOB_NONE;
    lastRawFreq = (int)smoothedFreqADC;   // 校準基準，避免切換後頻率跳動
  }
  forceDisplayUpdate = true;
}

//==============================================================================
//  自動掃描邏輯
//==============================================================================

// 啟動自動掃描：
//   1. 備份當前模式、頻率及所有已儲存頻道 (以便取消時還原)
//   2. 清空 presets 陣列
//   3. 從 87.5 MHz 開始逐頻點掃描到 108.0 MHz
void autoScan() {
  prevFreqBeforeScan = currentFreq;
  prevModeBeforeScan = currentMode;
  presetsBeforeScan = presetCount;

  // 備份現有頻道到 presetsBackup 陣列
  for (int i = 0; i < presetCount; i++) {
    presetsBackup[i] = presets[i];
  }
  presetCount = 0;   // 清空，從零開始掃描 (確保掃描結果不重複)

  isScanning = true;
  scanFreq = FREQ_MIN;
  scanStep = SCAN_STEP_SET;
  scanStepTimer = millis();
  lastScanSavedRSSI = 0;

  forceDisplayUpdate = true;
}

// 取消掃描：回復到掃描前的狀態 (頻率、模式、頻道清單)
void cancelScan() {
  isScanning = false;
  currentFreq = prevFreqBeforeScan;
  currentMode = prevModeBeforeScan;
  currentPresetIdx = 0;

  // 從備份陣列還原所有頻道
  presetCount = presetsBeforeScan;
  for (int i = 0; i < presetCount; i++) {
    presets[i] = presetsBackup[i];
  }

  rx.setFrequency(currentFreq);
  forceDisplayUpdate = true;
}

// 掃描完成：
//   若掃到至少一個頻道 → 排序、切換到預設模式、儲存到 NVS
//   若完全沒掃到       → 建立一個預設頻道 (最低頻率)、回到手動模式
void finishScan() {
  isScanning = false;
  if (presetCount > 0) {
    sortPresets();                 // 掃描過程中 presets 依序加入，但仍做最後排序確保正確
    currentFreq = presets[0];      // 自動播放找到的第一個頻道
    currentMode = MODE_PRESET;
    currentPresetIdx = 0;
    savePresets();                 // 儲存掃描結果到 NVS
  } else {
    // 完全沒收到任何電台時的備案
    presetCount = 1;
    presets[0] = FREQ_MIN;
    currentFreq = FREQ_MIN;
    currentMode = MODE_MANUAL;
  }
  rx.setFrequency(currentFreq);
  forceDisplayUpdate = true;
}

// 掃描狀態機處理 (每次 loop() 呼叫一次)
void handleScanStep() {
  unsigned long now = millis();
  switch (scanStep) {
    case SCAN_STEP_SET:
      // 1. 設定 RDA5807 到目標頻率
      rx.setFrequency(scanFreq);
      scanStepTimer = now;
      scanStep = SCAN_STEP_WAIT;   // 進入等待步驟
      break;

    case SCAN_STEP_WAIT:
      // 2. 等待 SCAN_SETTLE_MS 讓頻率鎖定
      if (now - scanStepTimer < SCAN_SETTLE_MS) return;

      {
        int rssi = rx.getRssi();   // 讀取訊號強度

        if (rssi >= RSSI_THRESHOLD) {
          //== Peak Detection 峰值偵測 ==
          // 若當前頻率與上一個已儲存頻率差距 ≤ 0.3 MHz (30)，
          // 表示可能是旁波帶 (Adjacent Channel)，而非真正的電台。
          // 此時若當前 RSSI 比之前存的高，則取代上一個頻道 (保留訊號最強的那個)。
          if (presetCount > 0 && (scanFreq - presets[presetCount - 1] <= 30)) {
            if (rssi > lastScanSavedRSSI) {
              presets[presetCount - 1] = scanFreq;   // 取代 (更新為較強訊號的頻率)
              lastScanSavedRSSI = rssi;

              scanStep = SCAN_STEP_PAUSE;
              scanStepTimer = now;
              forceDisplayUpdate = true;
              return;
            }
            // 若訊號沒比較強，則略過此頻率 (不儲存也不暫停)
          } else if (presetCount < MAX_PRESETS) {
            // 全新電台，直接儲存
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
      // 此頻率無有效訊號 → 直接前進到下一個頻率
      advanceScanStep();
      break;

    case SCAN_STEP_PAUSE:
      // 3. 找到有效電台後，短暫暫停讓使用者看到
      if (now - scanStepTimer < SCAN_PAUSE_MS) return;
      advanceScanStep();
      break;
  }
}

// 前進到下一個掃描頻率；若已達上限則結束掃描
void advanceScanStep() {
  scanFreq += FREQ_STEP;
  if (scanFreq > FREQ_MAX) {
    finishScan();               // 掃完整個頻段
  } else {
    scanStep = SCAN_STEP_SET;   // 繼續掃下一個頻率
    forceDisplayUpdate = true;
  }
}

//==============================================================================
//  NVS 持久化儲存 (Preferences)
//==============================================================================
// 使用 ESP32 的 NVS (Non-Volatile Storage) 模組將頻道資料寫入 flash。
// 儲存結構：
//   "count" → 頻道總數 (int)
//   "p0" ~ "p19" → 每個頻道的頻率值 (int)

// 儲存頻道到 NVS
void savePresets() {
  Preferences prefs;
  prefs.begin("radio", false);             // 開啟 "radio" 命名空間，讀寫模式
  prefs.putInt("count", presetCount);      // 儲存頻道數量
  for (int i = 0; i < presetCount; i++) {
    String key = "p" + String(i);          // 鍵名："p0", "p1", ...
    prefs.putInt(key.c_str(), presets[i]); // 儲存每個頻道的頻率值
  }
  prefs.end();                             // 關閉 NVS，確保資料寫入
}

// 從 NVS 載入頻道 (若無資料則使用預設頻道清單)
void loadPresets() {
  Preferences prefs;
  prefs.begin("radio", true);              // 開啟 "radio" 命名空間，唯讀模式
  int savedCount = prefs.getInt("count", 0); // 讀取頻道數量，若無資料則回傳 0
  prefs.end();                             // 先關閉，避免後續重新開啟時發生衝突

  if (savedCount == 0) {
    // 初次使用或 NVS 無資料 → 載入預設頻道清單 (台灣 FM 電台頻率)
    int defaultStations[] = {
      8830, 8970,  9010, 9130, 9170, 9210, 9270, 9430, 9630, 9770,
      9810, 9890,  9970, 10070, 10170, 10250, 10330, 10410, 10490, 10770
    };

    presetCount = sizeof(defaultStations) / sizeof(defaultStations[0]);
    if (presetCount > MAX_PRESETS) presetCount = MAX_PRESETS;

    for (int i = 0; i < presetCount; i++) {
      presets[i] = defaultStations[i];
    }
    sortPresets();   // 排序確保順序正確
    savePresets();   // 將預設值寫入 NVS，下次開機就能直接載入

  } else {
    // NVS 已有資料 → 逐一讀取每個頻道
    prefs.begin("radio", true);
    presetCount = savedCount;
    if (presetCount <= 0 || presetCount > MAX_PRESETS) presetCount = 1;
    for (int i = 0; i < presetCount; i++) {
      String key = "p" + String(i);
      presets[i] = prefs.getInt(key.c_str(), FREQ_MIN);  // 若鍵不存在則用預設值
    }
    prefs.end();
  }
}

//==============================================================================
//  OLED 顯示更新
//==============================================================================

// 顯示任務排程器 (由 loop() 呼叫)
// 每 500ms 自動刷新畫面；當 forceDisplayUpdate 為 true 時立即刷新
void updateDisplayTask() {
  if (forceDisplayUpdate || millis() - lastDisplayUpdate > 500) {
    if (!isScanning) {
      // 非掃描狀態下更新 RSSI 與立體聲狀態 (掃描時由 handleScanStep 處理)
      int rawRSSI = rx.getRssi();
      smoothedRSSI = (RSSI_ALPHA * rawRSSI) + ((1 - RSSI_ALPHA) * smoothedRSSI);
      currentRSSI = (int)(smoothedRSSI + 0.5f);    // 四捨五入

      isStereo = rx.isStereo();                    // 查詢 RDA5807 是否收到立體聲
    }
    updateDisplay();
    lastDisplayUpdate = millis();
    forceDisplayUpdate = false;
  }
}

// 螢幕繪製主函式 (根據目前系統狀態繪製不同畫面)
void updateDisplay() {
  display.clearDisplay();

  // --- 1. Popup 提示畫面 (優先級最高) ---
  if (showPopup) {
    if (millis() - popupTimer < 800) {        // 顯示 800ms
      display.setTextSize(2);                  // 大字 (12x16 像素)
      display.setCursor(10, 24);               // 垂直置中約略位置
      display.print(popupMsg);
      display.display();
      return;
    } else {
      showPopup = false;                       // 超時自動關閉
    }
  }

  // --- 2. 掃描進行中畫面 ---
  if (isScanning) {
    display.setTextSize(1);                    // 小字
    display.setCursor(0, 20);
    display.print(F(" Auto Scanning..."));

    // 繪製進度條 (外框 + 填色)
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
      // 主選單：5 個選項，目前選取項以反白顯示
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
          // 反白：黑字白底
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          display.print(menuItems[i]);
          display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);  // 恢復
        } else {
          display.print(menuItems[i]);
        }
      }
    } else {
      // 子選單 (選擇頻道 / 刪除頻道)
      display.setTextSize(1);
      display.setCursor(0, 0);
      if (currentMenuOption == MENU_SELECT_CH) {
        display.print(F("--- SELECT CH ---"));
      } else {
        display.print(F("--- DELETE CH ---"));
      }

      // 大字顯示頻道頻率
      display.setTextSize(2);
      display.setCursor(10, 24);
      display.print(presets[selectingChannelIdx] / 100.0f, 1);   // 轉換為 MHz 顯示
      display.print(F(" MHz"));

      // 底部資訊：頻道編號 / 總數
      display.setTextSize(1);
      display.setCursor(0, 54);
      display.print(F("CH:"));
      display.print(selectingChannelIdx + 1);
      display.print('/');
      display.print(presetCount);

      // 操作提示
      display.setCursor(52, 54);
      display.print(F("Hold Up:OK"));
    }
    display.display();
    return;
  }

  // --- 4. 正常播放畫面 (MANUAL / PRESET) ---

  display.setTextSize(1);

  // ---- 狀態列 (第一行) ----
  // 模式標籤 (黑底白字)
  if (currentMode == MODE_MANUAL) {
    display.fillRect(0, 0, 42, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(3, 2);
    display.print(F("MANUAL"));
  } else {
    display.fillRect(0, 0, 42, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(3, 2);
    display.print(F("PRESET"));
  }
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  // 立體聲 / 單聲道指示
  display.setCursor(65, 2);
  if (isStereo) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(F(" ST "));
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  } else {
    display.print(F("MONO"));
  }

  // 狀態列分隔線
  display.drawLine(0, 13, 128, 13, SSD1306_WHITE);

  // 右上角 RSSI 訊號強度圖示
  drawRSSI(currentRSSI, 110, 0);

  // ---- 中央大字頻率 (第二行) ----
  display.setTextSize(2);
  display.setCursor(10, 26);
  display.print(currentFreq / 100.0f, 1);   // 顯示如 " 88.5 MHz"
  display.print(F(" MHz"));

  // ---- 底部資訊列 (第三行) ----
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print(F("Vol:"));
  if (currentVol < 10) display.print(' ');   // 個位數前面補空格，維持對齊
  display.print(currentVol);

  // 右側：預設模式顯示頻道編號，手動模式顯示儲存提示
  if (currentMode == MODE_PRESET) {
    display.setCursor(64, 54);
    display.print(F("CH:"));
    display.print(currentPresetIdx + 1);
    display.print('/');
    display.print(presetCount);
  } else {
    // 搜尋目前頻率是否已在收藏清單中
    int savedIndex = -1;
    for (int i = 0; i < presetCount; i++) {
      if (presets[i] == currentFreq) {
        savedIndex = i;
        break;
      }
    }

    display.setCursor(52, 54);
    if (savedIndex != -1) {
      display.print(F("Saved CH:"));
      display.print(savedIndex + 1);
    } else {
      display.print(F("Hold Up:SAVE"));   // 提示使用者長按上方按鈕可儲存
    }
  }

  display.display();
}

//==============================================================================
//  RSSI 訊號強度圖示繪製
//==============================================================================
// 繪製 4 條垂直長條，由左至右代表訊號強度遞增。
// 門檻值：>10 → 1 格, >18 → 2 格, >25 → 3 格, >35 → 4 格
// 線條高度遞增 (2, 4, 6, 8 像素)，佔用空間 4px 寬 × 10px 高
void drawRSSI(int rssi, int x, int y) {
  int bars = 0;
  if (rssi > 10) bars = 1;
  if (rssi > 18) bars = 2;
  if (rssi > 25) bars = 3;
  if (rssi > 35) bars = 4;

  for (int i = 0; i < 4; i++) {
    int h = 2 + (i * 2);                     // 高度遞增：2, 4, 6, 8
    if (i < bars) {
      display.fillRect(x + (i * 4), y + 8 - h, 3, h, SSD1306_WHITE);  // 填滿 (亮)
    } else {
      display.drawRect(x + (i * 4), y + 8 - h, 3, h, SSD1306_WHITE);  // 空心 (暗)
    }
  }
}
