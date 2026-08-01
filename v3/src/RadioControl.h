/*
 * RadioControl.h — RRD-102 ESP32 FM Radio v3 共用介面
 * ==================================================
 * 此標頭檔定義「收音機核心狀態」與「命令 API」，供以下模組共用：
 *   - main.cpp        (實際定義全域變數並實作命令 API)
 *   - WebPanel/       (Web 遠端遙控面板，透過命令 API 控制收音機)
 *
 * 設計目的：避免 WebPanel 直接依賴 main.cpp 的內部函式，改用一個
 * 明確的「命令介面」來操控收音機，同時保留 v2 既有的全域變數風格。
 */

#ifndef RADIO_CONTROL_H
#define RADIO_CONTROL_H

#include <Arduino.h>

// 最大可儲存預設頻道數 (與 v2 相同)
#define MAX_PRESETS 20

// 系統運作模式 (SystemMode) — 由 v2 main.cpp 移入共用標頭
enum SystemMode {
  MODE_MANUAL,  // 手動調諧模式：旋鈕直接控制 FM 頻率
  MODE_PRESET,  // 預設頻道模式：按上下按鈕切換已儲存的頻道
  MODE_MENU     // 選單模式：進入設定選單進行操作
};

//==============================================================================
//  收音機核心狀態 (全域變數，定義於 main.cpp)
//==============================================================================

// 頻率以 x10 表示：如 8750 = 87.50 MHz、10800 = 108.00 MHz
extern int presets[MAX_PRESETS];  // 已儲存的預設頻道陣列
extern int presetCount;           // 目前預設頻道數量
extern int currentPresetIdx;      // 目前在預設模式下選取的頻道索引
extern int currentFreq;           // 目前播放的 FM 頻率 (x10)
extern int currentVol;            // 目前 UI 音量 (0~15，0=靜音)
extern bool isStereo;             // 目前是否接收立體聲
extern bool isScanning;           // 目前是否正在自動掃描
extern SystemMode currentMode;    // 目前系統運作模式

//==============================================================================
//  收音機命令 API (實作於 main.cpp)
//==============================================================================

// 直接設定頻率 (x10 表示法，0 表示不指定)。
// 若目前在手動模式，會同步更新旋鈕基準點，避免下次轉旋鈕時頻率跳動。
void radioSetFrequency(int freqX10);

// 設定音量 (0~15，0=靜音)。會同步更新音量旋鈕基準點。
void radioSetVolume(int vol);

// 以目前頻率為基準，向上/向下微調頻率。
// dir = +1 向上調、-1 向下調，步進 0.1 MHz (FREQ_STEP)。
void radioStepFreq(int dir);

// 開始全頻段自動掃描。
void radioStartScan();

// 取消目前的自動掃描並還原掃描前狀態。
void radioCancelScan();

// 切換到上一個 / 下一個預設頻道 (循環)。
void radioNextPreset();
void radioPrevPreset();

// 將目前頻率儲存為預設頻道 (自動排序、儲存至 NVS)。
void radioSavePreset();

// 刪除指定索引的預設頻道 (0 起算)。
void radioDeletePreset(int idx);

// 清空所有預設頻道。
void radioDeleteAllPresets();

// 切換模式：手動 <-> 預設 (若有預設頻道時)。
void radioToggleMode();

// 直接切換到指定索引的預設頻道。
void radioSetPreset(int idx);

// 靜音控制 (true = 靜音)。
void radioSetMute(bool mute);

// 取得目前 RSSI 訊號強度 (由 RDA5807 晶片讀取)。
int radioGetRssi();

// 取得目前立體聲狀態。
bool radioGetStereo();

// 取得目前是否靜音。
bool radioGetMuted();

#endif  // RADIO_CONTROL_H
