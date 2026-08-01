/*
 * WebPanel.cpp — RRD-102 ESP32 FM Radio v3 之 Web 遠端遙控面板實作
 * ================================================================
 * 提供手機/電腦可操作的網頁遙控面板、JSON API 與 OTA 無線更新。
 *
 * 硬體接線不變，僅在 v2 既有的 WiFi 連線之上新增此 WebServer。
 */

#include "WebPanel.h"

#include <WiFi.h>          // WiFi 狀態 / IP 資訊
#include <ESPmDNS.h>       // mDNS 服務 (esp32-radio.local)
#include <Update.h>        // OTA 無線韌體更新核心
#include <ArduinoJson.h>   // 建立 /api/status 的 JSON 回應

#include "RadioControl.h"  // 收音機狀態與命令 API
#include "../RDS/RDS.h"    // RDS 解碼器 (電台名稱 / 文字 / 節目類型)
#include "../NTPTime/NTPTime.h"  // NTP 時鐘
#include "../Weather/Weather.h"  // OpenWeatherMap 天氣

// 全域單例實體
WebPanelMgr Web;

// ------------------------------------------------------------------
// 初始化：啟動 mDNS 與 WebServer，註冊路由
// 需在 WiFi 連線成功後呼叫 (通常於 setup() 中、WiFiMgr.begin() 之後)
// ------------------------------------------------------------------
void WebPanelMgr::begin() {
  // 啟動 mDNS，方便以 http://esp32-radio.local 存取
  if (MDNS.begin("esp32-radio")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println(F("[Web] mDNS 已啟動: http://esp32-radio.local"));
  } else {
    Serial.println(F("[Web] mDNS 啟動失敗"));
  }

  // 註冊 HTTP 路由
  _server.on("/", HTTP_GET, [this]() { handleRoot(); });            // 遙控面板首頁
  _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); }); // 狀態 JSON
  _server.on("/api/freq", HTTP_POST, [this]() { handleSetFreq(); }); // 設定頻率
  _server.on("/api/vol", HTTP_POST, [this]() { handleSetVol(); });   // 設定音量
  _server.on("/api/cmd", HTTP_POST, [this]() { handleCommand(); });  // 控制命令
  _server.on("/update", HTTP_POST,
             [this]() { handleUpdate(); },         // OTA 上傳完成
             [this]() { handleUpdateUpload(); });  // OTA 上傳中 (分段接收)
  _server.onNotFound([this]() { handleNotFound(); });
  _server.begin();

  Serial.println(F("[Web] 遙控面板伺服器已啟動 (port 80)"));
  Serial.print(F("[Web] 請以瀏覽器開啟 http://"));
  Serial.println(WiFi.localIP());
}

// ------------------------------------------------------------------
// 主迴圈處理：於 loop() 中定期呼叫，處理 HTTP 請求
// OTA 上傳期間仍須持續呼叫此函式以接收韌體資料分段
// ------------------------------------------------------------------
void WebPanelMgr::handle() {
  _server.handleClient();
}

// ------------------------------------------------------------------
// 建立目前收音機狀態的 JSON 字串 (供前端每 1 秒輪詢)
// ------------------------------------------------------------------
String WebPanelMgr::buildStatusJSON() {
  static JsonDocument doc;   // 重用同一份 JsonDocument 減少 heap 碎片
  doc.clear();

  // --- 收音機基本狀態 ---
  const char* modeStr = "manual";
  if (currentMode == MODE_PRESET) modeStr = "preset";
  else if (currentMode == MODE_MENU) modeStr = "menu";
  doc["mode"] = modeStr;
  doc["freq"] = currentFreq;          // x10 表示 (8750 = 87.5 MHz)
  doc["vol"] = currentVol;            // 0~15
  doc["muted"] = radioGetMuted();
  doc["stereo"] = radioGetStereo();
  doc["rssi"] = radioGetRssi();
  doc["scanning"] = isScanning;
  doc["count"] = presetCount;
  doc["idx"] = currentPresetIdx;

  // --- 預設頻道清單 ---
  JsonArray presetsArr = doc["presets"].to<JsonArray>();
  for (int i = 0; i < presetCount; i++) presetsArr.add(presets[i]);

  // --- RDS 資訊 ---
  JsonObject rds = doc["rds"].to<JsonObject>();
  rds["synced"] = RDS.isSynced();
  rds["station"] = RDS.getStation();
  rds["text"] = RDS.getRadioText();
  rds["pty"] = RDS.getProgramType();
  rds["ptyName"] = RDS.getPtyName();
  rds["tp"] = RDS.hasTrafficProgram();
  rds["ta"] = RDS.hasTrafficAnnouncement();
  rds["time"] = RDS.getRdsTime();

  // --- WiFi 狀態 ---
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = WiFi.SSID();
  wifi["ip"] = WiFi.localIP().toString();
  wifi["rssi"] = WiFi.RSSI();

  // --- 時鐘 ---
  doc["clock"] = NTP.getDateTime("%Y-%m-%d %H:%M:%S");

  // --- 天氣 (僅在已取得時) ---
  JsonObject weather = doc["weather"].to<JsonObject>();
  weather["fetched"] = Weather.isFetched();
  if (Weather.isFetched()) {
    weather["temp"] = Weather.getTemp();
    weather["hum"] = Weather.getHumidity();
    weather["desc"] = Weather.getDescription();
    weather["id"] = Weather.getWeatherId();
  }

  doc["fw"] = "v3";

  String out;
  serializeJson(doc, out);
  return out;
}

// ------------------------------------------------------------------
// HTTP 處理器：GET /  → 遙控面板頁面
// ------------------------------------------------------------------
void WebPanelMgr::handleRoot() {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "text/html; charset=utf-8", uiHTML());
}

// ------------------------------------------------------------------
// HTTP 處理器：GET /api/status → 狀態 JSON
// ------------------------------------------------------------------
void WebPanelMgr::handleStatus() {
  _server.sendHeader("Cache-Control", "no-store");
  _server.send(200, "application/json", buildStatusJSON());
}

// ------------------------------------------------------------------
// HTTP 處理器：POST /api/freq → 設定頻率
// 參數：freq = 頻率 (x10，如 8750 = 87.5 MHz)
// ------------------------------------------------------------------
void WebPanelMgr::handleSetFreq() {
  if (isScanning) {
    _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"scanning\"}");
    return;
  }
  if (!_server.hasArg("freq")) {
    _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing freq\"}");
    return;
  }
  int f = _server.arg("freq").toInt();
  if (f < 8750 || f > 10800) {
    _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"freq out of range\"}");
    return;
  }
  radioSetFrequency(f);
  _server.send(200, "application/json", "{\"ok\":true}");
}

// ------------------------------------------------------------------
// HTTP 處理器：POST /api/vol → 設定音量
// 參數：vol = 0~15
// ------------------------------------------------------------------
void WebPanelMgr::handleSetVol() {
  if (!_server.hasArg("vol")) {
    _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing vol\"}");
    return;
  }
  int v = _server.arg("vol").toInt();
  v = constrain(v, 0, 15);
  radioSetVolume(v);
  _server.send(200, "application/json", "{\"ok\":true}");
}

// ------------------------------------------------------------------
// HTTP 處理器：POST /api/cmd → 控制命令
// 參數：cmd = 命令名稱, idx = (選擇性) 頻道索引
//    scan     開始自動掃描
//    cancel   取消掃描
//    mode     切換模式 (手動 <-> 預設)
//    next     下一個預設頻道
//    prev     上一個預設頻道
//    save     儲存目前頻道
//    delete   刪除指定頻道 (需 idx)
//    deleteall 清空所有頻道
//    preset   切換到指定頻道 (需 idx)
//    fup/fdn  頻率向上 / 向下微調 0.1 MHz
//    mute     切換靜音
//    reboot   重新啟動裝置
// ------------------------------------------------------------------
void WebPanelMgr::handleCommand() {
  if (!_server.hasArg("cmd")) {
    _server.send(400, "application/json", "{\"ok\":false,\"msg\":\"missing cmd\"}");
    return;
  }

  String cmd = _server.arg("cmd");
  int idx = _server.hasArg("idx") ? _server.arg("idx").toInt() : -1;

  // 掃描進行中時僅允許取消
  if (isScanning && cmd != "cancel") {
    _server.send(200, "application/json", "{\"ok\":false,\"msg\":\"scanning\"}");
    return;
  }

  bool ok = true;
  String msg = "ok";

  if (cmd == "scan")          radioStartScan();
  else if (cmd == "cancel")   radioCancelScan();
  else if (cmd == "mode")     radioToggleMode();
  else if (cmd == "next")     radioNextPreset();
  else if (cmd == "prev")     radioPrevPreset();
  else if (cmd == "save")     radioSavePreset();
  else if (cmd == "delete") {
    if (idx >= 0 && idx < presetCount) radioDeletePreset(idx);
    else { ok = false; msg = "bad idx"; }
  }
  else if (cmd == "deleteall") radioDeleteAllPresets();
  else if (cmd == "preset") {
    if (idx >= 0 && idx < presetCount) radioSetPreset(idx);
    else { ok = false; msg = "bad idx"; }
  }
  else if (cmd == "fup")      radioStepFreq(1);
  else if (cmd == "fdn")      radioStepFreq(-1);
  else if (cmd == "mute")     radioSetMute(!radioGetMuted());
  else if (cmd == "reboot") {
    _server.send(200, "application/json", "{\"ok\":true,\"msg\":\"rebooting\"}");
    delay(300);
    ESP.restart();
    return;
  }
  else { ok = false; msg = "unknown cmd"; }

  _server.send(200, "application/json",
               String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}");
}

// ------------------------------------------------------------------
// HTTP 處理器：POST /update (OTA 上傳完成後呼叫)
// ------------------------------------------------------------------
void WebPanelMgr::handleUpdate() {
  _updating = false;
  _server.sendHeader("Connection", "close");
  _server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
  Serial.println(F("[OTA] 更新流程結束，重新啟動..."));
  delay(500);
  ESP.restart();
}

// ------------------------------------------------------------------
// HTTP 處理器：POST /update 之上傳分段接收 (OTA 韌體資料流)
// ------------------------------------------------------------------
void WebPanelMgr::handleUpdateUpload() {
  HTTPUpload& upload = _server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    _updating = true;
    Serial.printf("[OTA] 更新開始: %s\n", upload.filename.c_str());
    // UPDATE_SIZE_UNKNOWN：依目前 OTA 分割區大小自動決定
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[OTA] 更新成功，共 %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    _updating = false;
    Serial.println(F("[OTA] 更新已中止"));
  }
}

// ------------------------------------------------------------------
// HTTP 處理器：其他路徑 → 404
// ------------------------------------------------------------------
void WebPanelMgr::handleNotFound() {
  _server.send(404, "text/plain", "Not Found");
}

// ------------------------------------------------------------------
// 遙控面板頁面 HTML (含 CSS + JavaScript，單一檔案方便燒錄)
// ------------------------------------------------------------------
String WebPanelMgr::uiHTML() {
  return R"HTML(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<meta name="theme-color" content="#0f172a">
<title>FM Radio Remote v3</title>
<style>
  * { box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
  body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
         background:linear-gradient(165deg,#0f172a 0%,#1e293b 55%,#0f172a 100%);
         color:#e2e8f0; min-height:100vh; padding:16px 14px 40px; }
  h1 { margin:0; font-size:18px; color:#f8fafc; }
  .top { display:flex; align-items:center; justify-content:space-between; margin-bottom:12px; }
  .top .meta { font-size:11px; color:#94a3b8; text-align:right; }
  .card { background:#1e293b; border:1px solid #334155; border-radius:16px;
          padding:14px 14px; margin-bottom:12px; box-shadow:0 6px 20px rgba(0,0,0,.35); }
  .card h2 { margin:0 0 8px; font-size:12px; letter-spacing:1px; color:#7dd3fc; text-transform:uppercase; }

  /* 頻率顯示 */
  .freqbox { display:flex; align-items:baseline; justify-content:center; gap:6px; padding:6px 0 2px; }
  .freqbox .num { font-size:52px; font-weight:700; letter-spacing:1px; color:#f8fafc; font-variant-numeric:tabular-nums; }
  .freqbox .unit { font-size:16px; color:#94a3b8; }
  .badges { display:flex; gap:6px; justify-content:center; margin-top:8px; flex-wrap:wrap; }
  .badge { font-size:11px; font-weight:600; padding:3px 9px; border-radius:20px; background:#334155; color:#cbd5e1; }
  .badge.ok  { background:#065f46; color:#6ee7b7; }
  .badge.warn{ background:#7c2d12; color:#fdba74; }
  .badge.info{ background:#1e3a8a; color:#93c5fd; }

  /* 滑桿 */
  input[type=range] { width:100%; accent-color:#38bdf8; height:28px; }
  .volrow { display:flex; align-items:center; gap:10px; }
  .volrow .lbl { min-width:60px; font-size:14px; color:#cbd5e1; font-variant-numeric:tabular-nums; }

  /* 按鈕 */
  .btns { display:grid; grid-template-columns:repeat(4,1fr); gap:8px; }
  .btn { border:none; border-radius:12px; padding:12px 4px; font-size:13px; font-weight:600;
         background:#334155; color:#e2e8f0; cursor:pointer; transition:transform .06s, background .15s; }
  .btn:active { transform:scale(.95); }
  .btn.primary { background:#0ea5e9; color:#fff; }
  .btn.success { background:#10b981; color:#fff; }
  .btn.danger  { background:#ef4444; color:#fff; }
  .btn.ghost   { background:#1e293b; color:#94a3b8; border:1px solid #334155; }
  .btn:disabled { opacity:.4; }
  .btn.wide { grid-column:span 2; }

  /* RDS */
  .rds-st { font-size:24px; font-weight:700; color:#f8fafc; margin:2px 0 4px; min-height:29px; }
  .rds-txt { font-size:12px; color:#cbd5e1; min-height:16px; line-height:1.5; }
  .rds-meta { font-size:11px; color:#94a3b8; margin-top:6px; }
  .rds-none { font-size:12px; color:#64748b; }

  /* 預設頻道 */
  .presets { display:flex; flex-wrap:wrap; gap:8px; }
  .chip { display:flex; align-items:center; gap:6px; background:#334155; border-radius:20px;
          padding:5px 6px 5px 12px; font-size:12px; }
  .chip.cur { background:#0ea5e9; color:#fff; }
  .chip button { border:none; background:transparent; color:#94a3b8; font-size:14px; cursor:pointer;
                 width:20px; height:20px; border-radius:50%; }
  .chip.cur button { color:#082f49; }
  .chip button:active { transform:scale(.85); }

  /* OTA */
  .otarow { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
  input[type=file] { flex:1; min-width:200px; font-size:12px; color:#cbd5e1; }
  .bar { width:100%; height:10px; background:#334155; border-radius:6px; margin-top:10px; overflow:hidden; }
  .bar > div { height:100%; width:0; background:linear-gradient(90deg,#0ea5e9,#22d3ee); transition:width .15s; }
  .otainfo { font-size:11px; color:#94a3b8; margin-top:8px; }
  .msg { font-size:12px; color:#fca5a5; min-height:16px; margin-top:8px; text-align:center; }
  .msg.ok { color:#6ee7b7; }
  .grid2 { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
  .stat { font-size:12px; color:#cbd5e1; }
  .stat b { color:#f8fafc; }
  .sep { border:none; border-top:1px solid #334155; margin:10px 0; }
</style>
</head>
<body>

  <div class="top">
    <h1>&#128251; FM Radio v3</h1>
    <div class="meta" id="netinfo">...</div>
  </div>

  <!-- 頻率 + 狀態 -->
  <div class="card">
    <div class="freqbox">
      <span class="num" id="freq">87.5</span><span class="unit">MHz</span>
    </div>
    <div class="badges">
      <span class="badge" id="mode">MANUAL</span>
      <span class="badge info" id="st">MONO</span>
      <span class="badge" id="rssi">RSSI --</span>
    </div>
    <hr class="sep">
    <input type="range" id="freqSlider" min="8750" max="10800" step="10" value="8750">
    <div class="btns" style="margin-top:6px;">
      <button class="btn" onclick="cmd('fdn')">&#9664; -0.1</button>
      <button class="btn" onclick="cmd('fup')">+0.1 &#9654;</button>
      <button class="btn primary wide" id="scanBtn" onclick="toggleScan()">&#128269; 自動掃描</button>
    </div>
  </div>

  <!-- 音量 -->
  <div class="card">
    <h2>音量 Volume</h2>
    <div class="volrow">
      <span class="lbl" id="volVal">0</span>
      <input type="range" id="volSlider" min="0" max="15" value="0">
    </div>
    <div class="btns" style="margin-top:8px;">
      <button class="btn" onclick="volStep(-1)">&#128263; -</button>
      <button class="btn" onclick="volStep(1)">+ &#128266;</button>
      <button class="btn danger" onclick="cmd('mute')">&#128264; 靜音</button>
      <button class="btn success" onclick="cmd('save')">&#128190; 儲存</button>
    </div>
  </div>

  <!-- RDS 數位廣播資訊 -->
  <div class="card">
    <h2>RDS 廣播資訊</h2>
    <div class="rds-st" id="rdsStation">--</div>
    <div class="rds-txt" id="rdsText"></div>
    <div class="rds-meta" id="rdsMeta"></div>
  </div>

  <!-- 預設頻道 -->
  <div class="card">
    <h2>預設頻道 Presets</h2>
    <div class="presets" id="presets">(無)</div>
    <div class="btns" style="margin-top:10px;">
      <button class="btn" onclick="cmd('prev')">&#9664; 上一個</button>
      <button class="btn" onclick="cmd('next')">下一個 &#9654;</button>
      <button class="btn" onclick="cmd('mode')">切換模式</button>
      <button class="btn danger" onclick="if(confirm('確定清空所有頻道？'))cmd('deleteall')">&#128465; 清空</button>
    </div>
  </div>

  <!-- 系統資訊 -->
  <div class="card">
    <h2>系統資訊</h2>
    <div class="grid2">
      <div class="stat">時鐘 <b id="clock">--</b></div>
      <div class="stat">WiFi <b id="wifissid">--</b></div>
      <div class="stat">IP <b id="ip">--</b></div>
      <div class="stat">訊號 <b id="wifirssi">--</b></div>
      <div class="stat">天氣 <b id="weather">--</b></div>
    </div>
  </div>

  <!-- OTA 無線韌體更新 -->
  <div class="card">
    <h2>OTA 韌體更新</h2>
    <div class="otarow">
      <input type="file" id="fwFile" accept=".bin">
      <button class="btn primary" onclick="doOTA()">上傳更新</button>
    </div>
    <div class="bar"><div id="otaBar"></div></div>
    <div class="otainfo">請選擇由 <b>pio run</b> 產生的 <b>firmware.bin</b>（v3 目錄 .pio/build/nodemcu-32s/）。上傳完成後裝置會自動重啟。</div>
    <div class="msg" id="otaMsg"></div>
  </div>

<script>
var SCAN = false;   // 掃描進行中旗標 (由 status 更新)

// ---- 通用工具 ----
function $(id){ return document.getElementById(id); }
function setMsg(el, t, ok){ var m=$(el); m.innerHTML=t; m.className='msg'+(ok?' ok':''); }

// 送出控制命令
function cmd(name, idx){
  var fd = new FormData();
  fd.append('cmd', name);
  if (idx !== undefined) fd.append('idx', idx);
  return fetch('/api/cmd', {method:'POST', body:fd}).then(function(r){ return r.json(); });
}

// ---- 狀態輪詢 ----
function refresh(){
  fetch('/api/status', {cache:'no-store'})
    .then(function(r){ return r.json(); })
    .then(function(s){
      SCAN = s.scanning;
      render(s);
    })
    .catch(function(){});
}

function render(s){
  // 頻率 / 模式 / 立體聲 / RSSI
  $('freq').textContent = (s.freq/100).toFixed(1);
  $('freqSlider').value = s.freq;
  $('mode').textContent = (s.mode==='manual'?'MANUAL':(s.mode==='preset'?'PRESET':'MENU'));
  $('st').textContent = s.scanning ? 'SCANNING' : (s.stereo ? 'STEREO' : 'MONO');
  $('st').className = 'badge ' + (s.stereo?'info':'');
  $('rssi').textContent = 'RSSI ' + s.rssi;

  // 掃描按鈕
  var sb = $('scanBtn');
  sb.innerHTML = s.scanning ? '&#10074;&#10074; 停止掃描' : '&#128269; 自動掃描';
  sb.className = 'btn ' + (s.scanning ? 'danger wide' : 'primary wide');

  // 音量
  $('volSlider').value = s.vol;
  $('volVal').textContent = s.muted ? (s.vol + ' (MUTE)') : String(s.vol);

  // RDS
  var rd = s.rds;
  if (rd && rd.synced && rd.station) {
    $('rdsStation').textContent = rd.station;
    $('rdsText').textContent = rd.text || '';
    $('rdsMeta').textContent =
      'PTY: ' + rd.ptyName + ' (' + rd.pty + ')' +
      (rd.tp ? ' | TP' : '') + (rd.ta ? ' | TA' : '') +
      (rd.time ? ' | CT ' + rd.time : '');
  } else {
    $('rdsStation').textContent = '--';
    $('rdsText').innerHTML = '<span class="rds-none">目前電台未提供 RDS 資訊</span>';
    $('rdsMeta').textContent = '';
  }

  // 預設頻道
  renderPresets(s);

  // 系統資訊
  $('clock').textContent = s.clock || '--';
  $('wifissid').textContent = s.wifi.ssid || '--';
  $('ip').textContent = s.wifi.ip || '--';
  $('wifirssi').textContent = (s.wifi.rssi !== undefined ? s.wifi.rssi + ' dBm' : '--');
  $('netinfo').innerHTML = (s.wifi.ip || '') + '<br>' + (s.clock || '');
  if (s.weather && s.weather.fetched) {
    $('weather').innerHTML = s.weather.temp + '&#176;C ' + s.weather.hum + '% ' + s.weather.desc;
  } else {
    $('weather').textContent = '--';
  }
}

function renderPresets(s){
  var box = $('presets');
  if (!s.count) { box.innerHTML = '<span class="rds-none">(目前無預設頻道)</span>'; return; }
  var html = '';
  for (var i=0; i<s.presets.length; i++){
    var cur = (i === s.idx && s.mode === 'preset') ? ' cur' : '';
    html += '<span class="chip'+cur+'" onclick="cmd(\'preset\','+i+')">' +
            (s.presets[i]/100).toFixed(1) +
            '<button onclick="event.stopPropagation();delChip('+i+')">&#10005;</button></span>';
  }
  box.innerHTML = html;
}

function delChip(i){ cmd('delete', i).then(function(){ refresh(); }); }

// ---- 操作 ----
function cmdAndRefresh(name, idx){ cmd(name, idx).then(function(){ refresh(); }); }

function toggleScan(){
  if (SCAN) cmd('cancel'); else cmd('scan');
  setTimeout(refresh, 300);
}

function volStep(d){
  var v = parseInt($('volSlider').value, 10) + d;
  v = Math.max(0, Math.min(15, v));
  var fd = new FormData(); fd.append('vol', v);
  fetch('/api/vol', {method:'POST', body:fd}).then(function(){ refresh(); });
}

// 頻率滑桿：拖動時即時更新標籤，放開才送出
$('freqSlider').addEventListener('input', function(){
  $('freq').textContent = (parseInt(this.value,10)/100).toFixed(1);
});
$('freqSlider').addEventListener('change', function(){
  var fd = new FormData(); fd.append('freq', this.value);
  fetch('/api/freq', {method:'POST', body:fd}).then(function(){ refresh(); });
});

// ---- OTA ----
function doOTA(){
  var f = $('fwFile').files[0];
  if (!f) { setMsg('otaMsg','請先選擇 firmware.bin 檔'); return; }
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function(e){
    if (e.lengthComputable){
      var p = Math.round(e.loaded/e.total*100);
      $('otaBar').style.width = p + '%';
      setMsg('otaMsg','上傳中... ' + p + '%', true);
    }
  };
  xhr.onload = function(){
    if (xhr.status === 200){
      setMsg('otaMsg','上傳完成！裝置重新啟動中，約 30 秒後可重新連線...', true);
    } else {
      setMsg('otaMsg','更新失敗 (HTTP ' + xhr.status + ')');
    }
  };
  xhr.onerror = function(){ setMsg('otaMsg','上傳失敗，請檢查網路連線'); };
  xhr.send(f);
}

// 每秒輪詢一次
refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)HTML";
}
