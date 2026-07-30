#include "ConfigPortal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ConfigStore.h"

// 全域單例實體
ConfigPortal Portal;

// ---- 內部使用 ----
static WebServer*    _server  = nullptr;
static DNSServer*    _dns     = nullptr;
static const byte    DNS_PORT = 53;

// 使用者送出的待驗證憑證
static String _trySSID = "";
static String _tryPass = "";

// ------------------------------------------------------------
// 公開介面
// ------------------------------------------------------------
// 配網流程（純 AP 架構，避免 AP+STA 並存導致 panic）：
//   1. 純 AP 模式開熱點 + DNS 攔截 + WebServer，等使用者送出 SSID/密碼
//   2. 收到憑證 → 關閉 AP/DNS/Web → 切純 STA → 驗證連線（同步阻塞）
//   3. 連線成功 → 存憑證 → ESP.restart()
//   4. 連線失敗 → ESP.restart()（重啟後因無有效憑證，回到步驟 1）
// 注意：本函式內部一定會 ESP.restart()，不會正常 return。
bool ConfigPortal::start(const String& apSSID, const String& apPassword, unsigned long timeoutMs) {
  _server = new WebServer(80);
  _dns    = new DNSServer();

  // 1) 純 AP 模式啟動
  WiFi.mode(WIFI_AP);
  bool apOK;
  if (apPassword.length() >= 8) {
    apOK = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
  } else {
    apOK = WiFi.softAP(apSSID.c_str());
  }
  Serial.print(F("[Portal] softAP "));
  Serial.println(apOK ? F("OK") : F("FAIL"));
  Serial.print(F("[Portal] AP IP: "));
  Serial.println(WiFi.softAPIP());

  // 2) DNS 攔截：所有網域都解析到 ESP32（Captive Portal 關鍵）
  _dns->start(DNS_PORT, "*", WiFi.softAPIP());

  // 3) 註冊網頁路由
  _server->on("/",        HTTP_GET,  [this]() { handleRoot(); });
  _server->on("/scan",    HTTP_GET,  [this]() { handleScan(); });
  _server->on("/connect", HTTP_POST, [this]() { handleConnect(); });
  _server->on("/reset",   HTTP_POST, [this]() { handleReset(); });
  _server->onNotFound([this]() { handleNotFound(); });
  _server->begin();
  Serial.println(F("[Portal] HTTP 伺服器已啟動 (port 80)"));

  Serial.println(F("[Portal] 配網入口已啟動，請以手機連上此 AP"));
  Serial.print(F("[Portal] AP SSID: "));
  Serial.println(apSSID);
  if (apPassword.length()) {
    Serial.print(F("[Portal] AP PASS: "));
    Serial.println(apPassword);
  }
  Serial.println(F("[Portal] 開啟瀏覽器造訪 http://192.168.4.1"));

  // 4) 配網主迴圈：只跑 AP+DNS+Web，不做任何 STA 操作（最穩定）
  unsigned long t0 = millis();
  while (true) {
    _dns->processNextRequest();
    _server->handleClient();

    // 逾時檢查
    if (timeoutMs > 0 && (millis() - t0) > timeoutMs) {
      Serial.println(F("[Portal] 配網逾時，重啟..."));
      break;
    }
    delay(2);
  }

  // 逾時出口：關閉服務後重啟
  _dns->stop();
  _server->stop();
  delete _dns;
  delete _server;
  _dns = nullptr;
  _server = nullptr;
  delay(100);
  ESP.restart();
  return false;  // 不會到這
}

// ------------------------------------------------------------
// 連線驗證（純 STA，獨立函式，在 AP 關閉後才呼叫）
// ------------------------------------------------------------
bool ConfigPortal::verifyAndSave() {
  Serial.println(F("[Portal] 關閉 AP，切換為純 STA 進行連線驗證..."));

  // 關閉 AP 端服務
  _dns->stop();
  _server->stop();
  delete _dns;
  delete _server;
  _dns = nullptr;
  _server = nullptr;

  // 切純 STA 並嘗試連線（此時已無 AP，不會有並存 panic 問題）
  WiFi.mode(WIFI_STA);
  delay(100);
  WiFi.begin(_trySSID.c_str(), _tryPass.c_str());

  Serial.print(F("[Portal] 連線中: "));
  Serial.println(_trySSID);

  // 同步等待連線，最多 15 秒
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > 15000) {
      Serial.println(F("[Portal] 連線逾時（密碼錯誤或訊號太弱）"));
      return false;
    }
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.print(F("[Portal] 連線成功！IP: "));
  Serial.println(WiFi.localIP());

  // 連線成功 → 儲存憑證
  Config.saveCredentials(_trySSID, _tryPass);
  Serial.println(F("[Portal] 憑證已儲存"));
  return true;
}

// ------------------------------------------------------------
// 請求處理
// ------------------------------------------------------------
void ConfigPortal::handleRoot() {
  _server->sendHeader("Cache-Control", "no-store");
  _server->send(200, "text/html; charset=utf-8", portalHTML());
}

void ConfigPortal::handleScan() {
  // 純 AP 模式下掃描：ESP32 會短暫切到 STA 做掃描再回來
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + escapeHTML(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  _server->sendHeader("Cache-Control", "no-store");
  _server->send(200, "application/json", json);
}

void ConfigPortal::handleConnect() {
  // 接收 SSID/密碼
  if (!_server->hasArg("ssid")) {
    _server->send(400, "application/json", "{\"ok\":false,\"msg\":\"缺少 ssid\"}");
    return;
  }
  _trySSID = _server->arg("ssid");
  _tryPass = _server->hasArg("password") ? _server->arg("password") : "";

  Serial.print(F("[Portal] 收到配網請求: "));
  Serial.println(_trySSID);

  // 先回應前端，讓它顯示「連線中」
  _server->send(200, "application/json", "{\"ok\":true}");

  // 給前端一點時間收到回應
  delay(300);

  // 進入連線驗證（會關閉 AP，之後不再回到這個迴圈）
  bool ok = verifyAndSave();

  if (ok) {
    Serial.println(F("[Portal] 配網完成，1 秒後重啟套用設定..."));
    delay(1000);
  } else {
    Serial.println(F("[Portal] 配網失敗，1 秒後重啟回到配網模式..."));
    delay(1000);
  }
  ESP.restart();
  // 不會到這
}

void ConfigPortal::handleReset() {
  Config.clearCredentials();
  Serial.println(F("[Portal] 已清除儲存的憑證"));
  _server->send(200, "application/json", "{\"ok\":true}");
}

void ConfigPortal::handleNotFound() {
  // Captive Portal 關鍵：把所有未知路徑導回首頁
  _server->sendHeader("Location", "http://192.168.4.1/");
  _server->send(302, "text/plain", "");
}

// ------------------------------------------------------------
// 工具函式
// ------------------------------------------------------------
String ConfigPortal::escapeHTML(const String& s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '"':  r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += " ";    break;
      case '\r': r += " ";    break;
      default:   r += c;
    }
  }
  return r;
}

// 登錄頁面 HTML（內含 CSS + JS，單一檔案方便燒錄）
String ConfigPortal::portalHTML() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 WiFi 配網</title>
<style>
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

    <label>可用網路</label>
    <div class="row">
      <select id="ssid" disabled><option value="">載入中...</option></select>
      <button class="refresh" onclick="loadList()">&#8635;</button>
    </div>

    <label>或手動輸入 SSID</label>
    <input id="ssidManual" type="text" placeholder="（選擇上方清單或自行輸入）">

    <label>密碼</label>
    <input id="pass" type="password" placeholder="WiFi 密碼">

    <button class="btn" id="connBtn" onclick="doConnect()">連線</button>
    <button class="btn sec" onclick="doReset()">清除已存設定</button>
    <div id="msg"></div>
  </div>

<script>
var sent = false;
function setMsg(t, ok){ var e=document.getElementById('msg'); e.innerHTML=t; e.className= ok?'ok':''; }
function setBusy(b){ var btn=document.getElementById('connBtn'); btn.disabled=b; btn.textContent=b?'連線中...':'連線'; }

function loadList(){
  var sel=document.getElementById('ssid');
  sel.disabled=true; sel.innerHTML='<option value="">掃描中...</option>';
  fetch('/scan',{cache:'no-store'}).then(r=>r.json()).then(function(list){
    if(!list.length){ sel.innerHTML='<option value="">（找不到網路）</option>'; return; }
    list.sort(function(a,b){return b.rssi-a.rssi;});
    var opt='<option value="">-- 請選擇 --</option>';
    for(var i=0;i<list.length;i++){
      var lock=list[i].secure?' &#128274;':'';
      opt+='<option value="'+list[i].ssid+'">'+list[i].ssid+' ('+list[i].rssi+'dBm)'+lock+'</option>';
    }
    sel.innerHTML=opt; sel.disabled=false;
  }).catch(function(){ sel.innerHTML='<option value="">掃描失敗</option>'; });
}

function doConnect(){
  if(sent) return;
  var ssid=document.getElementById('ssid').value || document.getElementById('ssidManual').value.trim();
  var pass=document.getElementById('pass').value;
  if(!ssid){ setMsg('請選擇或輸入 SSID'); return; }
  sent=true; setBusy(true);
  // 連線後 ESP32 會關閉 AP 進行驗證，然後重啟。
  // 期間此頁面會失去連線，所以送出後只顯示等待訊息，不做輪詢。
  setMsg('<span class=spin></span>ESP32 正在連線，請等待約 15~20 秒...<br>連線成功後 ESP32 會自動重啟並連上 WiFi');
  var fd=new FormData(); fd.append('ssid',ssid); fd.append('password',pass);
  fetch('/connect',{method:'POST',body:fd}).then(r=>r.json()).then(function(j){
    if(!j.ok){ sent=false; setBusy(false); setMsg(j.msg||'送出失敗'); return; }
    // 送出成功，等 ESP32 重啟。手機可改連回原本 WiFi 查看 ESP32 是否上線。
  }).catch(function(){
    // fetch 失敗屬正常（ESP32 正在關閉 AP / 重啟），顯示等待訊息即可
  });
}

function doReset(){
  if(!confirm('確定要清除已儲存的 WiFi 設定嗎？')) return;
  fetch('/reset',{method:'POST'}).then(r=>r.json()).then(function(){
    setMsg('已清除設定，ESP32 將重啟回到配網模式', true);
  });
}

window.addEventListener('load', loadList);
</script>
</body>
</html>
)HTML";
  return html;
}
