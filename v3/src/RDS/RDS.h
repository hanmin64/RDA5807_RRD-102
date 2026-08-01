/*
 * RDS.h — RRD-102 ESP32 FM Radio v3 之 RDS 解碼模組
 * =================================================
 * 封裝 RDA5807 (PU2CLR 函式庫) 的 RDS/RBDS 功能，提供：
 *   - 電台名稱 PS (Program Service，0A/0B 群組)
 *   - 電台文字 RT (RadioText，2A 群組)
 *   - 節目類型 PTY (Program Type) 與文字名稱
 *   - 交通資訊旗標 TP/TA (Traffic Program / Traffic Announcement)
 *   - RDS 時間 CT (Clock-Time，4A 群組，UTC)
 *   - RDS 同步狀態
 *
 * 使用方式：
 *   RDS.begin();      // setup() 中，於 rx.setup() 與 rx.setRDS(true) 後呼叫
 *   RDS.handle();     // loop() 中定期呼叫 (內部以 80ms 間隔輪詢)
 *   RDS.getStation(); // 取得電台名稱字串
 */

#ifndef RDS_H
#define RDS_H

#include <Arduino.h>

class RDSDecoder {
 public:
  // 初始化：清除內部緩衝區與狀態 (RDS 硬體已於 main 中啟用)
  void begin();

  // 於 loop() 中定期呼叫：內部以 80ms 間隔輪詢 RDS 資料
  void handle();

  // --- Getter ---
  bool isSynced() const { return _synced; }                // RDS 是否已同步 (原始 RDSS 位元)
  bool hasStation() const { return _hasStation; }          // 是否已取得穩定的電台名稱
  const char* getStation() const { return _station; }       // PS 電台名稱
  const char* getRadioText() const { return _rt; }          // RT 電台文字
  uint8_t getProgramType() const { return _pty; }           // PTY 代碼 (0~31)
  bool hasTrafficProgram() const { return _tp; }            // TP 交通節目旗標
  bool hasTrafficAnnouncement() const { return _ta; }       // TA 即時路況旗標
  const char* getRdsTime() const { return _time; }          // CT 時間字串 (UTC)
  const char* getPtyName() const;                           // PTY 的文字名稱

  // 清除所有已解碼的資料 (換台時呼叫)
  void clear();

 private:
  bool _synced = false;          // RDS 同步旗標
  bool _hasStation = false;      // 是否已鎖定一個穩定的電台名稱
  char _station[9];              // PS 電台名稱 (最多 8 字元)
  char _stationCandidate[9];     // 尚未確認的電台名稱候選值
  unsigned long _candidateSince = 0;  // 候選值開始維持不變的時間
  unsigned long _lastDataMs = 0;      // 最後一次收到有效 RDS 資料的時間
  char _rt[65];                  // RT 電台文字 (最多 64 字元)
  char _time[16];                // CT 時間字串
  uint8_t _pty = 0;              // PTY 代碼
  bool _tp = false;              // TP 旗標
  bool _ta = false;              // TA 旗標
  unsigned long _lastPoll = 0;   // 上次輪詢時間 (用於 80ms 間隔)
};

// 全域單例 (定義於 RDS.cpp)
extern RDSDecoder RDS;

#endif  // RDS_H
