#include "ConfigPortal.h"  // 引入對應的標頭檔

#include <DNSServer.h>     // DNS 伺服器函式庫：用於 Captive Portal 的 DNS 攔截
#include <WebServer.h>     // ESP32 輕量 HTTP Web 伺服器函式庫
#include <WiFi.h>          // ESP32 WiFi 函式庫

#include "ConfigStore.h"   // 憑證儲存模組：用於儲存/讀取 WiFi 帳密

// 全域單例實體
ConfigPortal Portal;

// ---- 內部靜態變數（僅此檔案內使用）----
static WebServer*    _server  = nullptr;  // HTTP Web 伺服器指標
static DNSServer*    _dns     = nullptr;  // DNS 伺服器指標（攔截所有網域）
static const byte    DNS_PORT = 53;       // DNS 服務埠（標準 DNS 埠號）

// 暫存使用者從網頁送出的待驗證憑證
static String _trySSID = "";    // 使用者輸入的 WiFi SSID
static String _tryPass = "";    // 使用者輸入的 WiFi 密碼

// ------------------------------------------------------------
// 公開介面：啟動配網入口
// ------------------------------------------------------------
// Captive Portal 流程（純 AP 架構）：
//   1. 純 AP 模式開熱點 + DNS 攔截 + WebServer，等待使用者送出 SSID/密碼
//   2. 收到憑證 → 關閉 AP/DNS/Web → 切純 STA → 驗證連線（同步阻塞）
//   3. 連線成功 → 存憑證 → ESP.restart()
//   4. 連線失敗 → ESP.restart()（重啟後回到步驟 1 重新配網）
// 注意：本函式內部一定會呼叫 ESP.restart()，不會正常 return。
bool ConfigPortal::start(const String& apSSID, const String& apPassword, unsigned long timeoutMs) {
  // 動態建立 WebServer（埠 80）與 DNSServer 物件
  _server = new WebServer(80);
  _dns    = new DNSServer();

  // 步驟 1：以純 AP 模式啟動 ESP32 的 SoftAP
  WiFi.mode(WIFI_AP);  // 設定為純 AP 模式（不啟動 STA，避免 AP+STA 並存 panic）
  bool apOK;
  // ESP32 SoftAP 密碼長度至少需 8 字元，不足則設為開放熱點
  if (apPassword.length() >= 8) {
    apOK = WiFi.softAP(apSSID.c_str(), apPassword.c_str());  // 有密碼的 AP
  } else {
    apOK = WiFi.softAP(apSSID.c_str());                      // 開放 AP（無密碼）
  }
  Serial.print(F("[Portal] softAP "));
  Serial.println(apOK ? F("OK") : F("FAIL"));
  Serial.print(F("[Portal] AP IP: "));
  Serial.println(WiFi.softAPIP());  // AP 的 IP 位址（預設為 192.168.4.1）

  // 步驟 2：啟動 DNS 攔截
  // 將所有網域名稱（*）都解析到 ESP32 的 AP IP
  // 這是 Captive Portal 的核心機制：使用者連上 AP 後，無論瀏覽器輸入什麼網址，
  // DNS 查詢都會被攔截並回應 ESP32 的 IP，進而連到設定頁面
  _dns->start(DNS_PORT, "*", WiFi.softAPIP());

  // 步驟 3：註冊 HTTP Web 伺服器的路由處理函式
  _server->on("/",        HTTP_GET,  [this]() { handleRoot(); });     // 首頁：配網表單
  _server->on("/scan",    HTTP_GET,  [this]() { handleScan(); });     // 掃描 WiFi 清單
  _server->on("/connect", HTTP_POST, [this]() { handleConnect(); });  // 送出憑證
  _server->on("/reset",   HTTP_POST, [this]() { handleReset(); });    // 清除已存設定
  _server->onNotFound([this]() { handleNotFound(); });                 // 404 攔截
  _server->begin();  // 啟動 HTTP 伺服器
  Serial.println(F("[Portal] HTTP 伺服器已啟動 (port 80)"));

  // 輸出 AP 資訊到序列埠，方便使用者連線
  Serial.println(F("[Portal] 配網入口已啟動，請以手機連上此 AP"));
  Serial.print(F("[Portal] AP SSID: "));
  Serial.println(apSSID);
  if (apPassword.length()) {
    Serial.print(F("[Portal] AP PASS: "));
    Serial.println(apPassword);
  }
  Serial.println(F("[Portal] 開啟瀏覽器造訪 http://192.168.4.1"));

  // 步驟 4：配網主迴圈
  // 只跑 AP + DNS + WebServer，不做任何 STA 操作（最穩定）
  unsigned long t0 = millis();
  while (true) {
    _dns->processNextRequest();   // 處理 DNS 查詢請求
    _server->handleClient();       // 處理 HTTP 客戶端請求

    // 逾時檢查：若設定了逾時時間且已超過，則跳出迴圈
    if (timeoutMs > 0 && (millis() - t0) > timeoutMs) {
      Serial.println(F("[Portal] 配網逾時，重啟..."));
      break;
    }
    delay(2);  // 短暫延遲，避免 CPU 滿載
  }

  // 逾時出口：關閉所有服務後重啟
  _dns->stop();           // 停止 DNS 伺服器
  _server->stop();        // 停止 HTTP 伺服器
  delete _dns;            // 釋放 DNSServer 物件記憶體
  delete _server;         // 釋放 WebServer 物件記憶體
  _dns = nullptr;         // 指標歸零，避免懸空指標
  _server = nullptr;
  delay(100);
  ESP.restart();          // 重新啟動 ESP32
  return false;           // 不會執行到這裡（僅供編譯器通過）
}

// ------------------------------------------------------------
// 連線驗證：關閉 AP、切換純 STA、嘗試連線、成功則儲存憑證
// 此函式在 AP 完全關閉後才進行 STA 連線，避免並存問題
// ------------------------------------------------------------
bool ConfigPortal::verifyAndSave() {
  Serial.println(F("[Portal] 關閉 AP，切換為純 STA 進行連線驗證..."));

  // 關閉 AP 端的 DNS 與 HTTP 服務
  _dns->stop();
  _server->stop();
  delete _dns;
  delete _server;
  _dns = nullptr;
  _server = nullptr;

  // 切換為純 STA 模式並嘗試連線
  // 此時已無 AP 在運作，不會有 AP+STA 並存的 panic 問題
  WiFi.mode(WIFI_STA);
  delay(100);                                     // 等待模式切換完成
  WiFi.begin(_trySSID.c_str(), _tryPass.c_str()); // 使用使用者提供的憑證連線

  Serial.print(F("[Portal] 連線中: "));
  Serial.println(_trySSID);

  // 同步等待連線結果，最多等 15 秒
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > 15000) {
      Serial.println(F("[Portal] 連線逾時（密碼錯誤或訊號太弱）"));
      return false;  // 連線失敗
    }
    delay(200);
    Serial.print(".");
  }

  // 連線成功
  Serial.println();
  Serial.print(F("[Portal] 連線成功！IP: "));
  Serial.println(WiFi.localIP());

  // 將通過驗證的憑證存入 NVS
  Config.saveCredentials(_trySSID, _tryPass);
  Serial.println(F("[Portal] 憑證已儲存"));
  return true;
}

// ------------------------------------------------------------
// 請求處理函式
// ------------------------------------------------------------

// 首頁 "/"：回傳配網設定頁面的 HTML
void ConfigPortal::handleRoot() {
  _server->sendHeader("Cache-Control", "no-store");                     // 禁止瀏覽器快取
  _server->send(200, "text/html; charset=utf-8", portalHTML());         // 回傳 HTML
}

// WiFi 掃描 "/scan"：掃描周圍的 WiFi 網路並回傳 JSON 清單
void ConfigPortal::handleScan() {
  // 在純 AP 模式下掃描：ESP32 會短暫切到 STA 做掃描再回到 AP
  int n = WiFi.scanNetworks(false, true);  // 同步掃描，包含隱藏網路
  String json = "[";                       // 開始 JSON 陣列
  for (int i = 0; i < n; ++i) {
    if (i) json += ",";                    // 非第一筆時加入逗號分隔
    json += "{\"ssid\":\"" + escapeHTML(WiFi.SSID(i)) + "\",";       // SSID
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";                // 訊號強度
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}"; // 是否加密
  }
  json += "]";                             // 結束 JSON 陣列
  WiFi.scanDelete();                       // 釋放掃描結果佔用的記憶體
  _server->sendHeader("Cache-Control", "no-store");
  _server->send(200, "application/json", json);  // 以 JSON 格式回傳
}

// 接收 WiFi 憑證 "/connect"：驗證連線並重啟
void ConfigPortal::handleConnect() {
  // 檢查請求中是否包含 ssid 參數
  if (!_server->hasArg("ssid")) {
    _server->send(400, "application/json", "{\"ok\":false,\"msg\":\"缺少 ssid\"}");
    return;
  }
  // 讀取使用者送出的 SSID 與密碼
  _trySSID = _server->arg("ssid");
  _tryPass = _server->hasArg("password") ? _server->arg("password") : "";

  Serial.print(F("[Portal] 收到配網請求: "));
  Serial.println(_trySSID);

  // 先回傳成功回應給前端，讓瀏覽器顯示「連線中...」
  _server->send(200, "application/json", "{\"ok\":true}");

  // 給瀏覽器一點時間收到並處理回應
  delay(300);

  // 執行連線驗證（此函式會關閉 AP，之後不再回到配網主迴圈）
  bool ok = verifyAndSave();

  // 根據驗證結果決定後續動作（皆會重啟）
  if (ok) {
    Serial.println(F("[Portal] 配網完成，1 秒後重啟套用設定..."));
    delay(1000);
  } else {
    Serial.println(F("[Portal] 配網失敗，1 秒後重啟回到配網模式..."));
    delay(1000);
  }
  // 重新啟動（重啟後若憑證有效則自動連線，無效則回到配網模式）
  ESP.restart();
}

// 清除已存 WiFi 設定 "/reset"
void ConfigPortal::handleReset() {
  Config.clearCredentials();  // 清除 NVS 中的憑證
  Serial.println(F("[Portal] 已清除儲存的憑證"));
  _server->send(200, "application/json", "{\"ok\":true}");
}

// 處理所有未定義路徑：Captive Portal 關鍵機制
// 將使用者嘗試連線的任何網址都導向 ESP32 的設定頁面
void ConfigPortal::handleNotFound() {
  _server->sendHeader("Location", "http://192.168.4.1/");  // HTTP 重新導向
  _server->send(302, "text/plain", "");                     // 302 Found
}

// ------------------------------------------------------------
// HTML 跳脫函式：將特殊字元轉換為 HTML 安全形式
// ------------------------------------------------------------
String ConfigPortal::escapeHTML(const String& s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '"':  r += "\\\""; break;  // 雙引號
      case '\\': r += "\\\\"; break;  // 反斜線
      case '\n': r += " ";    break;  // 換行轉空白
      case '\r': r += " ";    break;  // 回車轉空白
      default:   r += c;              // 一般字元
    }
  }
  return r;
}

// ------------------------------------------------------------
// 配網頁面 HTML（內含 CSS + JavaScript，單一檔案方便燒錄）
// 採用響應式設計，在手機上也有良好的操作體驗
// ------------------------------------------------------------
String ConfigPortal::portalHTML() {
  // R"HTML(...)HTML" 是 C++11 的原始字串字面值（Raw String Literal），
  // 可包含換行與引號不需跳脫，方便嵌入大量 HTML 程式碼
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 WiFi 配網</title>
<style>
  /* ---- CSS reset 與視覺設計 ---- */
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
         background:linear-gradient(160deg,#4facfe 0%,#00f2fe 100%); min-height:100vh;
         display:flex; align-items:center; justify-content:center; padding:18px; }
  .card { background:#fff; border-radius:18px; padding:24px 20px; width:100%; max-width:380px;
          box-shadow:0 12px 40px rgba(0,0,0,.25); }
  h1 { margin:0 0 4px; font-size:20px; color:#1a2a44; text-align:center; }
  .sub { color:#8794a8; font-size:12px; text-align:center; margin:0 0 18px; }
  label { display:block; font-size:13px; color:#5a6a82; margin:10px 0 4px; }
  input, select { width:100%; padding:11px 12px; border:1.5px solid #d8e0ec; border-radius:10px;
          font-size:15px; outline:none; transition:border-color .15s; }
  input:focus, select:focus { border-color:#4facfe; }
  .row { display:flex; gap:8px; align-items:center; }
  .row select { flex:1; }
  .btn { display:block; width:100%; padding:13px; border:none; border-radius:10px; font-size:15px;
         font-weight:600; color:#fff; background:linear-gradient(90deg,#4facfe,#00c6fb);
         cursor:pointer; margin-top:18px; transition:transform .05s,opacity .2s; }
  .btn:active { transform:scale(.98); }
  .btn:disabled { opacity:.55; }
  .btn.sec { background:#eef2f8; color:#5a6a82; margin-top:10px; }
  .refresh { font-size:12px; color:#4facfe; background:none; border:none; padding:6px 8px; cursor:pointer; }
  #msg { margin-top:14px; font-size:13px; text-align:center; min-height:18px; color:#e74c3c; }
  .ok { color:#27ae60 !important; }
  .spin { display:inline-block; width:14px; height:14px; border:2px solid #bbb; border-top-color:#4facfe;
          border-radius:50%; animation:sp .8s linear infinite; vertical-align:-2px; margin-right:6px; }
  @keyframes sp { to { transform:rotate(360deg); } }
</style>
</head>
<body>
  <div class="card">
    <h1>WiFi 配網設定</h1>
    <p class="sub">請選擇您的 WiFi 網路並輸入密碼</p>

    <!-- WiFi 清單下拉選單 -->
    <label>可用網路</label>
    <div class="row">
      <select id="ssid" disabled><option value="">載入中...</option></select>
      <button class="refresh" onclick="loadList()">&#8635;</button>
    </div>

    <!-- 手動輸入 SSID -->
    <label>或手動輸入 SSID</label>
    <input id="ssidManual" type="text" placeholder="（選擇上方清單或自行輸入）">

    <!-- 密碼輸入 -->
    <label>密碼</label>
    <input id="pass" type="password" placeholder="WiFi 密碼">

    <!-- 操作按鈕 -->
    <button class="btn" id="connBtn" onclick="doConnect()">連線</button>
    <button class="btn sec" onclick="doReset()">清除已存設定</button>
    <div id="msg"></div>
  </div>

<script>
// ---- JavaScript：處理 WiFi 掃描、連線、重置 ----
var sent = false;  // 防止重複送出

// 設定訊息文字，ok=true 時顯示綠色
function setMsg(t, ok){ var e=document.getElementById('msg'); e.innerHTML=t; e.className=ok?'ok':''; }

// 切換連線按鈕的忙碌狀態
function setBusy(b){ var btn=document.getElementById('connBtn'); btn.disabled=b; btn.textContent=b?'連線中...':'連線'; }

// 向 ESP32 請求掃描周遭 WiFi 網路並更新下拉選單
function loadList(){
  var sel=document.getElementById('ssid');
  sel.disabled=true; sel.innerHTML='<option value="">掃描中...</option>';
  fetch('/scan',{cache:'no-store'}).then(r=>r.json()).then(function(list){
    if(!list.length){ sel.innerHTML='<option value="">（找不到網路）</option>'; return; }
    list.sort(function(a,b){return b.rssi-a.rssi;});  // 依訊號強度降冪排列
    var opt='<option value="">-- 請選擇 --</option>';
    for(var i=0;i<list.length;i++){
      var lock=list[i].secure?' &#128274;':'';  // 加密網路顯示鎖頭圖示
      opt+='<option value="'+list[i].ssid+'">'+list[i].ssid+' ('+list[i].rssi+'dBm)'+lock+'</option>';
    }
    sel.innerHTML=opt; sel.disabled=false;
  }).catch(function(){ sel.innerHTML='<option value="">掃描失敗</option>'; });
}

// 送出連線請求
function doConnect(){
  if(sent) return;
  var ssid=document.getElementById('ssid').value || document.getElementById('ssidManual').value.trim();
  var pass=document.getElementById('pass').value;
  if(!ssid){ setMsg('請選擇或輸入 SSID'); return; }
  sent=true; setBusy(true);
  // 送出連線請求後，ESP32 會關閉 AP 進行驗證，然後重啟
  // 因此此頁面會失去連線，只顯示等待訊息
  setMsg('<span class=spin></span>ESP32 正在連線，請等待約 15~20 秒...<br>連線成功後 ESP32 會自動重啟並連上 WiFi');
  var fd=new FormData(); fd.append('ssid',ssid); fd.append('password',pass);
  fetch('/connect',{method:'POST',body:fd}).then(r=>r.json()).then(function(j){
    if(!j.ok){ sent=false; setBusy(false); setMsg(j.msg||'送出失敗'); return; }
    // 成功送出後等待 ESP32 重啟
  }).catch(function(){
    // fetch 失敗屬正常（ESP32 正在關閉 AP / 重啟），忽略錯誤即可
  });
}

// 清除已存設定
function doReset(){
  if(!confirm('確定要清除已儲存的 WiFi 設定嗎？')) return;
  fetch('/reset',{method:'POST'}).then(r=>r.json()).then(function(){
    setMsg('已清除設定，ESP32 將重啟回到配網模式', true);
  });
}

// 頁面載入後自動掃描 WiFi 清單
window.addEventListener('load', loadList);
</script>
</body>
</html>
)HTML";
  return html;  // 回傳完整的 HTML 頁面
}
