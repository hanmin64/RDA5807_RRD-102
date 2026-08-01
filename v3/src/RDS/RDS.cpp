/*
 * RDS.cpp — RRD-102 ESP32 FM Radio v3 之 RDS 解碼實作
 * ===================================================
 * 依 PU2CLR RDA5807 函式庫的輪詢 (polling) 模式實作：
 *   if (rx.getRdsReady() && rx.hasRdsInfo()) {
 *       // 讀取目前群組對應的資料 (函式庫內部依群組型別判斷)
 *   }
 *
 * 注意：必須先呼叫 rx.setRDS(true) 啟用 RDS，此模組才能取得資料。
 *       換台時 main.cpp 會呼叫 RDS.clear() 清空緩衝區。
 */

#include "RDS.h"
#include <RDA5807.h>

// 引用 main.cpp 中的 RDA5807 控制物件 (extern 宣告)
extern RDA5807 rx;

// 全域單例實體
RDSDecoder RDS;

// RDS 輪詢間隔 (毫秒)。RDA5807 開啟 FIFO 模式後最多可緩衝 6 組 RDS 資料，
// 每組約 87ms 送達；250ms 輪詢每次取回約 2~3 組，不會漏資料。
// 低頻率也減少對共用 I2C 匯流排的流量。
// (註：先前 OLED 掃描殘影的真正根因已在 vendor 版函式庫修復——
// 原廠 getStatusRegisters() 多餘的 Wire.endTransmission() 會送幽靈 I2C 寫入。)
#define RDS_POLL_INTERVAL_MS 250UL

// PS 電台名稱穩定時間 (毫秒)：名稱需連續維持不變達此時間才會正式顯示，
// 避免名稱逐段累積 (每次 2 字元) 時在螢幕上出現「打字/掃描」效果。
#define PS_STABLE_MS 1500UL

// 超過此時間未收到任何有效 RDS 資料，才認定電台名稱失效 (顯示 No RDS)。
// 期間短暫失去同步不會閃爍切回 No RDS。
#define RDS_TIMEOUT_MS 10000UL

// ------------------------------------------------------------
// 初始化：清除所有內部狀態
// ------------------------------------------------------------
void RDSDecoder::begin() {
  clear();
  Serial.println(F("[RDS] RDS 解碼器已啟動"));
}

// ------------------------------------------------------------
// 清除所有已解碼資料
// ------------------------------------------------------------
void RDSDecoder::clear() {
  _synced = false;
  _hasStation = false;
  _pty = 0;
  _tp = false;
  _ta = false;
  memset(_station, 0, sizeof(_station));
  memset(_stationCandidate, 0, sizeof(_stationCandidate));
  _candidateSince = 0;
  _lastDataMs = 0;
  memset(_rt, 0, sizeof(_rt));
  memset(_time, 0, sizeof(_time));
}

// ------------------------------------------------------------
// 主迴圈處理：每 80ms 輪詢一次 RDS 資料
// ------------------------------------------------------------
void RDSDecoder::handle() {
  unsigned long now = millis();
  if (now - _lastPoll < RDS_POLL_INTERVAL_MS) return;
  _lastPoll = now;

  // 必須先呼叫 getRdsReady()，函式庫會刷新狀態暫存器 (0A~0F)
  if (!rx.getRdsReady()) return;

  // 更新同步旗標 (RDSS bit)
  _synced = rx.getRdsSync();

  // 只有當 RDS 已同步、且 B 區塊無錯誤時才解碼資料
  if (!rx.hasRdsInfo()) return;

  // --- PS 電台名稱 (群組 0A/0B) ---
  // 函式庫會自行檢查目前群組型別；若目前群組非 0 型，會回傳 NULL
  char* ps = rx.getRdsText0A();
  if (ps != nullptr && strlen(ps) > 0) {
    // 去除尾端空白後暫存
    char tmp[9];
    strncpy(tmp, ps, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    for (int i = (int)strlen(tmp) - 1; i >= 0 && tmp[i] == ' '; i--) {
      tmp[i] = '\0';
    }

    _lastDataMs = now;

    if (strcmp(tmp, _station) == 0) {
      // 與已顯示的名稱相同：維持現狀
    } else if (strcmp(tmp, _stationCandidate) != 0) {
      // 名稱仍在逐段累積 / 變動：記下新候選值並重新計時
      strncpy(_stationCandidate, tmp, sizeof(_stationCandidate) - 1);
      _stationCandidate[sizeof(_stationCandidate) - 1] = '\0';
      _candidateSince = now;
    } else {
      // 候選值已連續兩次相同，且持續不變達 PS_STABLE_MS → 正式採用
      if (now - _candidateSince >= PS_STABLE_MS) {
        strncpy(_station, _stationCandidate, sizeof(_station) - 1);
        _station[sizeof(_station) - 1] = '\0';
        _hasStation = true;
      }
    }
  }

  // 長時間未收到任何有效資料 → 電台名稱失效 (避免顯示過時名稱)
  if (now - _lastDataMs > RDS_TIMEOUT_MS) {
    _hasStation = false;
  }

  // --- RT 電台文字 (群組 2A) ---
  char* rt = rx.getRdsText2A();
  if (rt != nullptr && strlen(rt) > 0) {
    strncpy(_rt, rt, sizeof(_rt) - 1);
    _rt[sizeof(_rt) - 1] = '\0';
    for (int i = (int)strlen(_rt) - 1; i >= 0 && _rt[i] == ' '; i--) {
      _rt[i] = '\0';       // 去除尾端空白
    }
  }

  // --- PTY 節目類型 + TP 交通節目旗標 ---
  _pty = rx.getRdsProgramType();
  _tp  = (rx.getRdsTrafficProgramCode() == 1);

  // --- TA 即時交通公告旗標 (僅群組 0 才有意義) ---
  // 函式庫未提供 TA getter，此處由 Block B (register 0x0D) 直接解析。
  // rds_blockb 為函式庫公開型別；group0.TA 即 TA bit (bit 4)。
  if (rx.getRdsGroupType() == 0) {
    rds_blockb blkb;
    blkb.blockB = rx.getDirectRegister(0x0D).raw;
    _ta = blkb.group0.TA == 1;
  }

  // --- CT 時間 (群組 4A，UTC) ---
  char* t = rx.getRdsTime();
  if (t != nullptr && strlen(t) > 0) {
    strncpy(_time, t, sizeof(_time) - 1);
    _time[sizeof(_time) - 1] = '\0';
  }
}

// ------------------------------------------------------------
// PTY (Program Type) 代碼對應之文字名稱
// 對照 EBU/RDS 標準 (EN 50067)
// ------------------------------------------------------------
const char* RDSDecoder::getPtyName() const {
  static const char* names[] = {
    "No PTY",        // 0
    "News",          // 1
    "Current Aff.",  // 2
    "Info",          // 3
    "Sport",         // 4
    "Education",     // 5
    "Drama",         // 6
    "Culture",       // 7
    "Science",       // 8
    "Varied",        // 9
    "Pop Music",     // 10
    "Rock Music",    // 11
    "Easy Music",    // 12
    "Light Classic", // 13
    "Serious Clas.", // 14
    "Other Music",   // 15
    "Weather",       // 16
    "Finance",       // 17
    "Children",      // 18
    "Social",        // 19
    "Religion",      // 20
    "Phone In",      // 21
    "Travel",        // 22
    "Leisure",       // 23
    "Jazz Music",    // 24
    "Country Music", // 25
    "National Mus.", // 26
    "Oldies",        // 27
    "Folk Music",    // 28
    "Documentary",   // 29
    "Alarm Test",    // 30
    "Alarm"          // 31
  };
  if (_pty < 32) return names[_pty];
  return "?";
}
