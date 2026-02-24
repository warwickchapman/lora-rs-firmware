#include "web_console.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Updater.h>
#include <cstring>

#include "build_info.h"
#include "config_store.h"
#include "logger.h"
#include "log_buffer.h"
#include "sensor_manager.h"
#include "state_machine.h"

#ifndef LRS_ENABLE_MDNS
#define LRS_ENABLE_MDNS 1
#endif

namespace {
constexpr uint32_t kMinHeartbeatMs = 60000;
constexpr uint32_t kMaxHeartbeatMs = 3600000;
constexpr uint32_t kMinAckTimeoutMs = 5 * 1000;
constexpr uint32_t kMaxAckTimeoutMs = 600 * 1000;
constexpr uint32_t kMinMqttRemoteRetryTimeoutMs = 5 * 1000;
constexpr uint32_t kMaxMqttRemoteRetryTimeoutMs = 3600 * 1000;
constexpr uint32_t kMinTxPollDefaultIntervalMs = 60 * 1000;
constexpr uint32_t kMaxTxPollDefaultIntervalMs = 3600 * 1000;
constexpr uint32_t kMinRxPushIntervalMs = 60 * 1000;
constexpr uint32_t kMaxRxPushIntervalMs = 3600 * 1000;
constexpr const char *kDefaultDeploymentKey = "lora-default-passphrase";
constexpr size_t kMinDeploymentKeyLen = 16;
constexpr const char *kHardwareVersion = "v1.2";
constexpr const char *kHardwareBatch = "251101";
constexpr uint32_t kLowHeapWarnThresholdBytes = 14000;
constexpr uint32_t kLowHeapWarnMinIntervalMs = 5000;
constexpr uint32_t kApiLightLowHeapRejectFreeBytes = 3000;
constexpr uint32_t kApiLightLowHeapRejectMaxBlockBytes = 1200;
constexpr uint32_t kApiLowHeapRejectFreeBytes = 6500;
constexpr uint32_t kApiLowHeapRejectMaxBlockBytes = 2500;
constexpr uint32_t kApiStatusLiveLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiStatusLiveLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiStatusStaticLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiStatusStaticLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiFleetLowHeapRejectFreeBytes = 4500;
constexpr uint32_t kApiFleetLowHeapRejectMaxBlockBytes = 1800;
constexpr uint32_t kApiProvStatusCompactFreeBytes = 3500;
constexpr uint32_t kApiProvStatusCompactMaxBlockBytes = 1400;
constexpr uint32_t kIndexLowHeapRejectFreeBytes = 3800;
constexpr uint32_t kIndexLowHeapRejectMaxBlockBytes = 2400;
constexpr uint32_t kStatusCacheTtlMs = 3000;
constexpr uint32_t kStatusLiveCacheTtlMs = 1500;
constexpr uint32_t kStatusStaticCacheTtlMs = 15000;
constexpr uint32_t kStatusLiteCacheTtlMs = 1000;
constexpr size_t kStatusCacheReserveBytes = 1600;
constexpr size_t kStatusLiveCacheReserveBytes = 1024;
constexpr size_t kStatusStaticCacheReserveBytes = 1024;
constexpr size_t kStatusLiteCacheReserveBytes = 384;
constexpr uint32_t kStatusCompatHitLogMinIntervalMs = 5000;
constexpr uint32_t kStatusLiveSseKeepAliveMs = 15000;
constexpr uint32_t kStatusLiveSseConnectMinFreeBytes = 5000;
constexpr uint32_t kStatusLiveSseConnectMinMaxBlockBytes = 2000;
constexpr uint32_t kStatusLiveSsePushHealthyMs = 2000;
constexpr uint32_t kStatusLiveSsePushWarnMs = 4000;
constexpr uint32_t kStatusLiveSsePushPressureMs = 8000;
constexpr uint32_t kStatusLiveSsePushSevereMs = 12000;
constexpr uint32_t kStatusLiveSsePressureWindowMs = 12000;
constexpr uint32_t kWebRequestPressureDurMs = 80;

#ifdef REGION_US
constexpr long kMinFrequencyHz = 902000000L;
constexpr long kMaxFrequencyHz = 928000000L;
constexpr long kDefaultFrequencyHz = 915000000L;
#else
constexpr long kMinFrequencyHz = 433000000L;
constexpr long kMaxFrequencyHz = 434790000L;
constexpr long kDefaultFrequencyHz = 433000000L;
#endif

bool isOwnLrsSoftApLike(const String &ssid) {
  String s = ssid;
  s.toLowerCase();
  if (!s.startsWith("lrs-")) return false;
  if (s.endsWith("-tx") || s.endsWith("-rx")) return true;
  // New role-independent AP naming: lrs-<8 hex chars>
  if (s.length() != 12) return false;
  for (size_t i = 4; i < 12; ++i) {
    const char c = s.charAt(i);
    const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!isHex) return false;
  }
  return true;
}

const char *wifiStatusText(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "ssid_not_found";
    case WL_SCAN_COMPLETED:
      return "scan_completed";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "connect_failed";
    case WL_CONNECTION_LOST:
      return "connection_lost";
    case WL_DISCONNECTED:
      return "disconnected";
#ifdef WL_WRONG_PASSWORD
    case WL_WRONG_PASSWORD:
      return "wrong_password";
#endif
#ifdef WL_NO_SHIELD
    case WL_NO_SHIELD:
      return "no_shield";
#endif
    default:
      return "unknown";
  }
}

const char *httpMethodText(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    default:
      return "OTHER";
  }
}

const char *remoteAckStateText(PeerAckState s) {
  switch (s) {
    case PeerAckState::Pending:
      return "pending";
    case PeerAckState::Ok:
      return "ok";
    case PeerAckState::Timeout:
      return "timeout";
    case PeerAckState::Unknown:
    default:
      return "unknown";
  }
}

const char *provisioningSessionStateText(ProvisioningSessionState s) {
  switch (s) {
    case ProvisioningSessionState::Discovering:
      return "discovering";
    case ProvisioningSessionState::DiscoveryRetry:
      return "discovery_retry";
    case ProvisioningSessionState::Ready:
      return "ready";
    case ProvisioningSessionState::Provisioning:
      return "provisioning";
    case ProvisioningSessionState::Complete:
      return "complete";
    case ProvisioningSessionState::Error:
      return "error";
    case ProvisioningSessionState::Idle:
    default:
      return "idle";
  }
}

const char *provisioningDeviceStateText(ProvisioningDeviceState s) {
  switch (s) {
    case ProvisioningDeviceState::Assigned:
      return "assigned";
    case ProvisioningDeviceState::Keying:
      return "keying";
    case ProvisioningDeviceState::AwaitVerify:
      return "await_verify";
    case ProvisioningDeviceState::Verified:
      return "verified";
    case ProvisioningDeviceState::Failed:
      return "failed";
    case ProvisioningDeviceState::Skipped:
      return "skipped";
    case ProvisioningDeviceState::Discovered:
    default:
      return "discovered";
  }
}

uint8_t parseAddressField(const JsonVariantConst &value, uint8_t fallback) {
  if (value.isNull()) {
    return fallback;
  }
  if (value.is<uint8_t>() || value.is<int>()) {
    const int n = value.as<int>();
    if (n >= 1 && n <= 254) return static_cast<uint8_t>(n);
    return fallback;
  }
  String text = String(static_cast<const char *>(value.as<const char *>()));
  text.trim();
  if (text.length() == 0) return fallback;

  long n = -1;
  if (text.startsWith("0x") || text.startsWith("0X")) {
    n = strtol(text.c_str(), nullptr, 16);
  } else {
    n = strtol(text.c_str(), nullptr, 10);
  }
  if (n < 1 || n > 254) {
    return fallback;
  }
  return static_cast<uint8_t>(n);
}

uint8_t parseAddressText(const String &text, uint8_t fallback) {
  String t = text;
  t.trim();
  if (t.length() == 0) return fallback;
  long n = -1;
  if (t.startsWith("0x") || t.startsWith("0X")) {
    n = strtol(t.c_str(), nullptr, 16);
  } else {
    n = strtol(t.c_str(), nullptr, 10);
  }
  if (n < 1 || n > 254) return fallback;
  return static_cast<uint8_t>(n);
}

bool parseBoolField(const JsonVariantConst &value, bool fallback) {
  if (value.isNull()) return fallback;
  if (value.is<bool>()) return value.as<bool>();
  if (value.is<int>()) return value.as<int>() != 0;
  const char *raw = value.as<const char *>();
  if (!raw) return fallback;
  String text(raw);
  text.trim();
  text.toLowerCase();
  if (text == "true" || text == "1" || text == "yes" || text == "on") return true;
  if (text == "false" || text == "0" || text == "no" || text == "off") return false;
  return fallback;
}

bool softApActiveNow() {
  const IPAddress apIp = WiFi.softAPIP();
  return apIp[0] != 0;
}

bool isDefaultDeploymentKey(const String &v) {
  String k = v;
  k.trim();
  return k == kDefaultDeploymentKey;
}

const char kLoginHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Login</title>
<style>
:root{--bg:#0b1220;--card:#111827;--txt:#e5e7eb;--muted:#94a3b8;--border:#334155;--field:#0f172a;--btn:#005f73}
body.light{--bg:#f4f6f8;--card:#fff;--txt:#122;--muted:#4b5563;--border:#d4dbe2;--field:#fff}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:linear-gradient(135deg,var(--bg),#111827);font-family:ui-sans-serif,system-ui;color:var(--txt);-webkit-text-size-adjust:100%}
body.light{background:linear-gradient(135deg,#e3f2fd,#f9fbff)}
.card{width:min(92vw,420px);background:var(--card);border:1px solid var(--border);border-radius:14px;padding:20px;box-shadow:0 12px 30px rgba(0,0,0,.22)}
h1{margin:0 0 10px;font-size:1.4rem}
p{margin:0 0 10px;color:var(--muted)}
label{display:block;margin:0 0 6px;color:var(--txt);font-size:14px;font-weight:600}
input{box-sizing:border-box;width:100%;padding:12px;border:1px solid var(--border);border-radius:10px;font-size:17px;background:var(--field);color:var(--txt)}
.pass-field{display:flex;align-items:center;gap:8px}
.pass-field input{flex:1 1 auto}
.pass-toggle{width:auto;margin:0;padding:10px 12px;border:1px solid var(--border);border-radius:10px;background:transparent;color:var(--txt);font-size:13px}
button{margin-top:10px;width:100%;padding:12px;border:0;border-radius:10px;background:var(--btn);color:#fff;font-size:16px}
.msg{margin-top:8px;font-size:14px;min-height:1.2em}.err{color:#b42318}.ok{color:#166534}
.top{display:flex;justify-content:flex-end}
.theme-btn{width:auto;margin:0;padding:6px 10px;border:1px solid var(--border);border-radius:999px;background:transparent;color:var(--txt)}
</style></head><body><div class="card">
<div class="top"><button id="themeBtn" class="theme-btn" type="button" onclick="toggleTheme()">☀</button></div>
<h1>LRS Device Console Login</h1>
<p id="hint">Use the device admin password. Username is not required.</p>
<form id="loginForm" autocomplete="on">
<input id="uname" name="username" type="text" autocomplete="username" value="admin" aria-hidden="true" tabindex="-1" style="position:absolute;left:-9999px;width:1px;height:1px;opacity:0;pointer-events:none" />
<label for="pw">Admin password</label>
<div class="pass-field"><input id="pw" name="password" type="password" autocomplete="current-password" placeholder="Enter admin password" /><button class="pass-toggle" type="button" onclick="togglePasswordField('pw',this)">Show</button></div>
<button id="btn" type="submit">Login</button>
</form>
<div id="msg" class="msg"></div>
</div>
<script>
const q=new URLSearchParams(location.search);
if(q.get('expired')==='1') document.getElementById('hint').innerText='Session expired. Please login again.';
if(q.get('logged_out')==='1') document.getElementById('hint').innerText='You have been logged out.';
const btn=document.getElementById('btn');
const loginForm=document.getElementById('loginForm');
const pw=document.getElementById('pw');
const msg=document.getElementById('msg');
let currentTheme='dark';
function applyTheme(theme){
 currentTheme = (theme === 'light') ? 'light' : 'dark';
 document.body.classList.toggle('light', currentTheme === 'light');
 const tb=document.getElementById('themeBtn');
 if(tb){ tb.innerText = currentTheme === 'dark' ? '☀' : '🌙'; }
 try{ localStorage.setItem('lrs_theme', currentTheme); }catch(e){}
}
function toggleTheme(){ applyTheme(currentTheme === 'dark' ? 'light' : 'dark'); }
function togglePasswordField(id,btn){
 const el=document.getElementById(id);
 if(!el) return;
 const show=el.type==='password';
 el.type=show?'text':'password';
 if(btn){ btn.innerText=show?'Hide':'Show'; }
}
async function login(){
 btn.disabled=true; msg.className='msg'; msg.innerText='Signing in...';
 try{
   const res=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:pw.value})});
   const out=await res.json().catch(()=>({}));
   if(!res.ok){
     msg.className='msg err';
     msg.innerText=out.error || 'Login failed';
     return;
   }
   msg.className='msg ok'; msg.innerText='Login successful';
   location.href = out.setup_required ? '/setup' : '/';
 }catch(e){
   msg.className='msg err'; msg.innerText=`Login failed: ${e.message}`;
 }finally{ btn.disabled=false; }
}
loginForm.addEventListener('submit', (e)=>{ e.preventDefault(); login(); });
try{ applyTheme(localStorage.getItem('lrs_theme') === 'light' ? 'light' : 'dark'); }catch(e){ applyTheme('dark'); }
pw.focus();
</script></body></html>
)HTML";

const char kFleetSetupHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Fleet Key Setup</title>
<style>
:root{--bg:#0b1220;--card:#111827;--txt:#e5e7eb;--muted:#94a3b8;--border:#334155;--field:#0f172a;--btn:#005f73;--btn2:#334155}
body.light{--bg:#f4f6f8;--card:#fff;--txt:#122;--muted:#4b5563;--border:#d4dbe2;--field:#fff;--btn2:#e5e7eb}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:linear-gradient(135deg,var(--bg),#111827);font-family:ui-sans-serif,system-ui;color:var(--txt);-webkit-text-size-adjust:100%}
body.light{background:linear-gradient(135deg,#e3f2fd,#f9fbff)}
.card{width:min(92vw,460px);background:var(--card);border:1px solid var(--border);border-radius:14px;padding:20px;box-shadow:0 12px 30px rgba(0,0,0,.22)}
h1{margin:0 0 8px;font-size:1.25rem} p{margin:0 0 12px;color:var(--muted)} label{display:block;margin:0 0 6px;font-size:14px;font-weight:600}
input{box-sizing:border-box;width:100%;padding:12px;border:1px solid var(--border);border-radius:10px;font-size:16px;background:var(--field);color:var(--txt)}
.row{display:flex;gap:8px;flex-wrap:wrap}.row button{flex:1 1 0}
button{margin-top:10px;padding:12px;border:0;border-radius:10px;background:var(--btn);color:#fff;font-size:15px}
button.secondary{background:var(--btn2);color:var(--txt);border:1px solid var(--border)}
.msg{margin-top:8px;font-size:14px;min-height:1.2em}.err{color:#b42318}.ok{color:#166534}.small{font-size:12px;color:var(--muted)}
</style></head><body><div class="card">
<h1>Set Fleet Key (Optional)</h1>
<p>This is shown once on first login. Set a shared Fleet key now to enable LoRa communication and fleet provisioning, or skip and configure later.</p>
<label for="fleet">Fleet key</label>
<input id="fleet" type="text" placeholder="Enter unique fleet key (min 16 chars)" />
<div class="small">Default key is blocked here. Use a unique key for this installation.</div>
<div class="row"><button id="saveBtn" type="button" onclick="saveKey()">Save Fleet Key</button><button id="skipBtn" class="secondary" type="button" onclick="skipKey()">Skip for Now</button></div>
<div id="msg" class="msg"></div>
</div><script>
const msg=document.getElementById('msg'); const fleet=document.getElementById('fleet'); const saveBtn=document.getElementById('saveBtn'); const skipBtn=document.getElementById('skipBtn');
function setBusy(b){ saveBtn.disabled=b; skipBtn.disabled=b; }
async function submit(body){
 setBusy(true); msg.className='msg'; msg.innerText='Saving...';
 try{
  const res=await fetch('/api/setup/fleet-key',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const out=await res.json().catch(()=>({}));
  if(!res.ok){ msg.className='msg err'; msg.innerText=out.error||'Failed'; return; }
  msg.className='msg ok'; msg.innerText='Saved';
  location.href='/';
 }catch(e){ msg.className='msg err'; msg.innerText=`Failed: ${e.message}`; }
 finally{ setBusy(false); }
}
function saveKey(){ submit({fleet_passphrase:fleet.value||''}); }
function skipKey(){ submit({skip:true}); }
fleet.focus();
</script></body></html>
)HTML";

const char kIndexLowHeapHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Console (Low-Memory Mode)</title>
<style>
:root{--bg:#08101d;--card:#0f1a2e;--txt:#e5e7eb;--muted:#94a3b8;--border:#31435f;--btn:#005f73}
body{margin:0;background:linear-gradient(180deg,#07111f,#0b1322);color:var(--txt);font-family:ui-sans-serif,system-ui;-webkit-text-size-adjust:100%}
.wrap{max-width:680px;margin:0 auto;padding:14px}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:14px;margin-bottom:12px}
h1{margin:0 0 6px;font-size:1.2rem} p{margin:0;color:var(--muted)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.tile{border:1px solid var(--border);border-radius:10px;padding:10px;background:#111c31}
.k{font-size:12px;color:var(--muted)} .v{font-size:1rem;font-weight:700;margin-top:4px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
button,a.btn{background:var(--btn);color:#fff;border:0;border-radius:10px;padding:10px 12px;text-decoration:none;font-size:15px}
a.btn.alt,button.alt{background:transparent;border:1px solid var(--border);color:var(--txt)}
.mono{font-family:ui-monospace,monospace}
</style></head><body><div class="wrap">
<div class="card">
 <h1>LRS Console (Low-memory mode)</h1>
 <p id="notice">Full console payload was skipped to protect stability. Showing lightweight status only.</p>
 <div class="row">
  <button onclick="refreshLite()">Refresh</button>
  <button class="alt" onclick="location.href='/?force_full=1'">Force full console</button>
  <a class="btn alt" href="/login">Login</a>
 </div>
 <div class="grid">
  <div class="tile"><div class="k">Role</div><div class="v" id="role">-</div></div>
  <div class="tile"><div class="k">Relay</div><div class="v" id="relay">-</div></div>
  <div class="tile"><div class="k">WiFi</div><div class="v" id="wifi">-</div></div>
  <div class="tile"><div class="k">LoRa</div><div class="v" id="lora">-</div></div>
  <div class="tile"><div class="k">Memory</div><div class="v" id="mem">-</div></div>
  <div class="tile"><div class="k">Updated</div><div class="v" id="updated">never</div></div>
 </div>
 <div style="margin-top:10px" class="mono" id="raw"></div>
</div>
<script>
let lastOk=0;
function setText(id,v){ const el=document.getElementById(id); if(el) el.innerText=String(v); }
function age(ms){ if(ms<1000) return `${ms} ms`; const s=Math.round(ms/1000); return `${s}s`; }
async function refreshLite(){
 try{
  const res=await fetch('/api/status-lite',{cache:'no-store'});
  if(res.status===401){ location.href='/login?expired=1'; return; }
  if(!res.ok) throw new Error(`HTTP ${res.status}`);
  const st=await res.json();
  lastOk=Date.now();
  setText('role', String(st.role||'-').toUpperCase());
  setText('relay', Number(st.relay_state)===1?'ON':'OFF');
  setText('wifi', st.sta_connected ? `${st.sta_rssi} dBm` : 'disconnected');
  setText('lora', Number(st.lora_last_packet_ms||0)>0 ? `${st.lora_last_rssi} dBm` : 'no packets');
  const hf=Number(st.heap_free_bytes||0), mb=Number(st.max_free_block_bytes||0);
  setText('mem', (hf&&mb) ? `${(hf/1024).toFixed(1)}k / ${(mb/1024).toFixed(1)}k` : '-');
  setText('updated', new Date().toLocaleTimeString());
  setText('raw', `heap=${hf} max=${mb} frag=${Number(st.heap_frag_percent||0)}% uptime=${Number(st.uptime_ms||0)}ms`);
 }catch(e){
  setText('notice', `Low-memory mode: ${e.message}`);
 }
}
refreshLite();
</script></body></html>
)HTML";

const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Console</title>
<style>
:root{--bg:#0b1220;--card:#111827;--accent:#005f73;--txt:#e5e7eb;--border:#334155;--field:#0f172a;--muted:#94a3b8;--link:#67e8f9}
body.light{--bg:#f4f6f8;--card:#fff;--txt:#122;--border:#d4dbe2;--field:#fff;--muted:#4b5563;--link:#0b5f75}
*{box-sizing:border-box}
body{margin:0;font-family:ui-sans-serif,system-ui;background:linear-gradient(135deg,var(--bg),#111827);color:var(--txt);-webkit-text-size-adjust:100%}
body.light{background:linear-gradient(135deg,#e3f2fd,#f9fbff)}
header{background:var(--accent);color:#fff;padding:12px 16px;font-weight:700;display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}
header .title{font-size:1.1rem}
header .wifi{font-size:.9rem;background:rgba(255,255,255,.16);border:1px solid rgba(255,255,255,.25);border-radius:999px;padding:6px 10px;white-space:nowrap;display:flex;align-items:center;gap:8px}
header .relay-head{font-size:.9rem;background:rgba(255,255,255,.16);border:1px solid rgba(255,255,255,.25);border-radius:999px;padding:6px 10px;white-space:nowrap}
header .relay-head.on{background:rgba(126,211,121,.24);border-color:rgba(126,211,121,.6)}
header .relay-head.off{background:rgba(255,255,255,.12)}
header .relay-head.mem-ok{background:rgba(126,211,121,.18);border-color:rgba(126,211,121,.48)}
header .relay-head.mem-warn{background:rgba(245,158,11,.18);border-color:rgba(245,158,11,.55);color:#ffe8bf}
header .relay-head.mem-crit{background:rgba(239,68,68,.18);border-color:rgba(239,68,68,.6);color:#ffd6d6}
header .reason-head{font-size:.85rem;background:rgba(255,255,255,.12);border:1px solid rgba(255,255,255,.22);border-radius:999px;padding:6px 10px;white-space:nowrap;display:none}
header .reason-head.show{display:inline-flex}
header .right{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
header .theme{margin-top:0;padding:6px 10px;min-width:38px;border:1px solid rgba(255,255,255,.35);background:rgba(255,255,255,.12);border-radius:999px}
header .logout{margin-top:0;padding:6px 10px;border:1px solid rgba(255,255,255,.35);background:rgba(255,255,255,.12);border-radius:999px}
main{padding:12px;display:grid;gap:12px;max-width:860px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:12px;box-shadow:0 8px 20px rgba(0,0,0,.12)}
label{display:block;font-size:12px;margin-top:8px}
.grid{display:grid;gap:8px;grid-template-columns:repeat(2,minmax(0,1fr))}
input,select,textarea{width:100%;padding:10px;border:1px solid var(--border);border-radius:8px;font-size:17px;background:var(--field);color:var(--txt)}
.pass-field{display:flex;align-items:center;gap:8px}
.pass-field input{flex:1 1 auto}
.pass-toggle{margin-top:0;padding:10px 12px;border:1px solid var(--border);border-radius:8px;background:transparent;color:var(--txt);white-space:nowrap}
input[type=checkbox]{width:18px;height:18px;padding:0}
.check-row{display:flex;align-items:center;gap:8px;margin-top:8px}
.radio-row{display:flex;align-items:center;gap:0;margin-top:8px;border:1px solid var(--border);border-radius:10px;overflow:hidden;width:max-content;max-width:100%}
.radio-row label{margin:0;display:flex;align-items:center;gap:10px;padding:10px 14px;cursor:pointer;user-select:none;font-weight:600;min-height:42px}
.radio-row label + label{border-left:1px solid var(--border)}
.radio-row input[type=radio]{width:18px;height:18px;accent-color:#4caf50;flex:0 0 auto}
.lora-field{display:flex;flex-direction:column}
.lora-field .radio-row{margin-top:6px}
.freq-wrap{display:flex;flex-direction:column;gap:8px}
.freq-wrap input[readonly]{opacity:.85}
.check-row label{margin:0}
button{margin-top:10px;padding:10px 14px;border:0;border-radius:8px;background:var(--accent);color:#fff}
button:disabled{opacity:.45;cursor:not-allowed;filter:saturate(.35)}
.small{font-size:12px;color:var(--muted);overflow-wrap:anywhere}
.key-strength{margin-top:6px;font-size:12px;font-weight:700}
.key-strength.weak{color:#fca5a5}
.key-strength.ok{color:#facc15}
.key-strength.strong{color:#86efac}
.actions{display:flex;gap:8px;flex-wrap:wrap}
#status{overflow-wrap:anywhere;line-height:1.4}
.inline-row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.hint{font-size:12px;opacity:.85}
details summary{cursor:pointer;font-weight:700;margin:6px 0}
.wifi-icon{display:inline-flex;align-items:center;justify-content:center;width:18px;height:14px}
.wifi-icon svg{width:18px;height:14px;display:block}
.wifi-icon .arc,.wifi-icon .dot{stroke:rgba(255,255,255,.35);fill:none;stroke-width:2;stroke-linecap:round}
.wifi-icon .dot{fill:rgba(255,255,255,.35);stroke:none}
.wifi-icon .x{stroke:#ff6b6b;stroke-width:2;stroke-linecap:round;display:none}
.wifi-icon.lv1 .dot,.wifi-icon.lv2 .dot,.wifi-icon.lv3 .dot,.wifi-icon.lv4 .dot{fill:#95d65e}
.wifi-icon.lv2 .a3,.wifi-icon.lv3 .a3,.wifi-icon.lv4 .a3{stroke:#95d65e}
.wifi-icon.lv3 .a2,.wifi-icon.lv4 .a2{stroke:#95d65e}
.wifi-icon.lv4 .a1{stroke:#95d65e}
.wifi-icon.lv0 .x{display:block}
.wifi-list{margin-top:8px}
.wifi-table{width:100%;border-collapse:collapse;font-size:14px}
.wifi-table th,.wifi-table td{padding:6px 8px;border-bottom:1px solid var(--border);text-align:left}
.wifi-table th:last-child,.wifi-table td:last-child{text-align:right}
.sig{display:inline-flex;align-items:flex-end;gap:1px;height:10px;margin-right:6px;vertical-align:-1px}
.sig i{display:block;width:2px;background:#b8c6cf;border-radius:2px}
.sig i:nth-child(1){height:3px}.sig i:nth-child(2){height:5px}.sig i:nth-child(3){height:7px}.sig i:nth-child(4){height:9px}
.sig.scan.lv1 i:nth-child(1),
.sig.scan.lv2 i:nth-child(-n+2),
.sig.scan.lv3 i:nth-child(-n+3),
.sig.scan.lv4 i:nth-child(-n+4){background:#2f9e64}
.sig.lora i{background:#d0d7df}
.sig.lora.lv1 i:nth-child(1),
.sig.lora.lv2 i:nth-child(-n+2),
.sig.lora.lv3 i:nth-child(-n+3),
.sig.lora.lv4 i:nth-child(-n+4){background:#111827}
.sec-chip{display:inline-flex;align-items:center;justify-content:center;min-width:18px;height:18px;border-radius:999px;font-size:11px;font-weight:700}
.sec-chip.y{background:#d9f3e2;color:#166534}
.sec-chip.n{background:#f3f4f6;color:#4b5563}
.link{color:var(--link);text-decoration:underline}
.menu-btn{margin-top:0;padding:6px 10px;min-width:38px;border:1px solid rgba(255,255,255,.35);background:rgba(255,255,255,.12);border-radius:999px}
.drawer-backdrop{position:fixed;inset:0;background:rgba(0,0,0,.4);opacity:0;pointer-events:none;transition:opacity .2s ease;z-index:20}
.drawer{position:fixed;left:0;top:0;bottom:0;width:min(82vw,290px);padding:16px 12px;background:var(--card);border-right:1px solid var(--border);transform:translateX(-100%);transition:transform .2s ease;z-index:21;overflow:auto}
.drawer h4{margin:4px 8px 10px 8px;font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
.navbtn{display:flex;align-items:center;gap:10px;width:100%;margin-top:0;padding:10px 12px;border:1px solid transparent;background:transparent;color:var(--txt);text-align:left;font-weight:600}
.navbtn.active{background:rgba(0,95,115,.22);border-color:var(--accent);color:#fff}
.navbtn.cog::before{content:'⚙';font-size:14px}
body.light .navbtn.active{color:#083344}
body.nav-open .drawer{transform:translateX(0)}
body.nav-open .drawer-backdrop{opacity:1;pointer-events:auto}
body.nav-open{overflow:hidden}
.tabbtn{background:#1f2937;color:#e5e7eb;border:1px solid var(--border)}
body.light .tabbtn{background:#e4eff3;color:#123;border:1px solid #bfd2da}
.tabbtn.active{background:var(--accent);color:#fff}
.page{display:none}
.page.active{display:block}
.settings-tabs{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
.settings-pane{display:none}
.settings-pane.active{display:block}
.fleet-pane{display:none}
.fleet-pane.active{display:block}
.system-tabs{display:flex;gap:8px;flex-wrap:wrap;margin:6px 0 10px 0}
.system-pane{display:none}
.system-pane.active{display:block}
.fleet-table{width:100%;border-collapse:collapse;font-size:14px}
.fleet-table th,.fleet-table td{padding:7px 8px;border-bottom:1px solid var(--border);text-align:left;vertical-align:middle}
.fleet-table th{color:var(--muted);font-weight:700}
.fleet-table tr.selected{background:rgba(0,95,115,.18)}
.fleet-row-actions{display:flex;gap:6px;flex-wrap:wrap}
.fleet-row-actions button{margin-top:0;padding:6px 8px;font-size:12px}
.fleet-detail{margin-top:10px;border:1px solid var(--border);border-radius:10px;padding:10px;background:rgba(255,255,255,.02)}
.fleet-detail-tabs{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px}
.fleet-detail-tabs .tabbtn{margin-top:0;padding:6px 10px}
.fleet-detail-grid{display:grid;grid-template-columns:150px 1fr;gap:6px 10px;font-size:14px}
.fleet-detail-grid .k{color:var(--muted)}
.fleet-detail-grid .v{font-weight:600;overflow-wrap:anywhere}
.chip{display:inline-flex;align-items:center;justify-content:center;padding:2px 8px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.chip.ok{background:#d9f6df;color:#166534;border-color:#3ea86b}
.chip.warn{background:#fef3c7;color:#92400e;border-color:#f59e0b}
.chip.err{background:#fee2e2;color:#b42318;border-color:#fca5a5}
.chip.neutral{background:#e5e7eb;color:#374151;border-color:#cfd4db}
.status-grid{display:grid;grid-template-columns:1.2fr 1fr;gap:12px}
.deploy-note{padding:10px;border:1px solid var(--border);border-radius:10px;background:rgba(255,255,255,.03);margin-bottom:10px}
.deploy-note.warn{border-color:#f59e0b;background:rgba(245,158,11,.10);color:#fef3c7}
.status-table{display:grid;grid-template-columns:150px 1fr;gap:6px 10px;font-size:15px}
.status-table .k{color:var(--muted)}
.status-table .v{font-weight:600;overflow-wrap:anywhere}
.status-table .v.copyable{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.status-table .section{grid-column:1/-1;font-weight:800;margin-top:4px;padding-top:6px;border-top:1px solid var(--border)}
.copy-btn{margin:0;padding:4px 8px;font-size:12px;border-radius:6px;background:#2f9e64;color:#fff}
.copy-btn:hover{filter:brightness(1.05)}
.relay-card{display:flex;flex-direction:column;justify-content:center;align-items:center;background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:12px;padding:12px}
.relay-badge{width:90px;height:90px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700}
.relay-badge.on{background:#d9f6df;color:#166534;border:2px solid #3ea86b}
.relay-badge.off{background:#f3f4f6;color:#4b5563;border:2px solid #c6cdd6}
.sensor-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.sensor-tile{background:rgba(255,255,255,.03);border:1px solid var(--border);border-radius:10px;padding:8px}
.sensor-state{display:inline-flex;align-items:center;justify-content:center;min-width:82px;padding:3px 8px;border-radius:999px;font-size:12px;font-weight:700}
.sensor-state.open{background:#f3f4f6;color:#4b5563}
.sensor-state.closed{background:#d9f6df;color:#166534}
.toast{position:fixed;right:14px;bottom:14px;background:#0b6a80;color:#fff;padding:10px 12px;border-radius:10px;box-shadow:0 8px 20px rgba(0,0,0,.2);font-size:14px;z-index:9999;display:none}
.toast.show{display:block}
.toast.err{background:#b42318}
.result-line{margin-top:8px;padding:8px 10px;border-radius:8px;border:1px solid var(--border);font-weight:700;display:none}
.result-line.show{display:block}
.result-line.ok{background:#d9f6df;color:#166534;border-color:#3ea86b}
.result-line.err{background:#fee2e2;color:#b42318;border-color:#fca5a5}
.spin{display:inline-block;width:12px;height:12px;border:2px solid rgba(255,255,255,.45);border-top-color:#fff;border-radius:50%;animation:sp 0.8s linear infinite;margin-right:8px;vertical-align:-2px}
@keyframes sp{to{transform:rotate(360deg)}}
@media(max-width:850px){.status-grid{grid-template-columns:1fr}}
@media(max-width:650px){.grid{grid-template-columns:1fr}}
</style></head>
<body><div id="drawerBackdrop" class="drawer-backdrop" onclick="toggleDrawer(false)"></div><aside id="appDrawer" class="drawer" aria-label="Main navigation"><h4>Menu</h4><button class="navbtn active" id="nav-status" onclick="showPage('status')">Status</button><button class="navbtn" id="nav-fleet" onclick="showPage('fleet')">Fleet</button><button class="navbtn" id="nav-sensors" onclick="showPage('sensors')">Sensors</button><button class="navbtn" id="nav-diagnostics" onclick="showPage('diagnostics')">Diagnostics</button><button class="navbtn" id="nav-logs" onclick="showPage('logs')">Logs</button><button class="navbtn cog" id="nav-settings" onclick="showPage('settings')">Settings</button></aside><header><button class="menu-btn" id="menuBtn" onclick="toggleDrawer()" title="Open menu" aria-label="Open menu">☰</button><div id="consoleTitle" class="title">LRS Device Console</div><div class="right"><div id="relayHeader" class="relay-head off">Relay: -</div><div id="heapHeader" class="relay-head off" title="Free heap">Heap: -</div><div id="loraBadge" class="wifi"><span id="loraIcon" class="sig lora lv0"><i></i><i></i><i></i><i></i></span><span id="loraText">LoRa</span></div><div id="wifiBadge" class="wifi"><span id="wifiIcon" class="wifi-icon lv0"><svg viewBox="0 0 20 14" aria-hidden="true"><path class="arc a1" d="M1 6.5c5-5 13-5 18 0"></path><path class="arc a2" d="M4.5 9c3-3 8-3 11 0"></path><path class="arc a3" d="M7.8 11.2c1.2-1.2 3.2-1.2 4.4 0"></path><circle class="dot" cx="10" cy="12.6" r="1.2"></circle><path class="x" d="M2 2l3 3"></path><path class="x" d="M5 2l-3 3"></path></svg></span><span id="wifiText">WiFi</span></div><button class="logout" onclick="logout()" title="Logout" aria-label="Logout">⎋</button><button class="theme" id="themeBtn" onclick="toggleTheme()">☀</button></div></header><main>
<section class="card page active" id="page-status">
<h3>Status</h3>
<div id="statusFleetShortcut" class="small" style="display:none;margin-bottom:10px"><a class="link" href="#" onclick="showPage('fleet');return false;">View fleet</a></div>
<div class="actions" style="margin-top:0;margin-bottom:8px"><button type="button" onclick="refreshStatus(true)">Refresh now</button><span id="statusLiveNotice" class="small" style="align-self:center">Waiting for device updates...</span></div>
<div class="status-grid">
<div>
<div id="statusTable">Loading status...</div>
</div>
<div class="relay-card">
<div id="relayBadge" class="relay-badge off">RELAY OFF</div>
<div id="relayMeta" class="small" style="margin-top:8px">Input: -, Link: -</div>
</div>
</div>
<h4 style="margin:10px 0 6px 0">Sensors</h4>
<div class="sensor-grid">
<div class="sensor-tile" id="sensorTempTile">Temperature: n/a</div>
<div class="sensor-tile" id="sensorRemoteTempTile">Remote LoRa temp: n/a</div>
<div class="sensor-tile" id="sensorInputTile">Dry contact input: <span class="sensor-state open">OPEN</span></div>
<div class="sensor-tile">Tank: n/a</div>
<div class="sensor-tile">Float: n/a</div>
<div class="sensor-tile">Flow: n/a</div>
</div>
</section>
<section class="card page" id="page-fleet">
<h3>Fleet</h3>
<div class="settings-tabs"><button class="tabbtn active" id="fleet-tab-devices" onclick="showFleetTab('devices')">Devices</button><button class="tabbtn" id="fleet-tab-manage" onclick="showFleetTab('manage')">Manage</button></div>
<div class="fleet-pane active" id="fleet-pane-devices"><div class="small" id="fleetSummary">Loading...</div><div id="fleetTableHost" style="margin-top:8px">Loading device list...</div><div id="fleetDetailHost" class="fleet-detail">Select a device to view details.</div></div>
<div class="fleet-pane" id="fleet-pane-manage"><div class="settings-tabs"><button class="tabbtn active" id="fleet-manage-tab-lora" onclick="showFleetManageTab('lora')">LoRa</button><button class="tabbtn" id="fleet-manage-tab-wifi" onclick="showFleetManageTab('wifi')">WiFi</button></div><div class="settings-pane" id="fleet-manage-pane-wifi"><div class="grid"><div style="grid-column:1/-1"><label>WiFi provisioning</label><div class="small">Uses STA SSID/password from Settings > Network and broadcasts them to devices in the same fleet.</div><div class="actions"><button type="button" onclick="provisionFleetWifi()">Send WiFi to Fleet (LoRa)</button></div><div id="wifiProvisionResult" class="result-line"></div></div></div></div><div class="settings-pane active" id="fleet-manage-pane-lora"><div class="grid"><div style="grid-column:1/-1"><label>LoRa provisioning</label><div class="small">Discover factory-key devices, auto-resolve duplicate addresses, and provision them into this fleet.</div><div class="grid"><div><label>Estimated devices</label><input id="prov_estimated_count" type="number" min="1" max="250" value="10" /></div></div><div class="actions"><button type="button" onclick="startFleetProvisioningDiscovery()">Start Discovery</button><button type="button" onclick="searchMoreFleetProvisioning()" title="Search more" aria-label="Search more">↻</button><button type="button" onclick="cancelFleetProvisioning()">Cancel</button></div><div id="provWizardResult" class="result-line"></div><div id="provWizardSummary" class="small" style="margin-top:6px"></div><div style="overflow:auto;max-height:260px;border:1px solid var(--border);border-radius:10px;margin-top:8px"><table class="table" style="margin:0"><thead><tr><th>Chip ID</th><th>Cur</th><th>New</th><th>FW</th><th>RSSI</th><th>Status</th></tr></thead><tbody id="provWizardRows"><tr><td colspan="6" class="small">No provisioning session active.</td></tr></tbody></table></div><div class="actions" style="margin-top:8px"><button type="button" onclick="provisionFleetAll()" id="provProvisionAllBtn" disabled>Provision All</button></div></div></div></div></div>
</section>
<section class="card page" id="page-settings"><h3>Settings</h3><div class="settings-tabs"><button class="tabbtn active" id="settings-tab-lora" onclick="showSettingsTab('lora')">LoRa</button><button class="tabbtn" id="settings-tab-network" onclick="showSettingsTab('network')">Network</button><button class="tabbtn" id="settings-tab-mqtt" onclick="showSettingsTab('mqtt')">MQTT</button><button class="tabbtn" id="settings-tab-system" onclick="showSettingsTab('system')">System</button></div><div class="settings-pane active" id="settings-pane-lora"><div class="grid">
<div class="lora-field"><label>Role</label><div class="radio-row"><label><input type="radio" name="role_tx_radio" id="role_tx_true" checked /> Transmitter</label><label><input type="radio" name="role_tx_radio" id="role_tx_false" /> Receiver</label></div><input id="role_tx" type="hidden" value="true" /></div>
<div class="lora-field"><label>Frequency (MHz)</label><div class="freq-wrap"><div class="radio-row"><label><input type="radio" name="freq_preset" id="freq_433" /> 433</label><label><input type="radio" name="freq_preset" id="freq_915" /> 915</label></div><div class="small" id="freq_selected_text">Selected: 433.000 MHz</div><input id="lora_frequency_mhz" type="hidden" /></div></div>
<div style="grid-column:1/-1"><label>Deployment Key (Encryption)</label><input id="fleet_passphrase" /><div id="fleet_passphrase_strength" class="key-strength"></div><div class="small">Must be unique per installation to prevent nearby systems from controlling each other.<br>Use at least 16 characters.<br>Examples: <code>fairview-generator-start-line-alpha42</code>, <code>smith-load-management-south-basin-27</code>, <code>farm-pump-control-west-field-9k</code>.</div></div>
<div><label id="local_address_label">Local address</label><input id="local_address" type="text" /><div class="hint" id="local_address_hex"></div></div>
<div><label id="remote_address_label">Remote address</label><input id="remote_address" type="text" /><div class="hint" id="remote_address_hex"></div></div>
</div>
<details><summary>Advanced</summary><div class="grid">
<div><label>TX power</label><input id="lora_tx_power" type="number" min="2" max="20" /></div>
<div><label>Spreading factor</label><input id="lora_spreading_factor" type="number" min="6" max="12" /></div>
<div><label>Bandwidth (Hz)</label><input id="lora_bandwidth_hz" type="number" /></div>
<div><label>Coding rate (5-8)</label><input id="lora_coding_rate" type="number" min="5" max="8" /></div>
<div><label>Heartbeat (seconds)</label><input id="heartbeat_s" type="number" min="60" max="3600" /></div>
<div><label>ACK timeout (seconds)</label><input id="ack_timeout_s" type="number" min="5" max="600" /></div>
<div id="tx_mqtt_remote_retry_row"><label>MQTT remote retry timeout (seconds)</label><input id="mqtt_remote_retry_timeout_s" type="number" min="5" max="3600" /><div class="small">TX only. Retry remote MQTT LoRa commands until this timeout is reached.</div></div>
<div id="tx_polling_enabled_row" style="grid-column:1/-1"><div class="check-row"><input id="tx_mqtt_remote_polling_enabled" type="checkbox" /><label for="tx_mqtt_remote_polling_enabled">Enable scheduled remote polling</label></div><div class="small">TX only. When disabled, `poll_interval_s` schedules are ignored but `poll_now` still works.</div></div>
<div id="tx_polling_default_row"><label>Default remote poll interval (seconds)</label><input id="tx_mqtt_remote_default_poll_interval_s" type="number" min="60" max="3600" /><div class="small">TX only. Applied to newly discovered remote nodes. Minimum 60s to reduce LoRa duty-cycle risk.</div></div>
<div id="rx_push_on_change_row" style="grid-column:1/-1"><div class="check-row"><input id="rx_push_on_change_enabled" type="checkbox" /><label for="rx_push_on_change_enabled">RX push on input change</label></div><div class="small">RX only. Sends a LoRa status update immediately on dry-contact change, rate-limited by minimum interval.</div></div>
<div id="rx_push_interval_row"><label>RX push minimum interval (seconds)</label><input id="rx_push_min_interval_s" type="number" min="60" max="3600" /><div class="small">RX only. Guardrail range 60..3600 seconds.</div></div>
<div id="tx_input_lora_control_row" style="grid-column:1/-1"><div class="check-row"><input id="tx_input_lora_control_enabled" type="checkbox" /><label for="tx_input_lora_control_enabled">Input drives LoRa relay control</label></div><div class="small">When disabled, TX still reports local input but does not send input-driven LoRa relay commands.</div></div>
</div><div class="small">Guardrail: heartbeat is limited to >= 60 seconds to reduce LoRa duty-cycle risk.</div></details><div class="actions"><button onclick="saveLora()">Save</button></div></div><div class="settings-pane" id="settings-pane-network"><div class="grid">
<div style="grid-column:1/-1"><div class="inline-row"><button onclick="scanWifi()">Rescan SSIDs</button></div><div id="wifi_scan_list" class="wifi-list"></div></div>
<div><label>STA SSID</label><input id="wifi_sta_ssid" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" data-1p-ignore="true" data-lpignore="true" /></div><div><label>STA Password</label><div class="pass-field"><input id="wifi_sta_password" type="password" autocomplete="new-password" autocapitalize="none" autocorrect="off" spellcheck="false" data-1p-ignore="true" data-lpignore="true" /><button class="pass-toggle" type="button" onclick="togglePasswordField('wifi_sta_password',this)">Show</button></div></div>
<div><div class="check-row"><input id="ap_always_on" type="checkbox" /><label for="ap_always_on">Keep Soft AP enabled</label></div></div><div></div>
<div style="grid-column:1/-1"><label id="lan_hostname_label">LAN hostname</label><input id="lan_hostname" /><div class="hint" id="lan_hostname_hint">Used as the device hostname for WiFi and OTA.</div><div class="hint" id="lan_hostname_preview_wrap" style="display:none">URL: <span id="lan_hostname_preview">http://lrs.local</span></div></div>
</div><div class="actions"><button onclick="saveNetwork()">Save</button><button id="btnTestSta" onclick="testSta()">Test</button></div><div id="netTestResult" class="result-line"></div></div><div class="settings-pane" id="settings-pane-mqtt"><div class="grid">
<div style="grid-column:1/-1"><div class="check-row"><input id="mqtt_enabled" type="checkbox" /><label for="mqtt_enabled">Enable MQTT</label></div></div>
<div><label>Broker host</label><input id="mqtt_host" /></div>
<div><label>Broker port</label><input id="mqtt_port" type="number" min="1" max="65535" /></div>
<div><label>MQTT user</label><input id="mqtt_user" /></div>
<div><label>MQTT password</label><input id="mqtt_password" /></div>
<div style="grid-column:1/-1"><label>Topic root</label><input id="mqtt_topic_root" /></div>
</div><div class="small" style="margin-top:4px">Control topics are per-device under &lt;topic_root&gt;/lrs-&lt;chipid&gt;.</div><div class="actions"><button onclick="saveMqtt()">Save</button><button onclick="testMqtt()">Test</button></div><div id="mqttTestResult" class="result-line"></div></div><div class="settings-pane" id="settings-pane-system"><div class="system-tabs"><button class="tabbtn active" id="system-tab-security" onclick="showSystemTab('security')">Security</button><button class="tabbtn" id="system-tab-configuration" onclick="showSystemTab('configuration')">Configuration</button><button class="tabbtn" id="system-tab-maintenance" onclick="showSystemTab('maintenance')">Maintenance</button></div><div class="system-pane active" id="system-pane-security"><div class="grid"><div><label>Admin password</label><input id="admin_password" type="password" /></div></div><div class="actions"><button onclick="saveSystem()">Save</button></div></div><div class="system-pane" id="system-pane-configuration"><div class="grid"><div style="grid-column:1/-1"><label>Configuration</label><div class="actions"><button onclick="window.location='/api/settings/export'">Export Config</button><button onclick="document.getElementById('importFile').click()">Import Config</button><input type="file" id="importFile" accept="application/json" style="display:none" onchange="importConfig(this.files&&this.files[0])"></div></div></div><pre id="factory"></pre></div><div class="system-pane" id="system-pane-maintenance"><div class="grid"><div style="grid-column:1/-1"><label>Firmware OTA</label><div class="actions"><input id="otaFile" type="file" accept=".bin,application/octet-stream" /><button onclick="uploadOta()">Upload OTA</button><span id="otaResult" class="small"></span></div></div><div style="grid-column:1/-1"><label>Device actions</label><div class="actions"><button onclick="window.location='/api/logs.csv'">Download Logs CSV</button><button onclick="reboot()">Reboot</button></div></div><div style="grid-column:1/-1"><label>Factory reset</label><div class="grid"><div><label>Confirm admin password</label><input id="factory_reset_password" type="password" autocomplete="current-password" /></div><div><div class="check-row"><input id="factory_reset_keep_fleet_local" type="checkbox" /><label for="factory_reset_keep_fleet_local">Keep shared fleet key</label></div><div class="small">Tick to keep this device in the LoRa fleet.</div><div class="check-row"><input id="factory_reset_keep_wifi_local" type="checkbox" /><label for="factory_reset_keep_wifi_local">Keep WiFi credentials</label></div><div class="small">Tick to keep STA SSID/password after reset.</div></div></div><div class="actions"><button onclick="factoryResetLocal()">Factory Reset Device</button></div><div id="factoryResetResult" class="result-line"></div></div></div></div></section>
<section class="card page" id="page-sensors"><h3>Sensors</h3>
<h4 style="margin:6px 0 8px 0">Temperature Sensor</h4>
<div class="check-row" style="margin-bottom:8px"><input id="sensor_temp_enabled" type="checkbox" /><label for="sensor_temp_enabled">Enable DS18B20 (GPIO0)</label></div>
<div id="sensorDiag" class="sensor-grid" style="margin-bottom:10px">
<div class="sensor-tile" id="sensorDiagState">DS18B20: checking...</div>
<div class="sensor-tile" id="sensorDiagTemp">Temperature: n/a</div>
<div class="sensor-tile" id="sensorDiagAddr">Address: n/a</div>
<div class="sensor-tile" id="sensorDiagLast">Last read: n/a</div>
</div>
<div class="small">Data pin is fixed to GPIO0 on this hardware. Temperature is sampled automatically at heartbeat/2 (twice per heartbeat period, minimum 2s).</div><div class="actions"><button onclick="saveSensors()">Save</button></div></section>
<section class="card page" id="page-diagnostics"><h3>Diagnostics</h3><div id="diagGrid" class="sensor-grid"></div><h4 style="margin:10px 0 6px 0">System Information</h4><div id="diagSystem" class="status-table"></div><div id="diagText" class="small"></div></section>
<section class="card page" id="page-logs"><h3>Logs</h3><div class="actions"><button onclick="refreshLogs()">Refresh</button><button onclick="window.location='/api/logs.csv'">Download Logs CSV</button></div><pre id="logView" style="max-height:320px;overflow:auto"></pre></section>
</main>
<footer style="max-width:860px;margin:0 auto 12px;padding:0 12px;"><div class="small card">HW: v1.2 | Batch: 251101 | <span id="footerFw">FW: -</span></div></footer>
<div id="toast" class="toast"></div>
<script>
const FREQ_MIN_MHZ = 400.0;
const FREQ_MAX_MHZ = 1000.0;
const MIN_DEPLOYMENT_KEY_LEN = 16;
)HTML"
#if LRS_ENABLE_MDNS
R"HTML(const UI_MDNS_ENABLED = true;
)HTML"
#else
R"HTML(const UI_MDNS_ENABLED = false;
)HTML"
#endif
R"HTML(
let statusFailCount = 0;
let activePage = 'status';
let activeSettingsTab = 'lora';
let activeSystemTab = 'security';
let currentTheme = 'dark';
let staIsConnected = false;
let connectedStaSsid = '';
let staTestInFlight = false;
let currentStaIp = '';
let currentLanMdns = '';
let currentApIp = '';
let currentApMdns = '';
let lastRoleIsTx = false;
let fleetDevicesCache = [];
let selectedFleetDeviceAddr = 0;
let fleetDeviceDetailTab = 'state';
let activeFleetTab = 'devices';
let activeFleetManageTab = 'lora';
let statusRefreshInFlight = false;
let headerStatusRefreshInFlight = false;
let sessionRefreshInFlight = false;
let fleetRefreshInFlight = false;
let fleetRefreshDebounceTimer = 0;
let provStatusInFlight = false;
let provLastStatusRefreshMs = 0;
let provUiSessionActive = false;
let provUiSessionState = 'idle';
let settingsPageLoaded = false;
let settingsPageLoadInFlight = false;
let wifiProvisionResultTimer=0;
let statusStaticCache = null;
let statusStaticLoadInFlight = false;
let statusLiveEventSource = null;
let statusLiveSseConnected = false;
let statusLiveSseLastMessageMs = 0;
let statusLiveSseReconnectTimer = 0;
let statusLiveSseBackoffMs = 1000;
let statusLiveUiTicker = 0;
let statusLiveHasLiveData = false;
let statusDegradedLiteMode = false;

function parseAddress(v){
 const t=String(v||'').trim();
 if(!t.length) return NaN;
 if(/^0x[0-9a-f]+$/i.test(t)) return parseInt(t,16);
 return parseInt(t,10);
}
function toHexByte(n){ return '0x'+Number(n).toString(16).toUpperCase().padStart(2,'0'); }
 
function refreshAddressHints(){
 const local=parseAddress(document.getElementById('local_address').value);
 const remote=parseAddress(document.getElementById('remote_address').value);
 document.getElementById('local_address_hex').innerText=Number.isInteger(local)?`hex ${toHexByte(local)}`:'enter dec or hex (e.g. 10 or 0x0A)';
 document.getElementById('remote_address_hex').innerText=Number.isInteger(remote)?`hex ${toHexByte(remote)}`:'enter dec or hex (e.g. 10 or 0x0A)';
}
function refreshRoleLabels(){
 const tx=document.getElementById('role_tx').value==='true';
 document.getElementById('local_address_label').innerText=tx?'TX local address (source)':'RX local address';
 document.getElementById('remote_address_label').innerText=tx?'RX remote address (destination)':'TX remote address (source)';
 const txInputRow=document.getElementById('tx_input_lora_control_row');
 if(txInputRow){ txInputRow.style.display = tx ? '' : 'none'; }
 const txMqttRetryRow=document.getElementById('tx_mqtt_remote_retry_row');
 if(txMqttRetryRow){ txMqttRetryRow.style.display = tx ? '' : 'none'; }
 const txPollingEnabledRow=document.getElementById('tx_polling_enabled_row');
 if(txPollingEnabledRow){ txPollingEnabledRow.style.display = tx ? '' : 'none'; }
 const txPollingDefaultRow=document.getElementById('tx_polling_default_row');
 if(txPollingDefaultRow){ txPollingDefaultRow.style.display = tx ? '' : 'none'; }
 const rxPushOnChangeRow=document.getElementById('rx_push_on_change_row');
 if(rxPushOnChangeRow){ rxPushOnChangeRow.style.display = tx ? 'none' : ''; }
 const rxPushIntervalRow=document.getElementById('rx_push_interval_row');
 if(rxPushIntervalRow){ rxPushIntervalRow.style.display = tx ? 'none' : ''; }
}
function refreshHostnamePreview(){
 if(!UI_MDNS_ENABLED) return;
 const label=document.getElementById('lan_hostname_label');
 const hint=document.getElementById('lan_hostname_hint');
 const wrap=document.getElementById('lan_hostname_preview_wrap');
 const preview=document.getElementById('lan_hostname_preview');
 if(label) label.innerText='LAN hostname (mDNS)';
 if(hint) hint.innerText='Used as the device hostname for WiFi, OTA, and LAN mDNS.';
 if(wrap) wrap.style.display='';
 if(!preview) return;
 const raw=(document.getElementById('lan_hostname').value||'').trim()||'lrs';
 preview.innerHTML=`<a class="link" href="http://${raw}.local">http://${raw}.local</a>`;
}
function isDefaultDeploymentKey(v){
 return String(v||'').trim() === 'lora-default-passphrase';
}
function deploymentKeyStrength(v){
 const s=String(v||'').trim();
 if(!s.length){ return {cls:'', text:`Enter deployment key (min ${MIN_DEPLOYMENT_KEY_LEN} chars).`}; }
 if(isDefaultDeploymentKey(s)){ return {cls:'weak', text:'Weak: default key is blocked.'}; }
 const hasLower=/[a-z]/.test(s);
 const hasUpper=/[A-Z]/.test(s);
 const hasDigit=/\d/.test(s);
 const hasSymbol=/[^A-Za-z0-9]/.test(s);
 let score=0;
 if(s.length >= MIN_DEPLOYMENT_KEY_LEN) score++;
 if(s.length >= 24) score++;
 if(hasLower && hasUpper) score++;
 if(hasDigit) score++;
 if(hasSymbol) score++;
 if(s.length < MIN_DEPLOYMENT_KEY_LEN){
  return {cls:'weak', text:`Weak: too short (${s.length}/${MIN_DEPLOYMENT_KEY_LEN}).`};
 }
 if(score >= 4){
  return {cls:'strong', text:'Strong: good length and character diversity.'};
 }
 if(score >= 2){
  return {cls:'ok', text:'OK: acceptable, but longer/more diverse is better.'};
 }
 return {cls:'weak', text:'Weak: increase length and mix characters.'};
}
function updateDeploymentKeyStrength(){
 const input=document.getElementById('fleet_passphrase');
 const out=document.getElementById('fleet_passphrase_strength');
 if(!input || !out) return;
 const s=deploymentKeyStrength(input.value);
 out.className = `key-strength${s.cls ? ` ${s.cls}` : ''}`;
 out.innerText = s.text;
}
function refreshFreqPreset(){
 const input=document.getElementById('lora_frequency_mhz');
 const f433=document.getElementById('freq_433');
 const f915=document.getElementById('freq_915');
 const txt=document.getElementById('freq_selected_text');
 if(!input || !f433 || !f915) return;
 const mhz=Number(input.value);
 const near=(a,b)=>Math.abs(a-b)<0.01;
 if(near(mhz,433.0)){ f433.checked=true; if(txt) txt.innerText='Selected: 433.000 MHz'; return; }
 if(near(mhz,915.0)){ f915.checked=true; if(txt) txt.innerText='Selected: 915.000 MHz'; return; }
 f433.checked=true;
 input.value='433.000';
 if(txt) txt.innerText='Selected: 433.000 MHz';
}
function bindFreqPreset(){
 const input=document.getElementById('lora_frequency_mhz');
 const f433=document.getElementById('freq_433');
 const f915=document.getElementById('freq_915');
 if(!input || !f433 || !f915) return;
 const apply=()=>{
  if(f433.checked){ input.value='433.000'; refreshFreqPreset(); return; }
  input.value='915.000';
  refreshFreqPreset();
 };
 f433.addEventListener('change',apply);
 f915.addEventListener('change',apply);
}
function escapeHtml(v){
 return String(v??'').replace(/[&<>"']/g,(m)=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}
function copyButtonHtml(value, label='Value'){
 const txt=String(value??'').trim();
 const low=txt.toLowerCase();
 if(!txt || low==='n/a' || low==='not_set') return '';
 return `<button type="button" class="copy-btn" data-copy="${escapeHtml(txt)}" data-label="${escapeHtml(label)}" onclick="copyFromButton(this)">Copy</button>`;
}
function copyableValueHtml(contentHtml, copyValue, label='Value'){
 return `<span>${contentHtml}</span>${copyButtonHtml(copyValue, label)}`;
}
async function writeClipboard(text){
 if(navigator.clipboard && navigator.clipboard.writeText){
  await navigator.clipboard.writeText(text);
  return;
 }
 const ta=document.createElement('textarea');
 ta.value=text;
 ta.setAttribute('readonly','readonly');
 ta.style.position='fixed';
 ta.style.opacity='0';
 document.body.appendChild(ta);
 ta.focus();
 ta.select();
 document.execCommand('copy');
 document.body.removeChild(ta);
}
async function copyFromButton(btn){
 if(!btn) return;
 const text=String(btn.dataset.copy||'').trim();
 if(!text){ showToast('Nothing to copy', true); return; }
 const label=String(btn.dataset.label||'Value');
 try{
  await writeClipboard(text);
  showToast(`${label} copied`);
 }catch(e){
  showToast(`Copy failed: ${e.message}`, true);
 }
}
function rssiToLevel(rssi){
 if(rssi >= -67) return 4;
 if(rssi >= -75) return 3;
 if(rssi >= -85) return 2;
 if(rssi > -120) return 1;
 return 0;
}
function sigIconHtml(rssi, mode='scan'){
 const lv=rssiToLevel(Number(rssi));
 return `<span class="sig ${mode} lv${lv}"><i></i><i></i><i></i><i></i></span>`;
}
function setWifiBadge(level, text){
 const icon=document.getElementById('wifiIcon');
 const wt=document.getElementById('wifiText');
 if(icon){ icon.className=`wifi-icon lv${level}`; }
 if(wt){ wt.innerText=text; }
}
function setLoraBadge(level, text){
 const icon=document.getElementById('loraIcon');
 const lt=document.getElementById('loraText');
 if(icon){ icon.className=`sig lora lv${level}`; }
 if(lt){ lt.innerText=text; }
}
function humanAgeMs(ms){
 const s=Math.max(0, Math.floor(Number(ms||0)/1000));
 if(s < 60) return `${s}s ago`;
 const m=Math.floor(s/60);
 if(m < 60) return `${m}m ago`;
 const h=Math.floor(m/60);
 return `${h}h ago`;
}
function humanAgeMsShort(ms){
 const s=Math.max(0, Math.floor(Number(ms||0)/1000));
 if(s < 60) return `${s}s`;
 const m=Math.floor(s/60);
 if(m < 60) return `${m}m`;
 const h=Math.floor(m/60);
 return `${h}h`;
}
function fleetDeviceStaleThresholdMs(r){
 const staleAfterMs=Math.max(0, Number(r.stale_after_ms||0));
 if(staleAfterMs>0) return staleAfterMs;
 const intervalMs=Math.max(0, Number(r.expected_interval_ms||r.poll_interval_ms||0));
 const base=intervalMs>0 ? intervalMs*3 : 300000;
 return Math.max(180000, base);
}
function fleetDeviceIsStale(r){
 const seen=Number(r.last_seen_ms||0);
 if(seen<=0) return true;
 return Number(r.last_seen_age_ms||0) > fleetDeviceStaleThresholdMs(r);
}
function ackChipClass(state){
 const s=String(state||'unknown').toLowerCase();
 if(s==='ok') return 'ok';
 if(s==='pending') return 'warn';
 if(s==='timeout') return 'err';
 return 'neutral';
}
function freshnessChip(r){
 if(fleetDeviceIsStale(r)) return `<span class="chip err">stale</span>`;
 if(Number(r.last_seen_ms||0)===0) return `<span class="chip neutral">unknown</span>`;
 return `<span class="chip ok">fresh</span>`;
}
function fleetDeviceAddrHex(r){
 if(r && r.addr_hex) return String(r.addr_hex);
 const n=Number(r.address||0);
 if(!Number.isFinite(n) || n<=0) return '0x00';
 return toHexByte(n);
}
function relayChip(v){
 const on=Number(v||0)===1;
 return `<span class="chip ${on?'ok':'neutral'}">${on?'relay on':'relay off'}</span>`;
}
function inputChip(v){
 const closed=Number(v||0)===1;
 return `<span class="chip ${closed?'ok':'neutral'}">${closed?'input closed':'input open'}</span>`;
}
function fleetDeviceTempText(r){
 if(!r || !r.temp_valid) return 'n/a';
 return `${Number(r.temp_c||0).toFixed(1)} C`;
}
function reasonLabel(v){
 const r=String(v||'').toLowerCase();
 if(r==='ack_timeout') return 'ack_timeout';
 if(r==='no_lora_link') return 'no_lora_link';
 if(r==='input_open') return 'input_open';
 if(r==='boot') return 'boot';
 if(r==='wait_ack') return 'wait_ack';
 return r || 'unknown';
}
function inputValue(id){
 const el=document.getElementById(id);
 return el ? String(el.value||'') : '';
}
function normalizedInputValue(id){
 return inputValue(id).trim();
}
function currentStaMatchesConnected(){
 if(!staIsConnected) return false;
 const ssid=normalizedInputValue('wifi_sta_ssid');
 if(!ssid.length) return false;
 return ssid===connectedStaSsid;
}
function updateStaTestButtonState(){
 const btn=document.getElementById('btnTestSta');
 if(!btn) return;
 const hasSsid=normalizedInputValue('wifi_sta_ssid').length>0;
 const unchanged=currentStaMatchesConnected();
 const disabled=staTestInFlight || !hasSsid || unchanged;
 btn.disabled=disabled;
 if(unchanged){
  btn.title='Already connected with these STA credentials. Change SSID or password to run a test.';
 }else if(!hasSsid){
  btn.title='Enter an SSID to test.';
 }else{
  btn.title='';
 }
}
function stripPort(host){
 return String(host||'').trim().toLowerCase().replace(/:\d+$/,'');
}
function normalizeLanHost(raw){
 let h=String(raw||'').trim().toLowerCase();
 if(!h.length) return '';
 h=h.replace(/^https?:\/\//,'');
 h=h.replace(/\/.*$/,'');
 if(h.endsWith('.local')) return h;
 return `${h}.local`;
}
function startLanHostnameRedirect(hostname){
 if(!UI_MDNS_ENABLED) return;
 const targetHost=normalizeLanHost(hostname);
 if(!targetHost) return;
 const targetUrl=`http://${targetHost}/`;
 const el=document.getElementById('netTestResult');
 const total=8;
 let left=total;
 if(window.__lanRedirectTimer){ clearInterval(window.__lanRedirectTimer); }
 const paint=()=>{
  if(!el) return;
  el.className='result-line show ok';
  el.innerText=`Network saved. Switching to ${targetUrl} in ${left}s...`;
 };
 paint();
 window.__lanRedirectTimer=setInterval(()=>{
  left--;
  if(left<=0){
   clearInterval(window.__lanRedirectTimer);
   window.__lanRedirectTimer=null;
   location.href=targetUrl;
   return;
  }
  paint();
 },1000);
}
function isLikelyStaSessionPath(){
 const host=stripPort(location.hostname);
 const staIp=stripPort(currentStaIp);
 const lanMdns=stripPort(currentLanMdns);
 if(host.length===0) return false;
 if(staIp && host===staIp) return true;
 if(UI_MDNS_ENABLED && lanMdns && host===lanMdns) return true;
 return false;
}
function toggleDrawer(force){
 const open = (typeof force === 'boolean') ? force : !document.body.classList.contains('nav-open');
 document.body.classList.toggle('nav-open', open);
 const btn=document.getElementById('menuBtn');
 if(btn){
  btn.innerText = open ? '✕' : '☰';
  btn.setAttribute('aria-expanded', open ? 'true' : 'false');
 }
}
function showSettingsTab(tab){
 const target = ['lora','network','mqtt','system'].includes(tab) ? tab : 'lora';
 activeSettingsTab = target;
 ['lora','network','mqtt','system'].forEach(p=>{
  const pane=document.getElementById(`settings-pane-${p}`);
  const btn=document.getElementById(`settings-tab-${p}`);
  if(pane) pane.classList.toggle('active', p===target);
  if(btn) btn.classList.toggle('active', p===target);
 });
 if(target==='system'){ showSystemTab(activeSystemTab); }
}
function showSystemTab(tab){
 const target = ['security','configuration','maintenance'].includes(tab) ? tab : 'security';
 activeSystemTab = target;
 ['security','configuration','maintenance'].forEach(p=>{
  const pane=document.getElementById(`system-pane-${p}`);
  const btn=document.getElementById(`system-tab-${p}`);
  if(pane) pane.classList.toggle('active', p===target);
  if(btn) btn.classList.toggle('active', p===target);
 });
}
function showFleetTab(tab){
 const target = (tab==='manage') ? 'manage' : 'devices';
 activeFleetTab = target;
 ['devices','manage'].forEach(p=>{
  const pane=document.getElementById(`fleet-pane-${p}`);
  const btn=document.getElementById(`fleet-tab-${p}`);
 if(pane) pane.classList.toggle('active', p===target);
 if(btn) btn.classList.toggle('active', p===target);
 });
 if(activePage==='fleet' && target==='devices'){
  if(fleetRefreshDebounceTimer){ clearTimeout(fleetRefreshDebounceTimer); fleetRefreshDebounceTimer=0; }
  fleetRefreshDebounceTimer=setTimeout(()=>{ fleetRefreshDebounceTimer=0; refreshFleet(); }, 250);
 }
 if(activePage==='fleet' && target==='manage'){ showFleetManageTab(activeFleetManageTab); }
 syncPagePolling();
}
function showFleetManageTab(tab){
 const target = (tab==='lora') ? 'lora' : 'wifi';
 activeFleetManageTab = target;
 ['wifi','lora'].forEach(p=>{
  const pane=document.getElementById(`fleet-manage-pane-${p}`);
  const btn=document.getElementById(`fleet-manage-tab-${p}`);
  if(pane) pane.classList.toggle('active', p===target);
  if(btn) btn.classList.toggle('active', p===target);
 });
 if(!(activePage==='fleet' && activeFleetTab==='manage')) return;
 if(target!=='lora'){
  stopProvisioningPolling();
 }else{
  startProvisioningPolling();
 }
}
function showPage(page){
 const allowed=['status','fleet','sensors','diagnostics','logs','settings'];
 activePage = allowed.includes(page) ? page : 'status';
 allowed.forEach(p=>{
  const sec=document.getElementById(`page-${p}`);
  const nav=document.getElementById(`nav-${p}`);
  if(sec) sec.classList.toggle('active', p===activePage);
  if(nav) nav.classList.toggle('active', p===activePage);
 });
 if(activePage==='settings'){ showSettingsTab(activeSettingsTab); loadSettingsPageData(false).catch(()=>{}); }
 if(activePage==='status'){
  statusStaticCache = null;
  statusStaticLoadInFlight = false;
  statusLiveHasLiveData = false;
  statusDegradedLiteMode = false;
  statusLiveSseLastMessageMs = 0;
  ensureStatusStatic(true).catch(()=>{});
  refreshStatusLiveNotice();
 }
 if(activePage==='logs'){ refreshLogs(); }
 if(activePage==='diagnostics'){ refreshDiagnostics(); }
 if(activePage==='fleet'){ showFleetTab(activeFleetTab); }
 toggleDrawer(false);
 syncPagePolling();
}
function applyFleetTabVisibility(){
 const fleetTabBtn=document.getElementById('nav-fleet');
 if(fleetTabBtn){ fleetTabBtn.style.display = lastRoleIsTx ? '' : 'none'; }
 const fleetShortcut=document.getElementById('statusFleetShortcut');
 if(fleetShortcut){ fleetShortcut.style.display = lastRoleIsTx ? '' : 'none'; }
 if(!lastRoleIsTx && activePage==='fleet'){ showPage('status'); }
}
function showToast(msg, isError=false){
 const t=document.getElementById('toast');
 if(!t) return;
 t.className=`toast show${isError?' err':''}`;
 t.innerText=msg;
 clearTimeout(window.__toastTimer);
 window.__toastTimer=setTimeout(()=>{ t.className='toast'; }, 2200);
}
function applyTheme(theme){
 currentTheme = (theme === 'light') ? 'light' : 'dark';
 document.body.classList.toggle('light', currentTheme === 'light');
 const btn=document.getElementById('themeBtn');
 if(btn){ btn.innerText = currentTheme === 'dark' ? '☀' : '🌙'; }
 try{ localStorage.setItem('lrs_theme', currentTheme); }catch(e){}
}
function toggleTheme(){ applyTheme(currentTheme === 'dark' ? 'light' : 'dark'); }
function togglePasswordField(id,btn){
 const el=document.getElementById(id);
 if(!el) return;
 const show=el.type==='password';
 el.type=show?'text':'password';
 if(btn){ btn.innerText=show?'Hide':'Show'; }
}
async function ensureStatusStatic(silent){
 if(location.pathname !== '/') return statusStaticCache;
 if(statusStaticCache) return statusStaticCache;
 if(statusStaticLoadInFlight) return null;
 statusStaticLoadInFlight = true;
 const st = await apiJson('/api/status-static',{silent:!!silent});
 if(st && st.ok!==false){
  statusStaticCache = st;
  statusDegradedLiteMode = false;
  const footerFw=document.getElementById('footerFw');
  if(footerFw){
   footerFw.innerText = `FW: ${String(st.fw_display || st.fw_version || '-')}`;
  }
 }else{
  const lite = await apiJson('/api/status-lite',{silent:true});
  if(lite && lite.ok!==false){
   applyStatusLiteDegraded(lite, 'Low-memory mode: limited status');
  }
 }
 statusStaticLoadInFlight = false;
 return statusStaticCache;
}
function buildStatusFallbackFromLite(lite){
 const role = String((lite && lite.role) || (lastRoleIsTx ? 'tx' : 'rx') || 'tx').toLowerCase();
 return {
  role: (role === 'rx') ? 'rx' : 'tx',
  local_address: Number((lite && lite.local_address) || 0),
  remote_address: Number((lite && lite.remote_address) || 0),
  link_state: String((lite && lite.link_state) || 'unknown'),
  relay_state: Number((lite && lite.relay_state) || 0),
  relay_reason: String((lite && lite.relay_reason) || 'unknown'),
  input_state: Number((lite && lite.input_state) || 0),
  local_input_state: Number((lite && lite.local_input_state) || 0),
  lora_last_rssi: Number((lite && lite.lora_last_rssi) || -127),
  lora_last_packet_ms: Number((lite && lite.lora_last_packet_ms) || 0),
  lora_last_tx_ms: Number((lite && lite.lora_last_tx_ms) || 0),
  lora_remote_temp_valid: false,
  lora_remote_temp_c: 0,
  sta_connected: !!(lite && lite.sta_connected),
  sta_ip: String((lite && lite.sta_ip) || ''),
  sta_ssid: String((lite && lite.sta_ssid) || ''),
  sta_target_ssid: String((lite && lite.sta_target_ssid) || ''),
  sta_rssi: Number((lite && lite.sta_rssi) || -127),
  sta_status_code: Number((lite && lite.sta_status_code) || 0),
  sta_status_text: String((lite && lite.sta_status_text) || (((lite && lite.sta_connected) ? 'connected' : 'unknown'))),
  heap_free_bytes: Number((lite && lite.heap_free_bytes) || 0),
  heap_frag_percent: Number((lite && lite.heap_frag_percent) || 0),
  max_free_block_bytes: Number((lite && lite.max_free_block_bytes) || 0),
  uptime_ms: Number((lite && lite.uptime_ms) || 0),
  ap_ssid: String((lite && lite.ap_ssid) || ''),
  ap_ip: String((lite && lite.ap_ip) || ''),
  mdns_ap: String((lite && lite.mdns_ap) || ''),
  mdns_lan: String((lite && lite.mdns_lan) || ''),
  deployment_key: String((lite && lite.deployment_key) || ''),
  sensor_temp_enabled: false,
  sensor_temp_detected: false,
  sensor_temp_valid: false,
  sensor_temp_c: 0,
  sensor_temp_error: 'n/a',
  sensor_temp_addr: '',
  sensor_temp_last_read_ms: 0,
  fw_display: String((lite && lite.fw_display) || (lite && lite.fw_version) || '-')
 };
}
function applyStatusLiteDegraded(lite, noticeText){
 if(!lite || lite.ok===false) return false;
 const fallback = buildStatusFallbackFromLite(lite);
 statusStaticCache = Object.assign({}, statusStaticCache || {}, fallback);
 statusDegradedLiteMode = true;
 applyStatusPageState(statusStaticCache);
 if(noticeText){ setStatusLiveNotice(noticeText); }
 return true;
}
function setStatusLiveNotice(text){
 const el=document.getElementById('statusLiveNotice');
 if(!el) return;
 el.innerText = String(text || '');
}
function refreshStatusLiveNotice(){
 if(location.pathname !== '/' || activePage !== 'status'){
  return;
 }
 if(document.hidden){
  setStatusLiveNotice('Status updates paused while tab is hidden');
  return;
 }
 if((suspendGlobalPollsUntilMs>0 && Date.now() < suspendGlobalPollsUntilMs)){
  setStatusLiveNotice('Status updates paused while device is busy');
  return;
 }
 if(statusDegradedLiteMode && !statusLiveSseConnected){
  setStatusLiveNotice('Low-memory mode: limited status');
  return;
 }
 if(statusLiveSseConnected){
  const age = statusLiveSseLastMessageMs > 0 ? (Date.now() - statusLiveSseLastMessageMs) : 0;
  if(age > 12000){
   setStatusLiveNotice('Waiting for device updates...');
  }else{
   setStatusLiveNotice('Live updates: connected');
  }
  return;
 }
 if(statusLiveHasLiveData){
  setStatusLiveNotice('Live updates disconnected; waiting to reconnect...');
 }else{
  setStatusLiveNotice('Waiting for device updates...');
 }
}
function stopStatusLiveUiTicker(){
 if(statusLiveUiTicker){ clearTimeout(statusLiveUiTicker); statusLiveUiTicker=0; }
}
function startStatusLiveUiTicker(){
 stopStatusLiveUiTicker();
 if(location.pathname !== '/' || activePage !== 'status') return;
 const loop=()=>{
  if(location.pathname !== '/' || activePage !== 'status'){ statusLiveUiTicker=0; return; }
  refreshStatusLiveNotice();
  statusLiveUiTicker=setTimeout(loop, 1000);
 };
 loop();
}
function closeStatusLiveSse(){
 if(statusLiveSseReconnectTimer){ clearTimeout(statusLiveSseReconnectTimer); statusLiveSseReconnectTimer=0; }
 if(statusLiveEventSource){
  try{ statusLiveEventSource.close(); }catch(e){}
  statusLiveEventSource = null;
 }
 statusLiveSseConnected = false;
 statusLiveSseLastMessageMs = 0;
 refreshStatusLiveNotice();
}
function shouldUseStatusLiveSse(){
 if(location.pathname !== '/') return false;
 if(activePage !== 'status') return false;
 if(typeof EventSource === 'undefined') return false;
 if(document.hidden) return false;
 if((suspendGlobalPollsUntilMs>0 && Date.now() < suspendGlobalPollsUntilMs)) return false;
 if(isFleetManageActive() || isProvisioningUiBusy()) return false;
 return true;
}
function scheduleStatusLiveSseReconnect(){
 if(statusLiveSseReconnectTimer || !shouldUseStatusLiveSse()) return;
 const delay = Math.max(1000, Math.min(10000, Number(statusLiveSseBackoffMs || 1000)));
 statusLiveSseReconnectTimer = setTimeout(()=>{
  statusLiveSseReconnectTimer = 0;
  if(shouldUseStatusLiveSse()){ startStatusLiveSse(); }
 }, delay);
 statusLiveSseBackoffMs = Math.min(10000, delay * 2);
}
function handleStatusLivePayload(live){
 if(!live || typeof live !== 'object') return;
 statusFailCount = 0;
 statusDegradedLiteMode = false;
 statusLiveHasLiveData = true;
 statusLiveSseLastMessageMs = Date.now();
 if(!statusStaticCache && !statusStaticLoadInFlight){
  ensureStatusStatic(true).catch(()=>{});
 }
 const st=Object.assign({}, statusStaticCache||{}, live||{});
 applyStatusPageState(st);
 refreshStatusLiveNotice();
}
function startStatusLiveSse(){
 if(!shouldUseStatusLiveSse()) return;
 if(statusLiveEventSource) return;
 try{
  const es = new EventSource('/api/status-live/events');
  statusLiveEventSource = es;
  es.onopen = ()=>{
   statusLiveSseConnected = true;
   statusLiveSseBackoffMs = 1000;
   statusLiveSseLastMessageMs = Date.now();
   refreshStatusLiveNotice();
  };
  const onStatusEvent = (ev)=>{
   try{
    const live = JSON.parse(ev.data);
    handleStatusLivePayload(live);
   }catch(e){}
  };
  es.addEventListener('status', onStatusEvent);
  es.onmessage = onStatusEvent;
  es.onerror = ()=>{
   if(statusLiveEventSource !== es) return;
   try{ es.close(); }catch(e){}
   statusLiveEventSource = null;
   statusLiveSseConnected = false;
   refreshStatusLiveNotice();
   scheduleStatusLiveSseReconnect();
  };
 }catch(e){
  statusLiveSseConnected = false;
  refreshStatusLiveNotice();
  scheduleStatusLiveSseReconnect();
 }
}
function syncStatusLiveSse(){
 if(shouldUseStatusLiveSse()){
  startStatusLiveSse();
  startStatusLiveUiTicker();
 }else{
  closeStatusLiveSse();
  if(activePage !== 'status'){ stopStatusLiveUiTicker(); }
 }
 refreshStatusLiveNotice();
}
function applyStatusPageState(st){
 if(!st) return;
 applyHeaderStatus(st);
 const relayOn = Number(st.relay_state) === 1;
 const hasLora = Number(st.lora_last_packet_ms||0) > 0;
 const hasLoraTx = Number(st.lora_last_tx_ms||0) > 0;
 const loraAgoMs = Number(st.uptime_ms||0) - Number(st.lora_last_packet_ms||0);
 const loraTxAgoMs = Number(st.uptime_ms||0) - Number(st.lora_last_tx_ms||0);
 const roleText=String(st.role||'').toLowerCase();
 if(roleText==='tx' || roleText==='rx'){
  lastRoleIsTx = roleText==='tx';
 }
 applyFleetTabVisibility();
 const roleIsTx = lastRoleIsTx;
 const txAddress = roleIsTx ? st.local_address : st.remote_address;
 const rxAddress = roleIsTx ? st.remote_address : st.local_address;
 const roleDisplay = `${roleIsTx ? 'Transmitter' : 'Receiver'} (tx ${txAddress}, rx ${rxAddress})`;
 const loraRssiText = hasLora ? `${sigIconHtml(st.lora_last_rssi,'lora')}${st.lora_last_rssi} dBm` : 'n/a';
 const loraLastText = hasLora ? `${humanAgeMsShort(loraAgoMs)} ago` : 'no packets yet';
 const loraLastTxText = hasLoraTx ? `${humanAgeMsShort(loraTxAgoMs)} ago` : 'none yet';
 staIsConnected = !!st.sta_connected;
 currentStaIp = String(st.sta_ip || '');
 currentLanMdns = String(st.mdns_lan || '');
 currentApIp = String(st.ap_ip || '');
 currentApMdns = String(st.mdns_ap || '');
 if(staIsConnected){
  const connectedSsid=String(st.sta_ssid || '');
  if(connectedSsid.length){
   connectedStaSsid = connectedSsid;
  }
 }else{
  connectedStaSsid = '';
 }
 updateStaTestButtonState();
 const apUrl = UI_MDNS_ENABLED && st.mdns_ap ? `http://${st.mdns_ap}` : '';
 const lanUrl = UI_MDNS_ENABLED && st.mdns_lan ? `http://${st.mdns_lan}` : '';
 const lanMdnsHtml = lanUrl ? `<a class="link" href="${escapeHtml(lanUrl)}">${escapeHtml(st.mdns_lan)}</a>` : escapeHtml(st.mdns_lan || 'n/a');
 const apMdnsHtml = apUrl ? `<a class="link" href="${escapeHtml(apUrl)}">${escapeHtml(st.mdns_ap)}</a>` : escapeHtml(st.mdns_ap || 'n/a');
 const statusMdnsRows = UI_MDNS_ENABLED
   ? `<div class="k">LAN mDNS</div><div class="v copyable">${copyableValueHtml(lanMdnsHtml, st.mdns_lan, 'LAN mDNS')}</div>`
   : '';
 const apMdnsRow = UI_MDNS_ENABLED
   ? `<div class="k">AP mDNS</div><div class="v copyable">${copyableValueHtml(apMdnsHtml, st.mdns_ap, 'AP mDNS')}</div>`
   : '';
 const rb=document.getElementById('relayBadge');
 if(rb){
  rb.className = `relay-badge ${relayOn ? 'on' : 'off'}`;
  rb.innerText = relayOn ? 'RELAY ON' : 'RELAY OFF';
 }
 const rm=document.getElementById('relayMeta');
 if(rm){ rm.innerText = `Link: ${st.link_state}`; }
 const table=document.getElementById('statusTable');
 const deployKey=String(st.deployment_key || '');
 const deployKeyCopyBtn = copyButtonHtml(deployKey, 'Fleet key');
 const footerFw=document.getElementById('footerFw');
 if(footerFw){
  footerFw.innerText = `FW: ${String(st.fw_display || st.fw_version || '-')}`;
 }
 if(table){
 table.className='status-table';
  table.innerHTML=
   `<div class="section">LoRa</div>
    <div class="k">Role</div><div class="v copyable">${copyableValueHtml(escapeHtml(roleDisplay), roleDisplay, 'Role')}</div>
    <div class="k">Fleet key</div><div class="v copyable">${copyableValueHtml(escapeHtml(deployKey || 'not_set'), deployKey, 'Fleet key')}</div>
    <div class="k">Link</div><div class="v">${escapeHtml(st.link_state)}</div>
    <div class="k">LoRa RSSI</div><div class="v">${loraRssiText}</div>
    <div class="k">Last LoRa TX</div><div class="v">${escapeHtml(loraLastTxText)}</div>
    <div class="k">Last LoRa packet</div><div class="v">${escapeHtml(loraLastText)}</div>
    <div class="k">Relay reason</div><div class="v">${escapeHtml(reasonLabel(st.relay_reason))}</div>
    <div class="section">WiFi Station</div>
    <div class="k">STA SSID</div><div class="v">${escapeHtml(st.sta_ssid || st.sta_target_ssid || 'not configured')}</div>
    <div class="k">STA State</div><div class="v">${escapeHtml(st.sta_status_text)} [${escapeHtml(st.sta_status_code)}]</div>
    <div class="k">Current RSSI</div><div class="v">${escapeHtml(st.sta_connected ? `${st.sta_rssi} dBm` : 'n/a')}</div>
    <div class="k">STA IP</div><div class="v copyable">${copyableValueHtml(escapeHtml(st.sta_ip || 'n/a'), st.sta_ip, 'STA IP')}</div>
    ${statusMdnsRows}
    <div class="section">Soft AP</div>
    <div class="k">AP SSID</div><div class="v">${escapeHtml(st.ap_ssid)}</div>
    <div class="k">AP IP</div><div class="v copyable">${copyableValueHtml(escapeHtml(st.ap_ip || 'n/a'), st.ap_ip, 'AP IP')}</div>
    ${apMdnsRow}`;
 }
 const t=document.getElementById('sensorTempTile');
 if(t){
  if(st.sensor_temp_detected && st.sensor_temp_valid){
    t.innerText=`Temperature: ${Number(st.sensor_temp_c).toFixed(1)} C`;
  }else if(st.sensor_temp_enabled){
    t.innerText=`Temperature: ${st.sensor_temp_error || 'n/a'}`;
  }else{
    t.innerText='Temperature: disabled';
  }
 }
 const rt=document.getElementById('sensorRemoteTempTile');
 if(rt){
  if(st.lora_remote_temp_valid){
    rt.innerText=`Remote LoRa temp: ${Number(st.lora_remote_temp_c).toFixed(1)} C`;
  }else{
    rt.innerText='Remote LoRa temp: n/a';
  }
 }
 const inTile=document.getElementById('sensorInputTile');
 if(inTile){
  const closed = Number(st.local_input_state) === 1;
  inTile.innerHTML=`Dry contact input: <span class="sensor-state ${closed?'closed':'open'}">${closed?'CLOSED':'OPEN'}</span>`;
 }
 const sdState=document.getElementById('sensorDiagState');
 const sdTemp=document.getElementById('sensorDiagTemp');
 const sdAddr=document.getElementById('sensorDiagAddr');
 const sdLast=document.getElementById('sensorDiagLast');
 if(sdState && sdTemp && sdAddr && sdLast){
  const enabled = !!st.sensor_temp_enabled;
  const detected = !!st.sensor_temp_detected;
  const valid = !!st.sensor_temp_valid;
  if(!enabled){
    sdState.innerText='DS18B20: disabled';
    sdTemp.innerText='Temperature: n/a';
    sdAddr.innerText='Address: n/a';
    sdLast.innerText='Last read: n/a';
  } else {
    sdState.innerText = detected ? 'DS18B20: detected' : `DS18B20: not detected (${st.sensor_temp_error || 'unknown'})`;
    sdTemp.innerText = valid ? `Temperature: ${Number(st.sensor_temp_c).toFixed(1)} C` : `Temperature: ${st.sensor_temp_error || 'n/a'}`;
    sdAddr.innerText = `Address: ${st.sensor_temp_addr || 'n/a'}`;
    const ageMs = Number(st.uptime_ms || 0) - Number(st.sensor_temp_last_read_ms || 0);
    sdLast.innerText = Number(st.sensor_temp_last_read_ms || 0) > 0 ? `Last read: ${humanAgeMs(ageMs)}` : 'Last read: n/a';
  }
 }
}
async function refreshStatus(force){
 if(location.pathname !== '/') return;
 if(statusRefreshInFlight) return;
 if((suspendGlobalPollsUntilMs>0 && Date.now() < suspendGlobalPollsUntilMs) || isFleetManageActive() || isProvisioningUiBusy()) return;
 if(!force && statusLiveSseConnected && statusLiveSseLastMessageMs > 0 && (Date.now() - statusLiveSseLastMessageMs) < 5000){
  return;
 }
 statusRefreshInFlight = true;
 const live=await apiJson('/api/status-live',{silent:true});
 if(!live){
  const lite=await apiJson('/api/status-lite',{silent:true});
  if(applyStatusLiteDegraded(lite, 'Low-memory mode: live status unavailable')){
   statusFailCount = 0;
   refreshStatusLiveNotice();
   statusRefreshInFlight = false;
   return;
  }
  statusFailCount++;
  if(statusFailCount >= 3){
   const s=document.getElementById('statusTable');
   if(s){ s.innerText='API status temporarily unavailable'; }
  }
  refreshStatusLiveNotice();
  statusRefreshInFlight = false;
  return;
 }
 handleStatusLivePayload(live);
 statusRefreshInFlight = false;
}
function applyHeaderStatus(st){
 if(!st) return;
 const heapEl=document.getElementById('heapHeader');
 const roleTag = String(st.role || '').toLowerCase() === 'tx' ? 'TX' : 'RX';
 const titleEl = document.getElementById('consoleTitle');
 const pageTitle = `LRS Device Console (${roleTag})`;
 if(titleEl){ titleEl.innerText = pageTitle; }
 document.title = pageTitle;
 const roleText=String(st.role||'').toLowerCase();
 if(roleText==='tx' || roleText==='rx'){
  lastRoleIsTx = roleText==='tx';
  applyFleetTabVisibility();
 }
 setWifiBadge(st.sta_connected ? rssiToLevel(st.sta_rssi) : 0, 'WiFi');
 const hasLora = Number(st.lora_last_packet_ms||0) > 0;
 setLoraBadge(hasLora ? rssiToLevel(st.lora_last_rssi) : 0, 'LoRa');
 const relayOn = Number(st.relay_state) === 1;
 const rh=document.getElementById('relayHeader');
 if(rh){
  rh.className=`relay-head ${relayOn ? 'on' : 'off'}`;
  rh.innerText=relayOn ? 'Relay: ON' : 'Relay: OFF';
 }
 if(heapEl){
 const heapBytes=Number(st.heap_free_bytes||0);
  const maxBlockBytes=Number(st.max_free_block_bytes||0);
  const heapFrag=Number(st.heap_frag_percent||0);
  const heapK = heapBytes>0 ? (heapBytes/1024) : 0;
  const maxK = maxBlockBytes>0 ? (maxBlockBytes/1024) : 0;
  const heapTxt = heapBytes>0 ? (heapK>=10 ? String(Math.round(heapK)) : heapK.toFixed(1)) : '-';
  const maxTxt = maxBlockBytes>0 ? (maxK>=10 ? String(Math.round(maxK)) : maxK.toFixed(1)) : '-';
  heapEl.innerText = (heapBytes>0 && maxBlockBytes>0) ? `Mem ${heapTxt}/${maxTxt}` : 'Mem -/-';
  let memClass='off';
  if(heapBytes>0 && maxBlockBytes>0){
   const crit = (maxBlockBytes < 1200) || (heapBytes < 3000 && maxBlockBytes < 1600);
   const warn = !crit && ((maxBlockBytes < 1800) || (heapBytes < 4000));
   memClass = crit ? 'mem-crit' : (warn ? 'mem-warn' : 'mem-ok');
  }
  heapEl.className = `relay-head ${memClass}`;
  if(heapBytes>0 || maxBlockBytes>0){
   heapEl.title = `Free heap: ${heapBytes} B | Max block: ${maxBlockBytes} B | Frag: ${heapFrag}%`;
  }else{
   heapEl.title = 'Memory metrics unavailable';
  }
 }
}
async function refreshHeaderStatus(){
 if(location.pathname !== '/') return;
 if(activePage==='status') return;
 if((suspendGlobalPollsUntilMs>0 && Date.now() < suspendGlobalPollsUntilMs)) return;
 if(headerStatusRefreshInFlight) return;
 headerStatusRefreshInFlight = true;
 const st=await apiJson('/api/status-lite',{silent:true});
 if(st && st.ok!==false){ applyHeaderStatus(st); }
 headerStatusRefreshInFlight = false;
}
function stopAllUiPollingForAuthExpiry(){
 suspendGlobalPollsUntilMs = Date.now() + 60000;
 try{ stopProvisioningPolling(); }catch(e){}
 try{ stopPagePolling(); }catch(e){}
 try{ stopHeaderPolling(); }catch(e){}
 try{ stopStatusLiveUiTicker(); }catch(e){}
 try{ closeStatusLiveSse(); }catch(e){}
 try{ provStatusInFlight = false; }catch(e){}
 try{ statusRefreshInFlight = false; }catch(e){}
 try{ headerStatusRefreshInFlight = false; }catch(e){}
 try{ fleetRefreshInFlight = false; }catch(e){}
}
async function apiJson(url, options){
 let t=null;
 try{
  const merged=Object.assign({cache:'no-store',silent:false,timeoutMs:8000}, options||{});
  const controller = new AbortController();
  const timeoutMs = Number(merged.timeoutMs || 8000);
  t = setTimeout(()=>controller.abort(), timeoutMs);
  delete merged.timeoutMs;
  merged.signal = controller.signal;
  const res=await fetch(url,merged);
  if(res.status===401){
   stopAllUiPollingForAuthExpiry();
   location.href='/login?expired=1';
   return null;
  }
  if(!res.ok){ throw new Error(`HTTP ${res.status}`); }
  return await res.json();
 }catch(e){
  if(!options || !options.silent){
   const st=document.getElementById('statusTable');
   if(st){ st.innerText=`API error: ${e.message}`; }
  }
  return null;
 }finally{
  if(t){ clearTimeout(t); }
 }
}
async function logout(){
 try{ await fetch('/api/logout',{method:'POST'}); }catch(e){}
 location.href='/login?logged_out=1';
}
async function load(){
  if(activePage==='status'){
   await ensureStatusStatic(true);
   refreshStatusLiveNotice();
  }
}
async function loadSettingsPageData(force){
 if(settingsPageLoadInFlight) return;
 if(settingsPageLoaded && !force) return;
 settingsPageLoadInFlight = true;
 try{
  const s=await apiJson('/api/settings',{silent:true,timeoutMs:6000});
  if(!s) return;
  lastRoleIsTx = !!s.role_tx;
  applyFleetTabVisibility();
  Object.keys(s).forEach(k=>{
   const el=document.getElementById(k);
   if(!el) return;
   if(el.type==='checkbox'){ el.checked=!!s[k]; return; }
   el.value=String(s[k]);
  });
  updateDeploymentKeyStrength();
  updateStaTestButtonState();
  document.getElementById('role_tx').value = String(!!s.role_tx);
  document.getElementById('role_tx_true').checked = !!s.role_tx;
  document.getElementById('role_tx_false').checked = !s.role_tx;
  document.getElementById('ap_always_on').checked = !!s.ap_always_on;
  document.getElementById('mqtt_enabled').checked = !!s.mqtt_enabled;
  document.getElementById('sensor_temp_enabled').checked = !!s.sensor_temp_enabled;
  document.getElementById('lora_frequency_mhz').value=(Number(s.lora_frequency_hz)/1000000).toFixed(3);
  refreshFreqPreset();
  document.getElementById('heartbeat_s').value=Math.max(1,Math.round(Number(s.heartbeat_ms)/1000));
  document.getElementById('ack_timeout_s').value=Math.max(1,Math.round(Number(s.ack_timeout_ms)/1000));
  document.getElementById('mqtt_remote_retry_timeout_s').value=Math.max(1,Math.round(Number(s.mqtt_remote_retry_timeout_ms||300000)/1000));
  document.getElementById('tx_mqtt_remote_polling_enabled').checked = !!s.tx_mqtt_remote_polling_enabled;
  document.getElementById('tx_mqtt_remote_default_poll_interval_s').value=Math.max(60,Math.round(Number(s.tx_mqtt_remote_default_poll_interval_ms||60000)/1000));
  document.getElementById('rx_push_on_change_enabled').checked = !!s.rx_push_on_change_enabled;
  document.getElementById('rx_push_min_interval_s').value=Math.max(60,Math.round(Number(s.rx_push_min_interval_ms||60000)/1000));
  document.getElementById('local_address').value=String(s.local_address);
  document.getElementById('remote_address').value=String(s.remote_address);
  refreshRoleLabels();
  refreshAddressHints();
  refreshHostnamePreview();
  const f=await apiJson('/api/factory',{silent:true,timeoutMs:6000});
  if(f){ document.getElementById('factory').innerText=JSON.stringify(f,null,2); }
  settingsPageLoaded = true;
 } finally {
  settingsPageLoadInFlight = false;
 }
}
async function refreshLogs(){
 const lv=document.getElementById('logView');
 if(!lv) return;
 try{
  const res=await fetch('/api/logs.txt',{cache:'no-store'});
  if(res.status===401){ location.href='/login?expired=1'; return; }
  if(!res.ok){ throw new Error(`HTTP ${res.status}`); }
  lv.innerText=await res.text();
  lv.scrollTop=lv.scrollHeight;
 }catch(e){
  lv.innerText=`Log fetch failed: ${e.message}`;
 }
}
async function scanWifi(){
 const host=document.getElementById('wifi_scan_list');
 host.innerHTML='Scanning...';
 try{
  const out=await apiJson('/api/wifi/scan');
  if(!out){ host.innerHTML='Scan failed'; return; }
  if(!out.networks || !out.networks.length){
    host.innerHTML='No SSIDs found';
    return;
  }
  out.networks.sort((a,b)=>Number(b.rssi)-Number(a.rssi));
  host.innerHTML='<table class="wifi-table"><thead><tr><th>SSID</th><th>Signal</th><th>Secure</th><th></th></tr></thead><tbody></tbody></table>';
  const tbody=host.querySelector('tbody');
  out.networks.forEach(n=>{
    const tr=document.createElement('tr');
    tr.innerHTML=`<td>${escapeHtml(n.ssid)}</td><td>${sigIconHtml(n.rssi,'scan')}${escapeHtml(n.rssi)} dBm</td><td><span class="sec-chip ${n.secure?'y':'n'}">${n.secure?'Y':'N'}</span></td><td><button type="button" data-ssid="${escapeHtml(n.ssid)}">Use</button></td>`;
    tbody.appendChild(tr);
  });
  host.querySelectorAll('button[data-ssid]').forEach(btn=>{
    btn.addEventListener('click',()=>{
      document.getElementById('wifi_sta_ssid').value=btn.getAttribute('data-ssid');
      updateStaTestButtonState();
    });
  });
 }catch(e){
  host.innerHTML='Scan failed';
 }
}
async function save(){
 await saveAll();
}
async function postSettings(body, options){
 const opts=Object.assign({skipReload:false}, options||{});
 const btns=[...document.querySelectorAll('button')];
 btns.forEach(b=>b.disabled=true);
 showToast('Saving...');
 let t=null;
 try{
  const controller = new AbortController();
  t = setTimeout(()=>controller.abort(), 10000);
  const res=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),signal:controller.signal});
  if(res.status===401){ location.href='/login?expired=1'; return false; }
  const text=await res.text();
  if(!res.ok){
   showToast(`Save failed: ${text}`, true);
   return false;
  }
  showToast('Saved');
  if(!opts.skipReload){
   setTimeout(()=>{ load().catch(()=>{}); }, 150);
  }
  return true;
 }catch(e){
  showToast(`Save failed: ${e.message}`, true);
  return false;
 }finally{
  if(t){ clearTimeout(t); }
  btns.forEach(b=>b.disabled=false);
 }
}
function collectLoraBody(){
 const local=parseAddress(document.getElementById('local_address').value);
 const remote=parseAddress(document.getElementById('remote_address').value);
 if(!Number.isInteger(local)||local<1||local>254){alert('Local address must be 1..254 (decimal or 0xHEX).'); return;}
 if(!Number.isInteger(remote)||remote<1||remote>254){alert('Remote address must be 1..254 (decimal or 0xHEX).'); return;}
 const mhz=Number(document.getElementById('lora_frequency_mhz').value);
 if(!Number.isFinite(mhz)||mhz<FREQ_MIN_MHZ||mhz>FREQ_MAX_MHZ){alert(`Frequency must be between ${FREQ_MIN_MHZ} and ${FREQ_MAX_MHZ} MHz.`); return;}
 const hbSec=Math.floor(Number(document.getElementById('heartbeat_s').value));
 const ackSec=Math.floor(Number(document.getElementById('ack_timeout_s').value));
 const mqttRetrySec=Math.floor(Number(document.getElementById('mqtt_remote_retry_timeout_s').value));
 const txPollDefaultSec=Math.floor(Number(document.getElementById('tx_mqtt_remote_default_poll_interval_s').value));
 const rxPushMinSec=Math.floor(Number(document.getElementById('rx_push_min_interval_s').value));
 if(!Number.isFinite(hbSec)||hbSec<60||hbSec>3600){alert('Heartbeat must be between 60 and 3600 seconds.'); return;}
 if(!Number.isFinite(ackSec)||ackSec<5||ackSec>600){alert('ACK timeout must be between 5 and 600 seconds.'); return;}
 if(!Number.isFinite(mqttRetrySec)||mqttRetrySec<5||mqttRetrySec>3600){alert('MQTT remote retry timeout must be between 5 and 3600 seconds.'); return;}
 if(!Number.isFinite(txPollDefaultSec)||txPollDefaultSec<60||txPollDefaultSec>3600){alert('Default poll interval must be between 60 and 3600 seconds.'); return;}
 if(!Number.isFinite(rxPushMinSec)||rxPushMinSec<60||rxPushMinSec>3600){alert('RX push minimum interval must be between 60 and 3600 seconds.'); return;}
 const ids=['lora_tx_power','lora_spreading_factor','lora_bandwidth_hz','lora_coding_rate','fleet_passphrase'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 body.fleet_passphrase=String(body.fleet_passphrase||'').trim();
 if(body.fleet_passphrase.length < MIN_DEPLOYMENT_KEY_LEN){
   alert(`Deployment Key must be at least ${MIN_DEPLOYMENT_KEY_LEN} characters.`);
   return;
 }
 if(isDefaultDeploymentKey(body.fleet_passphrase)){
   alert('Deployment Key cannot be the default value. Please set a unique installation key.');
   return;
 }
 body.role_tx=document.getElementById('role_tx_true').checked;
 body.local_address=local;
 body.remote_address=remote;
 body.tx_input_lora_control_enabled=document.getElementById('tx_input_lora_control_enabled').checked;
 body.tx_mqtt_remote_polling_enabled=document.getElementById('tx_mqtt_remote_polling_enabled').checked;
 body.rx_push_on_change_enabled=document.getElementById('rx_push_on_change_enabled').checked;
 body.lora_frequency_hz=Math.round(mhz*1000000);
 body.heartbeat_ms=hbSec*1000;
 body.ack_timeout_ms=ackSec*1000;
 body.mqtt_remote_retry_timeout_ms=mqttRetrySec*1000;
 body.tx_mqtt_remote_default_poll_interval_ms=txPollDefaultSec*1000;
 body.rx_push_min_interval_ms=rxPushMinSec*1000;
 return body;
}
function collectNetworkBody(){
 const ids=['wifi_sta_ssid','wifi_sta_password','lan_hostname'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 body.ap_always_on=document.getElementById('ap_always_on').checked;
 return body;
}
function collectSystemBody(){
 return {admin_password: document.getElementById('admin_password').value};
}
function collectMqttBody(){
 const ids=['mqtt_host','mqtt_user','mqtt_password','mqtt_topic_root'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 const portRaw=String(document.getElementById('mqtt_port').value||'').trim();
 const port=Number(portRaw);
 if(portRaw.length===0 || !Number.isFinite(port) || port<1 || port>65535){
  alert('MQTT port must be in range 1..65535.');
  return null;
 }
 body.mqtt_port=Math.floor(port);
 body.mqtt_enabled=document.getElementById('mqtt_enabled').checked;
 return body;
}
function collectSensorsBody(){
 const ids=[];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 body.sensor_temp_enabled=document.getElementById('sensor_temp_enabled').checked;
 return body;
}
async function saveLora(){
 const body=collectLoraBody();
 if(!body) return;
 await postSettings(body);
}
async function saveNetwork(){
 const body=collectNetworkBody();
 let shouldRedirect=false;
 let newHost='';
 if(UI_MDNS_ENABLED){
  const oldHost=normalizeLanHost(currentLanMdns);
  newHost=normalizeLanHost(body.lan_hostname);
  const hostChanged=oldHost.length>0 && newHost.length>0 && oldHost!==newHost;
  shouldRedirect=hostChanged && isLikelyStaSessionPath();
 }
 const ok=await postSettings(body, shouldRedirect ? {skipReload:true} : undefined);
 if(!ok) return;
 if(shouldRedirect){
  startLanHostnameRedirect(newHost);
 }
}
async function saveSystem(){
 await postSettings(collectSystemBody());
}
async function saveMqtt(){
 const body=collectMqttBody();
 if(!body) return;
 await postSettings(body);
}
async function saveSensors(){
 await postSettings(collectSensorsBody());
}
async function saveAll(){
 const lora=collectLoraBody();
 if(!lora) return;
 const mqtt=collectMqttBody();
 if(!mqtt) return;
 await postSettings(Object.assign({}, lora, collectNetworkBody(), mqtt, collectSensorsBody(), collectSystemBody()));
}
async function reboot(){await fetch('/api/reboot',{method:'POST'});}
async function factoryResetLocal(){
 const el=document.getElementById('factoryResetResult');
 const pwEl=document.getElementById('factory_reset_password');
 const keepEl=document.getElementById('factory_reset_keep_fleet_local');
 const keepWifiEl=document.getElementById('factory_reset_keep_wifi_local');
 if(!el || !pwEl || !keepEl || !keepWifiEl) return;
 const password=String(pwEl.value||'');
 const keepFleet=!!keepEl.checked;
 const keepWifi=!!keepWifiEl.checked;
 if(!password.length){
  el.className='result-line show err';
  el.innerText='Enter admin password to factory reset.';
  return;
 }
 if(!keepFleet){
  alert('Warning: this will remove the device from the current LoRa fleet and require manual provisioning again.');
  const confirmWord=prompt("Type REMOVE to confirm removing the shared fleet key:");
  if(String(confirmWord||'').trim()!=='REMOVE'){
   el.className='result-line show err';
   el.innerText='Factory reset cancelled (confirmation word not entered).';
   return;
  }
 }
 const keepBits=[];
 if(keepFleet) keepBits.push('shared fleet key');
 if(keepWifi) keepBits.push('WiFi credentials');
 const keepMsg=keepBits.length ? ` (keep ${keepBits.join(' + ')})` : '';
 if(!confirm(`Factory reset this device${keepMsg}? It will reboot.`)){
  return;
 }
 el.className='result-line show';
 el.innerText='Factory reset requested...';
 const out=await apiJson('/api/system/factory-reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({admin_password:password,keep_shared_fleet_key:keepFleet,keep_wifi_credentials:keepWifi}),silent:true});
 if(out && out.ok){
  el.className='result-line show ok';
  el.innerText='Factory reset started. Device is rebooting...';
  pwEl.value='';
  return;
 }
 const err=(out && out.error) ? out.error : 'request_failed';
 el.className='result-line show err';
 el.innerText=`Factory reset failed: ${err}`;
}
async function testSta(){
 const btn=document.getElementById('btnTestSta');
 const el=document.getElementById('netTestResult');
 if(!el) return;
 if(btn && btn.disabled) return;
 const requestedSsid=normalizedInputValue('wifi_sta_ssid');
 if(staIsConnected && requestedSsid.length && requestedSsid!==connectedStaSsid && isLikelyStaSessionPath()){
  const apHint=currentApIp ? `http://${currentApIp}` : (UI_MDNS_ENABLED && currentApMdns ? `http://${currentApMdns}` : 'the Soft AP URL');
  const msg=`Cannot test a different SSID from current LAN session (it drops this connection). Join device Soft AP and retry via ${apHint}.`;
  el.className='result-line show err';
  el.innerText=msg;
  showToast('Use Soft AP for cross-SSID test', true);
  return;
 }
 staTestInFlight = true;
 updateStaTestButtonState();
 if(btn){ btn.innerText='Testing...'; }
 el.className='result-line show';
 el.innerText='Testing STA connection...';
 showToast('Testing STA connection...');
 const out=await apiJson('/api/network/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collectNetworkBody())});
 staTestInFlight = false;
 if(btn){ btn.innerText='Test'; }
 updateStaTestButtonState();
 if(!out){
  const st=await apiJson('/api/status-live',{silent:true,timeoutMs:5000});
  if(st && st.sta_connected && String(st.sta_ssid || '').trim()===requestedSsid){
   const msg=`STA connect OK${st.sta_rssi?` (${st.sta_rssi} dBm)`:''}`;
   el.className='result-line show ok';
   el.innerText=msg;
   showToast(msg);
   return;
  }
  el.className='result-line show err';
  el.innerText='STA test failed';
  showToast('STA test failed', true);
  return;
 }
 if(out.ok){
  const msg=`STA connect OK${out.rssi?` (${out.rssi} dBm)`:''}`;
  el.className='result-line show ok';
  el.innerText=msg;
  showToast(msg);
 }else{
  const msg=`STA connect failed: ${out.status_text} [${out.status_code}]`;
  el.className='result-line show err';
  el.innerText=msg;
  showToast(msg, true);
 }
}
let provStatusPollTimer=0;
let provLastRowsHtml='';
let provPollSeenActive=false;
let provPollGraceUntilMs=0;
let suspendGlobalPollsUntilMs=0;
function stopProvisioningPolling(){
 if(provStatusPollTimer){ clearTimeout(provStatusPollTimer); provStatusPollTimer=0; }
}
function provisioningSessionStateLabel(s){
 const key=String(s||'idle');
 if(key==='provisioning') return 'devices';
 return key.split('_').join(' ');
}
function renderProvisioningStatus(out){
 const result=document.getElementById('provWizardResult');
 const summary=document.getElementById('provWizardSummary');
 const rows=document.getElementById('provWizardRows');
 const provisionBtn=document.getElementById('provProvisionAllBtn');
 if(!summary || !rows) return;
 const sess=(out&&out.session)||{};
 const devices=Array.isArray(out&&out.devices)?out.devices:[];
 provUiSessionActive = !!sess.active;
 provUiSessionState = String(sess.state||'idle');
 const compactMode=!!(sess && (sess.compact || sess.devices_truncated));
 if(provisionBtn){
  const st=String(sess.state||'idle');
  const discovered = Number(sess.discovered_count||0);
  provisionBtn.disabled = !(st==='ready' && discovered>0);
 }
 const now=Number(sess.now_ms||0);
 let countdownTxt='';
 if(sess.active && Number(sess.phase_deadline_ms||0)>0 && now>0){
  const rem=Math.max(0, Math.ceil((Number(sess.phase_deadline_ms)-now)/1000));
  if(rem>0 && (sess.state==='discovering' || sess.state==='discovery_retry' || sess.state==='provisioning')) countdownTxt=` · next phase in ~${rem}s`;
 }
 summary.innerText = sess.active
  ? `State: ${provisioningSessionStateLabel(sess.state)} · found ${Number(sess.discovered_count||0)} · conflicts ${Number(sess.conflict_count||0)} · verified ${Number(sess.verified_count||0)} · failed ${Number(sess.failed_count||0)}${countdownTxt}`
  : 'No provisioning session active.';
 if(result && sess.active){
  result.className='result-line show';
  if(sess.state==='complete') result.className='result-line show ok';
  if(sess.state==='error') result.className='result-line show err';
  result.innerText=`Provisioning ${provisioningSessionStateLabel(sess.state)}`;
 }
 if(!devices.length){
  if(compactMode && provLastRowsHtml){
   rows.innerHTML=provLastRowsHtml;
   return;
  }
  rows.innerHTML=`<tr><td colspan="6" class="small">${compactMode ? 'Low-memory mode: showing counts only (keeping rows when available).' : 'No devices discovered yet.'}</td></tr>`;
  return;
 }
 rows.innerHTML = devices.map((d)=>{
  const cur = Number(d.current_address||0);
  const nxt = Number(d.assigned_address||0);
  const fw = d.fw_version || `${d.fw_major||0}.${d.fw_minor||0}.${d.fw_patch||0}`;
  const conflict = d.address_conflict ? ' conflict' : '';
  return `<tr><td>${escapeHtml(String(d.chip_id_hex||d.chip_id||''))}</td><td>${cur||'-'}</td><td>${nxt||'-'}</td><td>${escapeHtml(String(fw))}</td><td>${Number(d.rssi||0)}</td><td>${escapeHtml(String(d.state||'unknown'))}${conflict}</td></tr>`;
 }).join('');
 provLastRowsHtml = rows.innerHTML;
}
async function refreshProvisioningStatus(silent=true){
 if(!(activePage==='fleet' && activeFleetTab==='manage' && activeFleetManageTab==='lora')) return null;
 if(provStatusInFlight) return null;
 provStatusInFlight = true;
 provLastStatusRefreshMs = Date.now();
 const out=await apiJson('/api/provisioning/status',{silent});
 if(out && out.ok){ renderProvisioningStatus(out); provStatusInFlight = false; return out; }
 provStatusInFlight = false;
 return null;
}
function startProvisioningPolling(opts){
 if(!(activePage==='fleet' && activeFleetTab==='manage' && activeFleetManageTab==='lora')) return;
 stopProvisioningPolling();
 const graceMs = Math.max(0, Number(opts && opts.graceMs || 0) || 0);
 if(graceMs>0){
  provPollSeenActive = false;
  provPollGraceUntilMs = Date.now() + graceMs;
 }else{
  provPollGraceUntilMs = 0;
 }
 const loop=async()=>{
  const out=await refreshProvisioningStatus(true);
  if(!out || !out.ok){
   provStatusPollTimer=setTimeout(loop, 1500);
   return;
  }
  const sess=(out&&out.session)||{};
  const st=String(sess.state||'idle');
  let nextDelayMs = 1500;
  if(sess.active){ provPollSeenActive = true; }
  const withinGrace = (provPollGraceUntilMs>0 && Date.now() < provPollGraceUntilMs && !provPollSeenActive);
  const terminal = (!withinGrace) && ((!sess.active) || st==='complete' || st==='error' || st==='idle');
  if(terminal){
   stopProvisioningPolling();
   provPollGraceUntilMs = 0;
   return;
  }
  // When discovery is complete and we're waiting on the user to click "Provision All",
  // back off polling to reduce heap pressure on ESP8266.
  if(st==='ready') nextDelayMs = 4000;
  provStatusPollTimer=setTimeout(loop, nextDelayMs);
 };
 loop();
}
let pagePollTimer=0;
let pagePollGeneration=0;
let headerPollTimer=0;
let headerPollGeneration=0;
function stopPagePolling(){
 if(pagePollTimer){ clearTimeout(pagePollTimer); pagePollTimer=0; }
 pagePollGeneration++;
}
function stopHeaderPolling(){
 if(headerPollTimer){ clearTimeout(headerPollTimer); headerPollTimer=0; }
 headerPollGeneration++;
}
function pagePollDelayMs(){
 if(document.hidden) return 5000;
 if(activePage==='fleet' && activeFleetTab==='devices') return 2000;
 if(activePage==='logs') return 3000;
 return 1500;
}
function headerPollDelayMs(){
 if(document.hidden) return 8000;
 if(activePage==='fleet' && activeFleetTab==='manage' && activeFleetManageTab==='lora') return 5000;
 return 3000;
}
function scheduleHeaderPolling(){
 stopHeaderPolling();
 if(location.pathname !== '/') return;
 if(activePage==='status') return;  // Status page updates via /api/status-live/events (SSE)
 const generation = headerPollGeneration;
 const loop=async()=>{
  if(generation !== headerPollGeneration) return;
  await refreshHeaderStatus();
  if(generation !== headerPollGeneration) return;
  headerPollTimer=setTimeout(loop, headerPollDelayMs());
 };
 loop();
}
function schedulePagePoll(){
 stopPagePolling();
 const generation = pagePollGeneration;
 let fn=null;
 let delayFn=pagePollDelayMs;
 if(activePage==='fleet' && lastRoleIsTx && activeFleetTab==='devices'){
  fn=refreshFleet;
 }else if(activePage==='logs'){
  fn=refreshLogs;
 }else{
  return;
 }
 const loop=async()=>{
  if(generation !== pagePollGeneration) return;
  if(document.hidden && activePage!=='logs'){
   if(generation !== pagePollGeneration) return;
   pagePollTimer=setTimeout(loop, delayFn());
   return;
  }
  await fn();
  if(generation !== pagePollGeneration) return;
  pagePollTimer=setTimeout(loop, delayFn());
 };
 loop();
}
function syncPagePolling(){
 syncStatusLiveSse();
 if(activePage==='fleet' && activeFleetTab==='manage'){
  stopPagePolling();
 if(activeFleetManageTab==='lora'){
   startProvisioningPolling();
  }else{
   stopProvisioningPolling();
  }
  scheduleHeaderPolling();
  return;
 }
 stopProvisioningPolling();
 schedulePagePoll();
 scheduleHeaderPolling();
}
async function startFleetProvisioningDiscovery(){
 return startFleetProvisioningDiscoveryWithMode(false);
}
async function searchMoreFleetProvisioning(){
 return startFleetProvisioningDiscoveryWithMode(true);
}
async function startFleetProvisioningDiscoveryWithMode(searchMore){
 const result=document.getElementById('provWizardResult');
 const estEl=document.getElementById('prov_estimated_count');
 const est=Math.max(1, Math.min(250, Number(estEl && estEl.value || 10) || 10));
 const retry=false;
 suspendGlobalPollsUntilMs = Date.now() + 5000;
 if(result){ result.className='result-line show'; result.innerText = searchMore ? 'Searching for more devices...' : 'Starting discovery...'; }
 const out=await apiJson('/api/provisioning/start',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({estimated_count:est,retry_once:retry}),silent:true});
 if(out && out.ok){
  if(out.session){ renderProvisioningStatus(out); }
  startProvisioningPolling({graceMs:5000});
  if(result){ result.className='result-line show ok'; result.innerText = searchMore ? 'Search started. Watching live updates...' : 'Discovery started. Watching live updates...'; }
  return;
 }
 if(result){ result.className='result-line show err'; result.innerText=`${searchMore ? 'Search' : 'Discovery'} start failed: ${(out&&out.error)||'request_failed'}`; }
}
async function provisionFleetAll(){
 const result=document.getElementById('provWizardResult');
 suspendGlobalPollsUntilMs = Date.now() + 5000;
 if(result){ result.className='result-line show'; result.innerText='Provisioning discovered devices...'; }
 const out=await apiJson('/api/provisioning/provision-all',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',silent:true});
 if(out && out.ok){
  if(out.session){ renderProvisioningStatus(out); }
  startProvisioningPolling({graceMs:5000});
  if(result){ result.className='result-line show ok'; result.innerText='Provisioning started. Waiting for verify replies...'; }
  return;
 }
 if(result){ result.className='result-line show err'; result.innerText=`Provisioning failed to start: ${(out&&out.error)||'request_failed'}`; }
}
async function cancelFleetProvisioning(){
 stopProvisioningPolling();
 await apiJson('/api/provisioning/cancel',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',silent:true});
 const out=await refreshProvisioningStatus(true);
 const result=document.getElementById('provWizardResult');
 if(result){ result.className='result-line show'; result.innerText = out && out.session && out.session.active ? 'Provisioning session updated.' : 'Provisioning session cancelled.'; }
}
async function provisionFleetWifi(){
 const el=document.getElementById('wifiProvisionResult');
 if(!el) return;
 let body=collectNetworkBody();
 if(!String(body.wifi_sta_ssid||'').trim().length){
  await loadSettingsPageData(false);
  body=collectNetworkBody();
 }
 if(!String(body.wifi_sta_ssid||'').trim().length){
  el.className='result-line show err';
  el.innerText='Enter STA SSID before sending WiFi to fleet.';
  return;
 }
 el.className='result-line show';
 el.innerText='Sending WiFi credentials over LoRa...';
 const out=await apiJson('/api/network/provision-fleet',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),silent:true});
 if(out && out.ok){
  const msg=`LoRa WiFi provisioning sent (${out.packets||'?'} packets broadcast)`;
  el.className='result-line show ok';
  el.innerText=msg;
  if(wifiProvisionResultTimer){ clearTimeout(wifiProvisionResultTimer); wifiProvisionResultTimer=0; }
  wifiProvisionResultTimer=setTimeout(()=>{
   const cur=document.getElementById('wifiProvisionResult');
   if(!cur) return;
   cur.className='result-line';
   cur.innerText='';
   wifiProvisionResultTimer=0;
  }, 10000);
  showToast(msg);
  return;
 }
 if(out && out.error==='cooldown_active'){
  const sec=Math.max(1, Number(out.retry_after_s||Math.ceil(Number(out.retry_after_ms||0)/1000)||1));
  const msg=`Wait ${sec}s before sending WiFi provisioning again.`;
  el.className='result-line show err';
  el.innerText=msg;
  showToast(msg, true);
  return;
 }
 const err=(out && out.error) ? out.error : 'request_failed';
 const msg=`Fleet WiFi provisioning failed: ${err}`;
 el.className='result-line show err';
 el.innerText=msg;
 showToast(msg, true);
}
async function testMqtt(){
 const body=collectMqttBody();
 if(!body) return;
 const out=await apiJson('/api/mqtt/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 const el=document.getElementById('mqttTestResult');
 if(!el) return;
 const target=(out && out.host) ? `${out.host}:${out.port}` : `${body.mqtt_host}:${body.mqtt_port}`;
 if(!out){
  el.className='result-line show err';
  el.innerText='Broker test failed';
  showToast('Broker test failed', true);
  return;
 }
 if(out.ok){
  const msg=`Broker connection OK (${target})`;
  el.className='result-line show ok';
  el.innerText=msg;
  showToast(msg);
 }else{
  const msg=`Broker connection failed (${target}, state ${out.state})`;
  el.className='result-line show err';
  el.innerText=msg;
 showToast(msg, true);
 }
}
async function fleetDeviceAction(action, addr, intervalS, enabled, extraBody){
 const pathByAction={
  poll_now:`/api/fleet/${addr}/actions/poll-now`,
  forget:`/api/fleet/${addr}/actions/forget`,
  set_interval:`/api/fleet/${addr}/actions/poll-interval`,
  set_schedule:`/api/fleet/${addr}/actions/schedule`,
  factory_reset:`/api/fleet/${addr}/actions/factory-reset`,
 };
 const path=pathByAction[action];
 if(!path){
  showToast(`Fleet device action failed: unknown_action`, true);
  return false;
 }
 const body={};
 if(intervalS!==undefined && intervalS!==null){ body.interval_s = intervalS; }
 if(enabled!==undefined){ body.enabled = !!enabled; }
 if(extraBody && typeof extraBody==='object'){ Object.assign(body, extraBody); }
 const out=await apiJson(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),silent:true});
 if(!out || !out.ok){
  const err=(out && out.error) ? out.error : 'request_failed';
  showToast(`Fleet device action failed: ${err}`, true);
  return false;
 }
 return true;
}
async function fleetDeviceFactoryReset(addr){
 const keepEl=document.getElementById('fleet-detail-keep-fleet');
 const keepFleet=keepEl ? !!keepEl.checked : true;
 if(!keepFleet){
  alert('Warning: this will remove the device from the control of this LoRa fleet and it will require manual provisioning again.');
  const confirmWord=prompt("Type REMOVE to confirm fleet device reset without shared fleet key:");
  if(String(confirmWord||'').trim()!=='REMOVE'){
   showToast('Fleet device factory reset cancelled', true);
   return;
  }
 }
 if(!confirm(`Factory reset fleet device ${toHexByte(addr)}${keepFleet ? ' (keep shared fleet key)' : ''}?`)) return;
 const ok=await fleetDeviceAction('factory_reset', addr, undefined, undefined, {keep_shared_fleet_key:keepFleet});
 if(!ok) return;
 showToast(`Factory reset sent to ${toHexByte(addr)}`);
 await refreshFleet();
}

function shouldRunFleetDevicesLive(){
 return activePage==='fleet' && lastRoleIsTx && activeFleetTab==='devices';
}
function isFleetManageActive(){
 return activePage==='fleet' && activeFleetTab==='manage';
}
function isProvisioningUiBusy(){
 const st=String(provUiSessionState||'idle');
 return !!provUiSessionActive || st==='discovering' || st==='discovery_retry' || st==='provisioning';
}
function sessionPollDelayMs(){
 if(document.hidden) return 1800000;
 return 1800000;
}
async function fleetDevicePollNow(addr){
 const ok=await fleetDeviceAction('poll_now', addr);
 if(!ok) return;
 showToast(`Poll requested for ${toHexByte(addr)}`);
 await refreshFleet();
}
async function fleetDeviceForget(addr){
 if(!confirm(`Forget ${toHexByte(addr)}?`)) return;
 const ok=await fleetDeviceAction('forget', addr);
 if(!ok) return;
 showToast(`Forgot ${toHexByte(addr)}`);
 await refreshFleet();
}
function fleetDeviceIntervalFromInput(){
 const el=document.getElementById('fleet-detail-interval');
 const sec=Math.floor(Number(el ? el.value : NaN));
 if(!Number.isFinite(sec) || sec<60 || sec>3600){
  showToast('Interval must be 60..3600 seconds', true);
  return null;
 }
 return sec;
}
async function fleetDeviceSetInterval(addr){
 const sec=fleetDeviceIntervalFromInput();
 if(sec===null) return;
 const ok=await fleetDeviceAction('set_interval', addr, sec);
 if(!ok) return;
 showToast(`Interval updated for ${toHexByte(addr)}`);
 await refreshFleet();
}
async function fleetDeviceSetSchedule(addr, enabled){
 let sec=undefined;
 if(enabled){
  const s=fleetDeviceIntervalFromInput();
  if(s===null) return;
  sec=s;
 }
 const ok=await fleetDeviceAction('set_schedule', addr, sec, enabled);
 if(!ok) return;
 showToast(`${enabled ? 'Enabled' : 'Disabled'} schedule for ${toHexByte(addr)}`);
 await refreshFleet();
}
function setFleetDeviceDetailTab(tab){
 fleetDeviceDetailTab = (tab==='manage') ? 'manage' : 'state';
 renderFleetDeviceDetail();
}
function selectFleetDevice(addr){
 selectedFleetDeviceAddr = Number(addr||0);
 renderFleetDeviceDetail();
}
function renderFleetDeviceDetail(){
 const host=document.getElementById('fleetDetailHost');
 if(!host) return;
 const selected=fleetDevicesCache.find((r)=>Number(r.address||0)===Number(selectedFleetDeviceAddr||0));
 if(!selected){
  host.innerHTML='Select a device to view details.';
  return;
 }
 const addr=Number(selected.address||0);
 const addrHex=fleetDeviceAddrHex(selected);
 const seenAge=(Number(selected.last_seen_ms||0)>0) ? humanAgeMsShort(selected.last_seen_age_ms||0) : 'never';
 const pollAge=(Number(selected.last_poll_tx_ms||0)>0) ? humanAgeMsShort(selected.last_poll_age_ms||0) : 'never';
 const intervalS=Math.max(0, Math.round(Number(selected.poll_interval_ms||0)/1000));
 const stateView=`
 <div class="fleet-detail-grid">
   <div class="k">Device</div><div class="v">${escapeHtml(addrHex)}</div>
   <div class="k">Freshness</div><div class="v">${freshnessChip(selected)} ${escapeHtml(seenAge)}</div>
   <div class="k">Relay</div><div class="v">${relayChip(selected.relay_state)}</div>
   <div class="k">Input</div><div class="v">${inputChip(selected.input_state)}</div>
   <div class="k">Temperature</div><div class="v">${escapeHtml(fleetDeviceTempText(selected))}</div>
   <div class="k">Uplink RSSI</div><div class="v">${escapeHtml(String(selected.uplink_rssi||-127))} dBm</div>
   <div class="k">Downlink RSSI</div><div class="v">${selected.downlink_rssi_valid ? `${escapeHtml(String(selected.downlink_rssi))} dBm` : 'n/a'}</div>
   <div class="k">Poll state</div><div class="v"><span class="chip ${selected.poll_pending?'warn':'neutral'}">${selected.poll_pending?'pending':'idle'}</span> (last tx ${escapeHtml(pollAge)})</div>
   <div class="k">Ack</div><div class="v"><span class="chip ${ackChipClass(selected.ack_state)}">${escapeHtml(String(selected.ack_state||'unknown'))}</span></div>
 </div>`;
 const manageView=`
 <div class="fleet-detail-grid">
   <div class="k">Interval</div><div class="v"><div class="inline-row"><input id="fleet-detail-interval" type="number" min="60" max="3600" value="${intervalS>0?intervalS:60}" style="max-width:120px" /><span class="small">${intervalS>0?`${intervalS}s active`:'disabled'}</span></div></div>
   <div class="k">Factory reset</div><div class="v"><div class="check-row" style="margin-top:0"><input id="fleet-detail-keep-fleet" type="checkbox" checked /><label for="fleet-detail-keep-fleet">Keep shared fleet key</label></div><div class="small">Untick only if you want to remove this device from this LoRa fleet.</div></div>
 </div>
 <div class="fleet-row-actions" style="margin-top:10px">
   <button type="button" onclick="fleetDevicePollNow(${addr})">Poll now</button>
   <button type="button" onclick="fleetDeviceSetInterval(${addr})">Set interval</button>
   <button type="button" onclick="fleetDeviceSetSchedule(${addr},${intervalS===0?'true':'false'})">${intervalS===0?'Enable schedule':'Disable schedule'}</button>
   <button type="button" onclick="fleetDeviceFactoryReset(${addr})">Factory reset</button>
   <button type="button" onclick="fleetDeviceForget(${addr})">Forget</button>
 </div>`;
 host.innerHTML=`
  <div class="fleet-detail-tabs">
    <button class="tabbtn ${fleetDeviceDetailTab==='state'?'active':''}" type="button" onclick="setFleetDeviceDetailTab('state')">State</button>
    <button class="tabbtn ${fleetDeviceDetailTab==='manage'?'active':''}" type="button" onclick="setFleetDeviceDetailTab('manage')">Manage</button>
  </div>
  ${fleetDeviceDetailTab==='state' ? stateView : manageView}`;
}
async function refreshFleet(){
 if(!(activePage==='fleet' && activeFleetTab==='devices')) return;
 if(fleetRefreshInFlight) return;
 const host=document.getElementById('fleetTableHost');
 const summary=document.getElementById('fleetSummary');
 const detail=document.getElementById('fleetDetailHost');
 if(!host || !summary) return;
 fleetRefreshInFlight = true;
 const out=await apiJson('/api/fleet',{silent:true});
 if(!out){
  summary.innerText='Fleet device list unavailable.';
  host.innerHTML='Fleet device list unavailable.';
  if(detail) detail.innerHTML='Device details unavailable.';
  fleetRefreshInFlight = false;
  return;
 }
 const role=String(out.role||'').toLowerCase();
 if(role!=='tx'){
  summary.innerText='Fleet view is TX-only in this firmware.';
  host.innerHTML='Switch role to TX to manage discovered devices.';
  if(detail) detail.innerHTML='Switch role to TX to view device details.';
  fleetRefreshInFlight = false;
  return;
 }
 const devices=Array.isArray(out.devices) ? out.devices : [];
 fleetDevicesCache = devices;
 summary.innerText=`Discovered devices: ${devices.length} | Global schedule default: ${Math.max(60, Math.round(Number(out.tx_default_poll_interval_ms||60000)/1000))}s | Global polling: ${out.tx_polling_enabled ? 'enabled' : 'disabled'}`;
 if(devices.length===0){
  host.innerHTML='No devices discovered yet.';
  if(detail) detail.innerHTML='No device selected.';
  selectedFleetDeviceAddr = 0;
  fleetRefreshInFlight = false;
  return;
 }
 if(!devices.some((r)=>Number(r.address||0)===Number(selectedFleetDeviceAddr||0))){
  selectedFleetDeviceAddr = Number(devices[0].address||0);
 }
 const rows=devices.map((r)=>{
  const addrHex=fleetDeviceAddrHex(r);
  const addr=Number(r.address||0);
  const seenAge=(Number(r.last_seen_ms||0)>0) ? humanAgeMsShort(r.last_seen_age_ms||0) : 'never';
  const freshness=freshnessChip(r);
  const selectedCls=addr===Number(selectedFleetDeviceAddr||0) ? 'selected' : '';
  return `<tr class="${selectedCls}">
   <td><b>${escapeHtml(addrHex)}</b></td>
   <td>${relayChip(r.relay_state)}</td>
   <td>${inputChip(r.input_state)}</td>
   <td>${escapeHtml(fleetDeviceTempText(r))}</td>
   <td>${freshness} ${escapeHtml(seenAge)}</td>
   <td><button type="button" onclick="selectFleetDevice(${addr})">View</button></td>
  </tr>`;
 }).join('');
 host.innerHTML=`<table class="fleet-table"><thead><tr><th>Device</th><th>Relay</th><th>Input</th><th>Sensors</th><th>Freshness</th><th>View</th></tr></thead><tbody>${rows}</tbody></table>`;
 renderFleetDeviceDetail();
 fleetRefreshInFlight = false;
}
async function refreshDiagnostics(){
 const d=await apiJson('/api/diagnostics',{silent:true});
 const host=document.getElementById('diagGrid');
 const sys=document.getElementById('diagSystem');
 const txt=document.getElementById('diagText');
 if(!host||!sys||!txt) return;
 if(!d){ host.innerHTML='Diagnostics unavailable'; sys.innerHTML=''; return; }
 host.innerHTML = `
  <div class="sensor-tile">LoRa TX: ${d.lora_tx_packets}</div>
  <div class="sensor-tile">ACK OK: ${d.ack_ok}</div>
  <div class="sensor-tile">ACK Timeout: ${d.ack_timeout}</div>
  <div class="sensor-tile">Replay Drops: ${d.replay_drop}</div>
  <div class="sensor-tile">STA Connect Attempts: ${d.wifi_connect_attempts}</div>
  <div class="sensor-tile">STA Connect Failures: ${d.wifi_connect_fail}</div>
  <div class="sensor-tile">STA Disconnects: ${d.wifi_disconnects}</div>
  <div class="sensor-tile">STA State: ${d.sta_status_text} [${d.sta_status_code}]</div>`;
 sys.innerHTML = `
  <div class="k">Role</div><div class="v">${escapeHtml(String(d.role||'').toUpperCase())}</div>
  <div class="k">Addresses</div><div class="v">${escapeHtml(`${d.local_address} -> ${d.remote_address}`)}</div>
  <div class="k">Firmware</div><div class="v">${escapeHtml(d.fw_display || 'n/a')}</div>
  <div class="k">Build</div><div class="v">${escapeHtml(`${d.build_date || 'n/a'} ${d.build_time || ''}`)}</div>
  <div class="k">Uptime</div><div class="v">${escapeHtml(humanAgeMs(Number(d.uptime_ms || 0)))}</div>
  <div class="k">Free Heap</div><div class="v">${escapeHtml(String(d.free_heap_bytes || 0))} B</div>
  <div class="k">CPU Freq</div><div class="v">${escapeHtml(String(d.cpu_freq_mhz || 0))} MHz</div>
  <div class="k">Chip ID</div><div class="v">${escapeHtml(String(d.chip_id || 'n/a'))}</div>
  <div class="k">Flash (real/ide)</div><div class="v">${escapeHtml(String(d.flash_real_size || 0))} / ${escapeHtml(String(d.flash_ide_size || 0))} B</div>
  <div class="k">SDK/Core</div><div class="v">${escapeHtml(String(d.sdk_version || 'n/a'))} / ${escapeHtml(String(d.core_version || 'n/a'))}</div>`;
 txt.innerText=`Last save: ${d.audit_last_saved_by} at ${d.audit_last_saved_ms} ms | Last reboot: ${d.audit_last_reboot_reason} at ${d.audit_last_reboot_ms} ms | Boot count: ${d.audit_boot_count}`;
}
async function importConfig(file){
 if(!file) return;
 try{
  const text=await file.text();
  const body=JSON.parse(text);
  const out=await fetch('/api/settings/import',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  if(!out.ok){ showToast(`Import failed: ${await out.text()}`, true); return; }
  showToast('Import saved');
  await load();
 }catch(e){ showToast(`Import failed: ${e.message}`, true); }
}
async function uploadOta(){
 const f=document.getElementById('otaFile').files[0];
 const el=document.getElementById('otaResult');
 if(!f){ if(el) el.innerText='Select .bin file'; return; }
 if(el) el.innerText='Uploading...';
 const fd=new FormData();
 fd.append('firmware', f);
 try{
  const res=await fetch('/api/ota',{method:'POST',body:fd});
  const t=await res.text();
  if(!res.ok){ if(el) el.innerText=`OTA failed: ${t}`; return; }
  if(el) el.innerText='OTA complete, rebooting...';
 }catch(e){ if(el) el.innerText=`OTA failed: ${e.message}`; }
}
window.addEventListener('error', (e) => {
 const st=document.getElementById('statusTable');
 if(st){ st.innerText=`UI error: ${e.message}`; }
});
function initPage(){
 const role=document.getElementById('role_tx');
  const local=document.getElementById('local_address');
  const remote=document.getElementById('remote_address');
 const host=document.getElementById('lan_hostname');
 const staSsid=document.getElementById('wifi_sta_ssid');
 const staPass=document.getElementById('wifi_sta_password');
 const roleTx=document.getElementById('role_tx_true');
 const roleRx=document.getElementById('role_tx_false');
 const fleetKey=document.getElementById('fleet_passphrase');
 const syncRole=()=>{ if(role){ role.value = roleTx.checked ? 'true' : 'false'; refreshRoleLabels(); } };
 if(roleTx) roleTx.addEventListener('change',syncRole);
 if(roleRx) roleRx.addEventListener('change',syncRole);
 if(local) local.addEventListener('input',refreshAddressHints);
 if(remote) remote.addEventListener('input',refreshAddressHints);
 if(host && UI_MDNS_ENABLED) host.addEventListener('input',refreshHostnamePreview);
 if(fleetKey) fleetKey.addEventListener('input',updateDeploymentKeyStrength);
 if(staSsid) staSsid.addEventListener('input',updateStaTestButtonState);
 if(staPass) staPass.addEventListener('input',updateStaTestButtonState);
 bindFreqPreset();
 syncRole();
 showFleetTab(activeFleetTab);
 updateDeploymentKeyStrength();
 try{ applyTheme(localStorage.getItem('lrs_theme') === 'light' ? 'light' : 'dark'); }catch(e){ applyTheme('dark'); }
 const menuBtn=document.getElementById('menuBtn');
 if(menuBtn){ menuBtn.setAttribute('aria-expanded','false'); }
 document.addEventListener('keydown',(e)=>{ if(e.key==='Escape'){ toggleDrawer(false); } });
 showPage('status');
 window.addEventListener('beforeunload', ()=>{ stopProvisioningPolling(); stopPagePolling(); stopHeaderPolling(); closeStatusLiveSse(); });
 document.addEventListener('visibilitychange', ()=>{ syncPagePolling(); });
 load();
 syncPagePolling();
}
initPage();
</script></body></html>
)HTML";

const char *linkStateText(LinkState st) {
  switch (st) {
    case LinkState::Boot:
      return "boot";
    case LinkState::Idle:
      return "idle";
    case LinkState::WaitAck:
      return "wait_ack";
    case LinkState::Timeout:
      return "timeout";
  }
  return "unknown";
}
}

bool WebConsole::begin(ConfigStore *config,
                       NodeStateMachine *sm,
                       SensorManager *sensors,
                       LogBuffer *logs,
                       std::function<void(bool, bool)> onApply) {
  config_ = config;
  sm_ = sm;
  sensors_ = sensors;
  logs_ = logs;
  on_apply_ = onApply;
  server_.collectHeaders("Cookie", "User-Agent");
  initStatusCaches();

  routes();
  server_.begin();
  return true;
}

void WebConsole::tick() {
  server_.handleClient();
  tickStatusLiveSse();
}

void WebConsole::beginRequestLog(const char *path, bool api, bool poll, bool heapDiag) {
  request_log_.active = true;
  request_log_.api = api;
  request_log_.poll = poll;
  request_log_.heap_diag = heapDiag;
  request_log_.started_ms = millis();
  request_log_.status = 0;
  request_log_.path = path ? String(path) : server_.uri();
  request_log_.client_ip = server_.client().remoteIP().toString();
}

void WebConsole::finishRequestLog() {
  if (!request_log_.active) return;

  const uint32_t endMs = millis();
  const uint32_t durMs = endMs - request_log_.started_ms;
  const int status = request_log_.status;
  const String path = request_log_.path.length() ? request_log_.path : server_.uri();
  const String ip = request_log_.client_ip;
  const uint32_t freeHeap = lrslog::heapFree();
  const uint8_t heapFrag = lrslog::heapFragPercent();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const lrslog::Category cat = request_log_.api ? lrslog::Category::API : lrslog::Category::WEB;
  const lrslog::Level level = request_log_.poll ? lrslog::Level::DEBUG : lrslog::Level::INFO;

  if (request_log_.heap_diag) {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu",
                 httpMethodText(server_.method()),
                 path.c_str(),
                 status,
                 static_cast<unsigned long>(durMs),
                 ip.c_str(),
                 static_cast<unsigned long>(freeHeap),
                 static_cast<unsigned>(heapFrag),
                 static_cast<unsigned long>(maxBlock));
    if (freeHeap < kLowHeapWarnThresholdBytes &&
        (last_low_heap_warn_ms_ == 0 || (endMs - last_low_heap_warn_ms_) >= kLowHeapWarnMinIntervalMs)) {
      last_low_heap_warn_ms_ = endMs;
      LRS_LOGW(API,
               "event=low_heap path=%s heap_free=%lu heap_frag=%u max_free_block=%lu dur_ms=%lu",
               path.c_str(),
               static_cast<unsigned long>(freeHeap),
               static_cast<unsigned>(heapFrag),
               static_cast<unsigned long>(maxBlock),
               static_cast<unsigned long>(durMs));
    }
  } else {
    lrslog::logf(level,
                 cat,
                 "event=request method=%s path=%s status=%d dur_ms=%lu ip=%s",
                 httpMethodText(server_.method()),
                 path.c_str(),
                 status,
                 static_cast<unsigned long>(durMs),
                 ip.c_str());
  }

  if (durMs >= kWebRequestPressureDurMs) {
    // Local proxy for "recent slow-phase pressure" used by SSE pacing.
    // We don't currently have app-level slow-phase state exposed to WebConsole.
    last_web_pressure_ms_ = endMs;
  }

  request_log_ = RequestLogState{};
}

void WebConsole::markResponseStatus(int status) {
  if (request_log_.active) request_log_.status = status;
}

void WebConsole::sendTracked(int code, const char *contentType, const char *body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::sendTracked(int code, const char *contentType, const String &body) {
  markResponseStatus(code);
  server_.send(code, contentType, body);
}

void WebConsole::initStatusCaches() {
  // Keep cache allocation lazy on low-RAM boards; reserve() here can OOM during boot.
  status_cache_.built_ms = 0;
  status_live_cache_.built_ms = 0;
  status_static_cache_.built_ms = 0;
  status_lite_cache_.built_ms = 0;
}

bool WebConsole::apiHeapHealthy(uint32_t minFreeBytes, uint32_t minMaxBlockBytes) const {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  return freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes);
}

bool WebConsole::tryServeCachedJson(const char *path,
                                    uint32_t minFreeBytes,
                                    uint32_t minMaxBlockBytes,
                                    uint32_t ttlMs,
                                    JsonResponseCache &cache) {
  const uint32_t now = millis();
  if (cache.body.length() > 0 && ttlMs > 0 && (now - cache.built_ms) < ttlMs) {
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  if (apiHeapHealthy(minFreeBytes, minMaxBlockBytes)) {
    return false;
  }

  if (cache.body.length() > 0) {
    LRS_LOGW(API,
             "event=api_cached_stale path=%s heap_free=%lu heap_frag=%u max_free_block=%lu age_ms=%lu",
             path ? path : server_.uri().c_str(),
             static_cast<unsigned long>(lrslog::heapFree()),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
             static_cast<unsigned long>(now - cache.built_ms));
    sendTracked(200, "application/json", cache.body);
    return true;
  }

  return rejectApiIfLowHeap(path, minFreeBytes, minMaxBlockBytes);
}

void WebConsole::setUiNoStoreHeaders() {
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Expires", "0");
}

void WebConsole::closeStatusLiveSse() {
  if (status_live_sse_client_) {
    status_live_sse_client_.flush();
    status_live_sse_client_.stop();
  }
  status_live_sse_client_ = WiFiClient();
  status_live_sse_active_ = false;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;
  status_live_sse_last_interval_ms_ = 0;
}

uint32_t WebConsole::computeStatusLiveSseIntervalMs() const {
  const uint32_t now = millis();
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const uint32_t ratioPct = (freeHeap == 0U) ? 0U : ((maxBlock * 100U) / freeHeap);
  const bool recentPressure =
      (last_web_pressure_ms_ != 0U) && (static_cast<uint32_t>(now - last_web_pressure_ms_) < kStatusLiveSsePressureWindowMs);

  if (maxBlock < 1200U || ratioPct < 30U || freeHeap < 3000U) return kStatusLiveSsePushSevereMs;
  if (maxBlock < 1700U || ratioPct < 40U || (recentPressure && maxBlock < 2600U)) return kStatusLiveSsePushPressureMs;
  if (maxBlock < 2400U || ratioPct < 55U || recentPressure) return kStatusLiveSsePushWarnMs;
  return kStatusLiveSsePushHealthyMs;
}

bool WebConsole::buildStatusLiveCache() {
  if (!config_ || !sm_) return false;

  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  const wl_status_t st = WiFi.status();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["link_state"] = linkStateText(sm_->linkState());
  doc["relay_state"] = sm_->relayState();
  doc["input_state"] = sm_->inputState();
  doc["local_input_state"] = sm_->localDryContactState();
  doc["lora_last_rssi"] = sm_->lastPacketRssi();
  doc["lora_last_packet_ms"] = sm_->lastPacketMs();
  doc["lora_last_tx_ms"] = sm_->lastTxMs();
  doc["lora_remote_temp_valid"] = sm_->remoteTemperatureValid();
  doc["lora_remote_temp_c"] = sm_->remoteTemperatureC();
  doc["lora_remote_temp_ms"] = sm_->remoteTemperatureMs();
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  doc["sta_ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  String relayReason = "boot";
  if (cfg.role_tx) {
    if (sm_->relayState() == 0) {
      if (sm_->inputState() == 0) {
        relayReason = "input_open";
      } else if (sm_->linkState() == LinkState::Timeout) {
        relayReason = "ack_timeout";
      } else if (sm_->linkState() == LinkState::WaitAck) {
        relayReason = "wait_ack";
      } else {
        relayReason = "no_lora_link";
      }
    } else {
      relayReason = "ok";
    }
  } else {
    const bool relayOn = sm_->relayState() != 0;
    switch (sm_->lastRxControlSource()) {
      case RxControlSource::Mqtt:
        relayReason = relayOn ? "mqtt_on" : "mqtt_off";
        break;
      case RxControlSource::LoRa:
        relayReason = relayOn ? "lora_on" : "lora_off";
        break;
      default:
        relayReason = "boot";
        break;
    }
  }
  doc["relay_reason"] = relayReason;

  if (sensors_) {
    const TempSensorStatus &ts = sensors_->tempStatus();
    doc["sensor_temp_enabled"] = ts.enabled;
    doc["sensor_temp_detected"] = ts.detected;
    doc["sensor_temp_valid"] = ts.valid;
    doc["sensor_temp_c"] = ts.celsius;
    doc["sensor_temp_addr"] = ts.address;
    doc["sensor_temp_error"] = ts.error;
    doc["sensor_temp_last_read_ms"] = ts.last_read_ms;
  }

  status_live_cache_.body = "";
  serializeJson(doc, status_live_cache_.body);
  status_live_cache_.built_ms = millis();
  return status_live_cache_.body.length() > 0;
}

bool WebConsole::buildStatusStaticCache() {
  if (!config_) return false;

  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["sta_target_ssid"] = cfg.wifi_sta_ssid;
  doc["deployment_key"] = cfg.fleet_passphrase;
  doc["deployment_key_default"] = isDefaultDeploymentKey(cfg.fleet_passphrase);
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["fleet_setup_required"] = needsFleetSetupPrompt();
  doc["ap_ssid"] = config_->apSsid();
  doc["ap_ip"] = WiFi.softAPIP().toString();
#if LRS_ENABLE_MDNS
  doc["mdns_ap"] = "lrs.local";
  doc["mdns_lan"] = config_->settings().lan_hostname + ".local";
#endif
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  const String fwVersion = String(LRS_FW_VERSION);
  if (LRS_GIT_DIRTY == 0) {
    doc["fw_display"] = fwVersion + " (" + String(LRS_GIT_SHA) + ")";
  } else {
    doc["fw_display"] = fwVersion + " (" + String(LRS_GIT_SHA) + ", dirty)";
  }
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["session_remaining_s"] = sessionRemainingS();
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  status_static_cache_.body = "";
  serializeJson(doc, status_static_cache_.body);
  status_static_cache_.built_ms = millis();
  return status_static_cache_.body.length() > 0;
}

bool WebConsole::buildStatusLiteCache() {
  if (!config_) return false;

  DynamicJsonDocument doc(256);
  auto &cfg = config_->settings();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["relay_state"] = sm_ ? sm_->relayState() : 0;
  doc["lora_last_rssi"] = sm_ ? sm_->lastPacketRssi() : 0;
  doc["lora_last_packet_ms"] = sm_ ? sm_->lastPacketMs() : 0;
  doc["lora_last_tx_ms"] = sm_ ? sm_->lastTxMs() : 0;
  doc["sta_connected"] = WiFi.isConnected();
  doc["sta_rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
  doc["heap_free_bytes"] = ESP.getFreeHeap();
  doc["heap_frag_percent"] = lrslog::heapFragPercent();
  doc["max_free_block_bytes"] = lrslog::heapMaxFreeBlock();
  doc["uptime_ms"] = millis();

  status_lite_cache_.body = "";
  serializeJson(doc, status_lite_cache_.body);
  status_lite_cache_.built_ms = millis();
  return status_lite_cache_.body.length() > 0;
}

bool WebConsole::buildStatusCompatCacheFromLiveStatic() {
  if (status_live_cache_.body.length() == 0 || status_static_cache_.body.length() == 0) {
    return false;
  }

  DynamicJsonDocument liveDoc(768);
  DynamicJsonDocument staticDoc(768);
  if (deserializeJson(staticDoc, status_static_cache_.body)) return false;
  if (deserializeJson(liveDoc, status_live_cache_.body)) return false;

  DynamicJsonDocument merged(1536);
  JsonObject dst = merged.to<JsonObject>();
  JsonObject staticObj = staticDoc.as<JsonObject>();
  for (JsonPair kv : staticObj) {
    dst[kv.key()] = kv.value();
  }
  JsonObject liveObj = liveDoc.as<JsonObject>();
  for (JsonPair kv : liveObj) {
    dst[kv.key()] = kv.value();
  }

  status_cache_.body = "";
  serializeJson(merged, status_cache_.body);
  status_cache_.built_ms = millis();
  return status_cache_.body.length() > 0;
}

void WebConsole::tickStatusLiveSse() {
  if (!status_live_sse_active_) return;
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }

  const uint32_t now = millis();
  const uint32_t pushIntervalMs = computeStatusLiveSseIntervalMs();
  status_live_sse_last_interval_ms_ = pushIntervalMs;

  if (status_live_sse_last_keepalive_ms_ == 0 || (now - status_live_sse_last_keepalive_ms_) >= kStatusLiveSseKeepAliveMs) {
    if (status_live_sse_client_.print(F(": keepalive\n\n")) == 0) {
      closeStatusLiveSse();
      return;
    }
    status_live_sse_last_keepalive_ms_ = now;
  }

  if (status_live_sse_last_push_ms_ != 0 && (now - status_live_sse_last_push_ms_) < pushIntervalMs) {
    return;
  }

  bool cacheUpdated = false;
  if (status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) {
    if (apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
      cacheUpdated = buildStatusLiveCache();
    }
  }

  if (status_live_cache_.body.length() == 0) {
    return;
  }

  if (!cacheUpdated && status_live_cache_.built_ms == status_live_sse_last_sent_cache_ms_) {
    return;
  }

  if (status_live_sse_client_.print(F("event: status\nid: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.built_ms) == 0 ||
      status_live_sse_client_.print(F("\ndata: ")) == 0 ||
      status_live_sse_client_.print(status_live_cache_.body) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }

  status_live_sse_last_push_ms_ = now;
  status_live_sse_last_sent_cache_ms_ = status_live_cache_.built_ms;
}

bool WebConsole::rejectApiIfLowHeap(const char *path, uint32_t minFreeBytes, uint32_t minMaxBlockBytes) {
  const uint32_t freeHeap = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  if (freeHeap >= minFreeBytes && (minMaxBlockBytes == 0 || maxBlock >= minMaxBlockBytes)) {
    return false;
  }
  LRS_LOGW(API,
           "event=api_low_heap_reject path=%s heap_free=%lu heap_frag=%u max_free_block=%lu need_free=%lu need_block=%lu",
           path ? path : server_.uri().c_str(),
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock),
           static_cast<unsigned long>(minFreeBytes),
           static_cast<unsigned long>(minMaxBlockBytes));
  char body[160];
  snprintf(body,
           sizeof(body),
           "{\"ok\":false,\"error\":\"low_heap\",\"heap_free\":%lu,\"heap_frag\":%u,\"max_free_block\":%lu}",
           static_cast<unsigned long>(freeHeap),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(maxBlock));
  sendTracked(503, "application/json", body);
  return true;
}

bool WebConsole::hasSession() const {
  if (session_token_.length() == 0) return false;
  if (session_expires_ms_ == 0) return false;
  return static_cast<int32_t>(session_expires_ms_ - millis()) > 0;
}

void WebConsole::clearSession() {
  session_token_ = "";
  session_expires_ms_ = 0;
}

String WebConsole::cookieValue(const String &name) const {
  const String raw = server_.header("Cookie");
  if (raw.length() == 0) return "";
  const String needle = name + "=";
  int p = raw.indexOf(needle);
  if (p < 0) return "";
  p += needle.length();
  int e = raw.indexOf(';', p);
  if (e < 0) e = raw.length();
  String v = raw.substring(p, e);
  v.trim();
  return v;
}

String WebConsole::randomToken() const {
  char out[33];
  for (size_t i = 0; i < 16; i++) {
    const uint8_t b = static_cast<uint8_t>(::random(0, 256));
    snprintf(out + (i * 2), 3, "%02x", b);
  }
  out[32] = '\0';
  return String(out);
}

void WebConsole::startSession() {
  session_token_ = randomToken();
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  server_.sendHeader("Set-Cookie", "lrs_session=" + session_token_ + "; Path=/; HttpOnly; SameSite=Lax");
}

uint32_t WebConsole::sessionRemainingS() const {
  if (!hasSession()) return 0;
  return static_cast<uint32_t>((session_expires_ms_ - millis()) / 1000UL);
}

bool WebConsole::requireAuth(bool api) {
  if (locked_until_ms_ != 0 && static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    if (api) {
      sendTracked(429, "application/json", "{\"error\":\"too_many_failed_logins\"}");
    } else {
      sendTracked(429, "text/plain", "Too many failed logins. Try again shortly.");
    }
    LRS_LOGW(API, "event=auth_locked ip=%s", server_.client().remoteIP().toString().c_str());
    return false;
  }

  if (!hasSession()) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    return false;
  }

  const String cookie = cookieValue("lrs_session");
  if (cookie.length() == 0 || cookie != session_token_) {
    if (api) {
      sendTracked(401, "application/json", "{\"error\":\"auth_required\"}");
    } else {
      server_.sendHeader("Location", "/login?expired=1");
      sendTracked(302, "text/plain", "redirect");
    }
    LRS_LOGW(API,
             "event=auth_cookie_invalid ip=%s has_cookie=%u",
             server_.client().remoteIP().toString().c_str(),
             static_cast<unsigned>(cookie.length() ? 1U : 0U));
    return false;
  }
  session_expires_ms_ = millis() + (30UL * 60UL * 1000UL);
  failed_auth_ = 0;
  return true;
}

bool WebConsole::isSoftApActive() const { return softApActiveNow(); }

bool WebConsole::needsFleetSetupPrompt() const {
  if (config_ == nullptr) return false;
  const auto &cfg = config_->settings();
  return isDefaultDeploymentKey(cfg.fleet_passphrase) && !cfg.fleet_setup_prompt_dismissed;
}

void WebConsole::handleCaptiveProbe() {
  if (!isSoftApActive()) {
    sendTracked(204, "text/plain", "");
    return;
  }
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("Location", "http://192.168.4.1/");
  sendTracked(302, "text/plain", "Redirecting to LRS console");
}

void WebConsole::routes() {
  server_.on("/", HTTP_GET, [this]() {
    beginRequestLog("/", false, false, true);
    handleIndex();
    finishRequestLog();
  });
  server_.on("/login", HTTP_GET, [this]() {
    beginRequestLog("/login", false, false, true);
    handleLoginPage();
    finishRequestLog();
  });
  server_.on("/setup", HTTP_GET, [this]() {
    beginRequestLog("/setup", false, false, true);
    handleFleetSetupPage();
    finishRequestLog();
  });
  server_.on("/api/login", HTTP_POST, [this]() {
    beginRequestLog("/api/login", true, false, true);
    handleLoginApi();
    finishRequestLog();
  });
  server_.on("/api/setup/fleet-key", HTTP_POST, [this]() {
    beginRequestLog("/api/setup/fleet-key", true, false, true);
    handleFleetSetupApi();
    finishRequestLog();
  });
  server_.on("/api/logout", HTTP_POST, [this]() {
    beginRequestLog("/api/logout", true, false, false);
    handleLogoutApi();
    finishRequestLog();
  });
  server_.on("/api/session", HTTP_GET, [this]() {
    beginRequestLog("/api/session", true, true, true);
    handleSessionApi();
    finishRequestLog();
  });
  server_.on("/generate_204", HTTP_GET, [this]() { handleCaptiveProbe(); });       // Android
  server_.on("/gen_204", HTTP_GET, [this]() { handleCaptiveProbe(); });            // Android (variant)
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handleCaptiveProbe(); });  // Apple
  server_.on("/ncsi.txt", HTTP_GET, [this]() { handleCaptiveProbe(); });           // Windows
  server_.on("/connecttest.txt", HTTP_GET, [this]() { handleCaptiveProbe(); });    // Windows
  server_.on("/fwlink", HTTP_GET, [this]() { handleCaptiveProbe(); });             // Windows
  server_.on("/api/status", HTTP_GET, [this]() {
    beginRequestLog("/api/status", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatus();
    finishRequestLog();
  });
  server_.on("/api/status-live", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLive();
    finishRequestLog();
  });
  server_.on("/api/status-live/events", HTTP_GET, [this]() {
    beginRequestLog("/api/status-live/events", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLiveEvents();
    finishRequestLog();
  });
  server_.on("/api/status-static", HTTP_GET, [this]() {
    beginRequestLog("/api/status-static", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusStatic();
    finishRequestLog();
  });
  server_.on("/api/status-lite", HTTP_GET, [this]() {
    beginRequestLog("/api/status-lite", true, true, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleStatusLite();
    finishRequestLog();
  });
  server_.on("/api/fleet", HTTP_GET, [this]() {
    beginRequestLog("/api/fleet", true, false, true);
    if (!requireAuth(true)) {
      finishRequestLog();
      return;
    }
    handleFleet();
    finishRequestLog();
  });
  server_.on("/api/factory", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleFactory();
  });
  server_.on("/api/diagnostics", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleDiagnostics();
  });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("networks");
    const int count = WiFi.scanNetworks(false, true);
    for (int i = 0; i < count; i++) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      if (isOwnLrsSoftApLike(ssid)) continue;
      JsonObject n = arr.createNestedObject();
      n["ssid"] = ssid;
      n["rssi"] = WiFi.RSSI(i);
      n["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
  });
  server_.on("/api/network/test", HTTP_POST, [this]() { handleTestSta(); });
  server_.on("/api/network/provision-fleet", HTTP_POST, [this]() {
    beginRequestLog("/api/network/provision-fleet", true, false, true);
    handleProvisionFleetWifi();
    finishRequestLog();
  });
  server_.on("/api/provisioning/start", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/start", true, false, true);
    handleProvisioningStart();
    finishRequestLog();
  });
  server_.on("/api/provisioning/status", HTTP_GET, [this]() {
    beginRequestLog("/api/provisioning/status", true, true, true);
    handleProvisioningStatus();
    finishRequestLog();
  });
  server_.on("/api/provisioning/provision-all", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/provision-all", true, false, true);
    handleProvisioningProvisionAll();
    finishRequestLog();
  });
  server_.on("/api/provisioning/cancel", HTTP_POST, [this]() {
    beginRequestLog("/api/provisioning/cancel", true, false, true);
    handleProvisioningCancel();
    finishRequestLog();
  });
  server_.on("/api/mqtt/test", HTTP_POST, [this]() { handleTestMqtt(); });
  server_.on("/api/settings", HTTP_GET, [this]() {
    if (!requireAuth(true)) return;
    handleGetSettings();
  });
  server_.on("/api/settings", HTTP_POST, [this]() { handlePostSettings(); });
  server_.on("/api/settings/export", HTTP_GET, [this]() { handleExportSettings(); });
  server_.on("/api/settings/import", HTTP_POST, [this]() { handleImportSettings(); });
  server_.on(
      "/api/ota", HTTP_POST, [this]() { handleOtaUpload(); }, [this]() { handleOtaUploadChunk(); });
  server_.on("/api/logs.csv", HTTP_GET, [this]() { handleLogsCsv(); });
  server_.on("/api/logs.txt", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/system/factory-reset", HTTP_POST, [this]() { handleFactoryReset(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.onNotFound([this]() {
    if (server_.method() == HTTP_POST && handleFleetDeviceActionRoute(server_.uri())) {
      return;
    }
    if (isSoftApActive()) {
      handleCaptiveProbe();
      return;
    }
    server_.send(404, "text/plain", "not found");
  });
}

void WebConsole::handleIndex() {
  if (!requireAuth(false)) return;
  if (needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/setup");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  const uint32_t heapBefore = lrslog::heapFree();
  const uint32_t maxBlockBefore = lrslog::heapMaxFreeBlock();
  const uint8_t fragBefore = lrslog::heapFragPercent();
  const bool forceFull = server_.hasArg("force_full") && server_.arg("force_full") != "0";
  LRS_LOGI(WEB,
           "event=index_send_start heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned>(fragBefore),
           static_cast<unsigned long>(maxBlockBefore));
  if (!forceFull && (heapBefore < kIndexLowHeapRejectFreeBytes || maxBlockBefore < kIndexLowHeapRejectMaxBlockBytes)) {
    LRS_LOGW(WEB,
             "event=index_send_reject_low_heap heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
    LRS_LOGI(WEB, "event=index_send_low_heap_fallback");
    markResponseStatus(200);
    setUiNoStoreHeaders();
    server_.send_P(200, "text/html", kIndexLowHeapHtml);
    return;
  }
  if (forceFull && (heapBefore < kIndexLowHeapRejectFreeBytes || maxBlockBefore < kIndexLowHeapRejectMaxBlockBytes)) {
    LRS_LOGW(WEB,
             "event=index_send_force_low_heap heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapBefore),
             static_cast<unsigned>(fragBefore),
             static_cast<unsigned long>(maxBlockBefore));
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kIndexHtml);
  LRS_LOGI(WEB,
           "event=index_send_done heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
}

void WebConsole::handleLoginPage() {
  if (hasSession() && cookieValue("lrs_session") == session_token_) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", needsFleetSetupPrompt() ? "/setup" : "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kLoginHtml);
}

void WebConsole::handleFleetSetupPage() {
  if (!requireAuth(false)) return;
  if (!needsFleetSetupPrompt()) {
    setUiNoStoreHeaders();
    server_.sendHeader("Location", "/");
    sendTracked(302, "text/plain", "redirect");
    return;
  }
  markResponseStatus(200);
  setUiNoStoreHeaders();
  server_.send_P(200, "text/html", kFleetSetupHtml);
}

void WebConsole::handleLoginApi() {
  if (locked_until_ms_ != 0 && static_cast<int32_t>(locked_until_ms_ - millis()) > 0) {
    sendTracked(429, "application/json", "{\"error\":\"Too many failed logins. Try again shortly.\"}");
    LRS_LOGW(API, "event=login_blocked ip=%s", server_.client().remoteIP().toString().c_str());
    return;
  }

  DynamicJsonDocument doc(256);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  const String posted = String(static_cast<const char *>(doc["password"] | ""));
  if (posted != config_->settings().admin_password) {
    failed_auth_++;
    if (failed_auth_ >= 5) {
      locked_until_ms_ = millis() + 60000;
      failed_auth_ = 0;
    }
    sendTracked(401, "application/json", "{\"error\":\"Invalid password\"}");
    LRS_LOGW(API,
             "event=login_failed ip=%s remaining_lock_attempts=%u",
             server_.client().remoteIP().toString().c_str(),
             static_cast<unsigned>((failed_auth_ < 5) ? (5 - failed_auth_) : 0));
    return;
  }

  failed_auth_ = 0;
  locked_until_ms_ = 0;
  startSession();
  DynamicJsonDocument out(128);
  out["ok"] = true;
  out["setup_required"] = needsFleetSetupPrompt();
  String body;
  serializeJson(out, body);
  sendTracked(200, "application/json", body);
  LRS_LOGI(API,
           "event=login_ok ip=%s setup_required=%u",
           server_.client().remoteIP().toString().c_str(),
           needsFleetSetupPrompt() ? 1U : 0U);
}

void WebConsole::handleFleetSetupApi() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument doc(384);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const bool skip = parseBoolField(doc["skip"], false);
  if (skip) {
    cfg.fleet_setup_prompt_dismissed = true;
    cfg.audit_last_saved_by = "first_login_skip";
    cfg.audit_last_saved_ms = millis();
    if (!config_->save()) {
      sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
      return;
    }
    sendTracked(200, "application/json", "{\"ok\":true,\"skipped\":true}");
    LRS_LOGI(API, "event=fleet_setup_skip");
    return;
  }

  String fleetKey = String(static_cast<const char *>(doc["fleet_passphrase"] | ""));
  fleetKey.trim();
  if (fleetKey.length() < kMinDeploymentKeyLen) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"deployment_key_too_short\"}");
    return;
  }
  if (isDefaultDeploymentKey(fleetKey)) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"deployment_key_default_blocked\"}");
    return;
  }

  cfg.fleet_passphrase = fleetKey;
  cfg.fleet_setup_prompt_dismissed = true;
  cfg.audit_last_saved_by = "first_login_setup";
  cfg.audit_last_saved_ms = millis();
  if (!config_->save()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"save_failed\"}");
    return;
  }
  if (on_apply_) on_apply_(false, false);
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=fleet_key_set key=%s", lrslog::maskSecret(fleetKey).c_str());
}

void WebConsole::handleLogoutApi() {
  clearSession();
  server_.sendHeader("Set-Cookie", "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=logout ip=%s", server_.client().remoteIP().toString().c_str());
}

void WebConsole::handleSessionApi() {
  DynamicJsonDocument doc(128);
  const bool ok = hasSession() && cookieValue("lrs_session") == session_token_;
  doc["ok"] = ok;
  doc["remaining_s"] = ok ? sessionRemainingS() : 0;
  doc["setup_required"] = ok ? needsFleetSetupPrompt() : false;
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleStatus() {
  if (tryServeCachedJson("/api/status",
                         kApiLowHeapRejectFreeBytes,
                         kApiLowHeapRejectMaxBlockBytes,
                         kStatusCacheTtlMs,
                         status_cache_)) {
    return;
  }

  const uint32_t now = millis();
  if (last_status_compat_hit_log_ms_ == 0 || (now - last_status_compat_hit_log_ms_) >= kStatusCompatHitLogMinIntervalMs) {
    last_status_compat_hit_log_ms_ = now;
    String ua = server_.header("User-Agent");
    if (ua.length() > 96) ua = ua.substring(0, 96);
    const String clientTag = server_.arg("client");
    LRS_LOGW(API,
             "event=status_compat_hit ip=%s client=%s ua=%s",
             server_.client().remoteIP().toString().c_str(),
             clientTag.length() ? clientTag.c_str() : "-",
             ua.length() ? ua.c_str() : "-");
  }

  if ((status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) &&
      apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
    buildStatusLiveCache();
  }
  if ((status_static_cache_.body.length() == 0 || (now - status_static_cache_.built_ms) >= kStatusStaticCacheTtlMs) &&
      apiHeapHealthy(kApiStatusStaticLowHeapRejectFreeBytes, kApiStatusStaticLowHeapRejectMaxBlockBytes)) {
    buildStatusStaticCache();
  }

  if (!buildStatusCompatCacheFromLiveStatic()) {
    if (status_cache_.body.length() > 0) {
      LRS_LOGW(API,
               "event=status_compat_stale_fallback heap_free=%lu heap_frag=%u max_free_block=%lu",
               static_cast<unsigned long>(lrslog::heapFree()),
               static_cast<unsigned>(lrslog::heapFragPercent()),
               static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
      sendTracked(200, "application/json", status_cache_.body);
      return;
    }
    sendTracked(503, "application/json", "{\"ok\":false,\"error\":\"compat_cache_unavailable\"}");
    return;
  }

  sendTracked(200, "application/json", status_cache_.body);
}

void WebConsole::handleStatusLive() {
  if (tryServeCachedJson("/api/status-live",
                         kApiStatusLiveLowHeapRejectFreeBytes,
                         kApiStatusLiveLowHeapRejectMaxBlockBytes,
                         kStatusLiveCacheTtlMs,
                         status_live_cache_))
    return;
  if (!buildStatusLiveCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_live_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_live_cache_.body);
}

void WebConsole::handleStatusLiveEvents() {
  const bool connectHeapHealthy =
      apiHeapHealthy(kStatusLiveSseConnectMinFreeBytes, kStatusLiveSseConnectMinMaxBlockBytes);

  if (status_live_sse_active_) {
    closeStatusLiveSse();
  }

  const uint32_t now = millis();
  if ((status_live_cache_.body.length() == 0 || (now - status_live_cache_.built_ms) >= kStatusLiveCacheTtlMs) &&
      apiHeapHealthy(kApiStatusLiveLowHeapRejectFreeBytes, kApiStatusLiveLowHeapRejectMaxBlockBytes)) {
    buildStatusLiveCache();
  }

  WiFiClient client = server_.client();
  client.setNoDelay(true);
  client.setSync(true);
  status_live_sse_client_ = client;
  status_live_sse_active_ = true;
  status_live_sse_last_push_ms_ = 0;
  status_live_sse_last_keepalive_ms_ = 0;
  status_live_sse_last_sent_cache_ms_ = 0;

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  markResponseStatus(200);
  server_.sendContent_P(PSTR("HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
                             "Pragma: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "X-Accel-Buffering: no\r\n"
                             "\r\n"));
  if (!status_live_sse_client_ || !status_live_sse_client_.connected()) {
    closeStatusLiveSse();
    return;
  }
  if (status_live_sse_client_.print(F("retry: ")) == 0 ||
      status_live_sse_client_.print(connectHeapHealthy ? 3000U : 10000U) == 0 ||
      status_live_sse_client_.print(F("\n\n")) == 0) {
    closeStatusLiveSse();
    return;
  }
  tickStatusLiveSse();
  LRS_LOGI(API,
           "event=status_live_sse_open ip=%s heap_free=%lu heap_frag=%u max_free_block=%lu heap_ok=%u",
           server_.client().remoteIP().toString().c_str(),
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()),
           connectHeapHealthy ? 1U : 0U);
}

void WebConsole::handleStatusStatic() {
  if (tryServeCachedJson("/api/status-static",
                         kApiStatusStaticLowHeapRejectFreeBytes,
                         kApiStatusStaticLowHeapRejectMaxBlockBytes,
                         kStatusStaticCacheTtlMs,
                         status_static_cache_))
    return;
  if (!buildStatusStaticCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_static_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_static_cache_.body);
}

void WebConsole::handleStatusLite() {
  if (tryServeCachedJson("/api/status-lite",
                         kApiLightLowHeapRejectFreeBytes,
                         kApiLightLowHeapRejectMaxBlockBytes,
                         kStatusLiteCacheTtlMs,
                         status_lite_cache_))
    return;
  if (!buildStatusLiteCache()) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"status_lite_build_failed\"}");
    return;
  }
  sendTracked(200, "application/json", status_lite_cache_.body);
}

void WebConsole::handleFleet() {
  if (rejectApiIfLowHeap("/api/fleet", kApiFleetLowHeapRejectFreeBytes, kApiFleetLowHeapRejectMaxBlockBytes)) return;
  DynamicJsonDocument doc(4096);
  auto &cfg = config_->settings();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["tx_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  const uint32_t now = millis();
  doc["uptime_ms"] = now;
  JsonArray arr = doc.createNestedArray("devices");
  if (cfg.role_tx && sm_ != nullptr) {
    const size_t n = sm_->peerCount();
    for (size_t i = 0; i < n; ++i) {
      PeerStatusSnapshot node{};
      if (!sm_->peerByIndex(i, node)) continue;
      JsonObject r = arr.createNestedObject();
      r["address"] = node.address;
      char addrHex[5];
      snprintf(addrHex, sizeof(addrHex), "0x%02X", node.address);
      r["addr_hex"] = addrHex;
      r["relay_state"] = node.relay_state;
      r["input_state"] = node.input_state;
      r["temp_valid"] = node.temp_valid;
      r["temp_c"] = node.temp_c;
      r["uplink_rssi"] = node.uplink_rssi;
      r["downlink_rssi_valid"] = node.downlink_rssi_valid;
      r["downlink_rssi"] = node.downlink_rssi;
      r["last_seen_ms"] = node.last_seen_ms;
      const uint32_t seenAgeMs = (node.last_seen_ms > 0 && now >= node.last_seen_ms) ? (now - node.last_seen_ms) : 0;
      r["last_seen_age_ms"] = seenAgeMs;
      r["last_cmd_counter"] = node.last_cmd_counter;
      r["ack_state"] = remoteAckStateText(node.ack_state);
      r["poll_interval_ms"] = node.poll_interval_ms;
      r["poll_interval_s"] = node.poll_interval_ms / 1000U;
      r["last_poll_tx_ms"] = node.last_poll_tx_ms;
      const uint32_t pollAgeMs = (node.last_poll_tx_ms > 0 && now >= node.last_poll_tx_ms) ? (now - node.last_poll_tx_ms) : 0;
      r["last_poll_age_ms"] = pollAgeMs;
      r["poll_pending"] = node.poll_pending;
      r["poll_state"] = node.poll_pending ? "pending" : "idle";
      const uint32_t expectedIntervalMs = (node.poll_interval_ms > 0) ? node.poll_interval_ms : 300000U;
      uint32_t staleAfterMs = expectedIntervalMs * 3U;
      if (staleAfterMs < 180000U) staleAfterMs = 180000U;
      r["expected_interval_ms"] = expectedIntervalMs;
      r["stale_after_ms"] = staleAfterMs;
      r["stale_threshold_ms"] = staleAfterMs;
      r["stale"] = (node.last_seen_ms == 0) || (seenAgeMs > staleAfterMs);
    }
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

bool WebConsole::handleFleetDeviceActionRoute(const String &uri) {
  const String prefix = "/api/fleet/";
  if (!uri.startsWith(prefix)) return false;
  const String suffix = uri.substring(prefix.length());
  const int slash = suffix.indexOf('/');
  if (slash <= 0) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!requireAuth(true)) return true;
  auto &cfg = config_->settings();
  if (!cfg.role_tx || sm_ == nullptr) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"tx_only\"}");
    return true;
  }

  const uint8_t addr = parseAddressText(suffix.substring(0, slash), 0);
  if (addr == 0 || addr == 255) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_address\"}");
    return true;
  }

  const String actionPrefix = "actions/";
  const String tail = suffix.substring(slash + 1);
  if (!tail.startsWith(actionPrefix)) {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }
  const String action = tail.substring(actionPrefix.length());

  DynamicJsonDocument doc(256);
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return true;
    }
  }

  bool ok = false;
  if (action == "poll-now") {
    ok = sm_->mqttPollPeerNow(addr);
  } else if (action == "forget") {
    ok = sm_->mqttForgetPeer(addr);
  } else if (action == "poll-interval") {
    uint32_t sec = doc["interval_s"] | 0;
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, sec * 1000U);
  } else if (action == "schedule") {
    const bool enabled = parseBoolField(doc["enabled"], true);
    uint32_t sec = doc["interval_s"] | (cfg.tx_mqtt_remote_default_poll_interval_ms / 1000U);
    if (sec > 0 && sec < 60U) sec = 60U;
    if (sec > 3600U) sec = 3600U;
    ok = sm_->mqttSetPeerPollIntervalMs(addr, enabled ? (sec * 1000U) : 0U);
  } else if (action == "factory-reset") {
    const bool keepSharedFleetKey = parseBoolField(doc["keep_shared_fleet_key"], true);
    ok = sm_->sendPeerFactoryReset(addr, keepSharedFleetKey);
  } else {
    server_.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}");
    return true;
  }

  if (!ok) {
    server_.send(409, "application/json", "{\"ok\":false,\"error\":\"action_failed\"}");
    return true;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
  return true;
}

void WebConsole::handleFactory() {
  DynamicJsonDocument doc(512);
  auto &cfg = config_->settings();
  doc["serial"] = cfg.factory_serial;
  doc["chip_id"] = config_->chipIdHex();
  doc["mac"] = WiFi.softAPmacAddress();
  doc["factory_role"] = cfg.role_tx ? "tx" : "rx";
  doc["factory_local_address"] = cfg.local_address;
  doc["factory_remote_address"] = cfg.remote_address;
  doc["factory_ap_ssid"] = config_->apSsid();
  doc["factory_ap_password"] = config_->apPassword();
  doc["hardware_version"] = kHardwareVersion;
  doc["hardware_batch"] = kHardwareBatch;
  doc["fw_version"] = LRS_FW_VERSION;
  doc["fw_git_sha"] = LRS_GIT_SHA;
  doc["fw_git_branch"] = LRS_GIT_BRANCH;
  doc["fw_dirty"] = (LRS_GIT_DIRTY != 0);
  doc["fw_build_id"] = LRS_BUILD_ID;
  doc["fw_build_date_short"] = LRS_BUILD_DATE_SHORT;
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleGetSettings() {
  DynamicJsonDocument doc(1024);
  auto &cfg = config_->settings();
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["tx_input_lora_control_enabled"] = cfg.tx_input_lora_control_enabled;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg.wifi_sta_password;
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = cfg.fleet_passphrase;
  doc["fleet_setup_prompt_dismissed"] = cfg.fleet_setup_prompt_dismissed;
  doc["admin_password"] = cfg.admin_password;
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_enabled"] = cfg.mqtt_enabled;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = cfg.mqtt_password;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg.sensor_temp_interval_s;

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handlePostSettings() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument doc(1536);
  auto err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    server_.send(400, "text/plain", "invalid json");
    return;
  }

  auto &cfg = config_->settings();
  const Settings prev = cfg;
  Settings next = cfg;
  const String prevStaSsid = prev.wifi_sta_ssid;
  const String prevStaPassword = prev.wifi_sta_password;
  const String prevLanHost = prev.lan_hostname;
  const bool prevApAlwaysOn = prev.ap_always_on;
  const String prevAdminPassword = prev.admin_password;
  const bool oldRoleTx = prev.role_tx;
  const String oldDefaultHost = config_->defaultLanHostnameForRole(oldRoleTx);
  const String oldLegacyDefaultHost = String("lrs-") + config_->chipIdHex();
  const String oldLegacyRoleTxHost = oldLegacyDefaultHost + "-tx";
  const String oldLegacyRoleRxHost = oldLegacyDefaultHost + "-rx";
  next.role_tx = parseBoolField(doc["role_tx"], next.role_tx);
  next.local_address = parseAddressField(doc["local_address"], next.local_address);
  next.remote_address = parseAddressField(doc["remote_address"], next.remote_address);
  next.lora_frequency_hz = doc["lora_frequency_hz"] | next.lora_frequency_hz;
  next.lora_tx_power = static_cast<uint8_t>(doc["lora_tx_power"] | next.lora_tx_power);
  next.lora_spreading_factor = static_cast<uint8_t>(doc["lora_spreading_factor"] | next.lora_spreading_factor);
  next.lora_bandwidth_hz = doc["lora_bandwidth_hz"] | next.lora_bandwidth_hz;
  next.lora_coding_rate = static_cast<uint8_t>(doc["lora_coding_rate"] | next.lora_coding_rate);
  next.heartbeat_ms = doc["heartbeat_ms"] | next.heartbeat_ms;
  next.ack_timeout_ms = doc["ack_timeout_ms"] | next.ack_timeout_ms;
  next.mqtt_remote_retry_timeout_ms = doc["mqtt_remote_retry_timeout_ms"] | next.mqtt_remote_retry_timeout_ms;
  next.tx_mqtt_remote_polling_enabled = parseBoolField(doc["tx_mqtt_remote_polling_enabled"], next.tx_mqtt_remote_polling_enabled);
  next.tx_mqtt_remote_default_poll_interval_ms =
      doc["tx_mqtt_remote_default_poll_interval_ms"] | next.tx_mqtt_remote_default_poll_interval_ms;
  next.rx_push_on_change_enabled = parseBoolField(doc["rx_push_on_change_enabled"], next.rx_push_on_change_enabled);
  next.rx_push_min_interval_ms = doc["rx_push_min_interval_ms"] | next.rx_push_min_interval_ms;
  next.tx_input_lora_control_enabled = parseBoolField(doc["tx_input_lora_control_enabled"], next.tx_input_lora_control_enabled);
  next.wifi_sta_ssid = String(static_cast<const char *>(doc["wifi_sta_ssid"] | next.wifi_sta_ssid.c_str()));
  next.wifi_sta_password = String(static_cast<const char *>(doc["wifi_sta_password"] | next.wifi_sta_password.c_str()));
  const String postedLanHost = String(static_cast<const char *>(doc["lan_hostname"] | next.lan_hostname.c_str()));
  const bool wasDefaultHostname = (prev.lan_hostname == oldDefaultHost) || (prev.lan_hostname == oldLegacyDefaultHost) ||
                                  (prev.lan_hostname == oldLegacyRoleTxHost) || (prev.lan_hostname == oldLegacyRoleRxHost);
  if (wasDefaultHostname && (postedLanHost == oldDefaultHost || postedLanHost == oldLegacyDefaultHost ||
                             postedLanHost == oldLegacyRoleTxHost || postedLanHost == oldLegacyRoleRxHost)) {
    next.lan_hostname = config_->defaultLanHostnameForRole(next.role_tx);
  } else {
    next.lan_hostname = postedLanHost;
  }
  next.fleet_passphrase = String(static_cast<const char *>(doc["fleet_passphrase"] | next.fleet_passphrase.c_str()));
  next.ap_always_on = parseBoolField(doc["ap_always_on"], next.ap_always_on);
  next.mqtt_enabled = parseBoolField(doc["mqtt_enabled"], next.mqtt_enabled);
  next.mqtt_host = String(static_cast<const char *>(doc["mqtt_host"] | next.mqtt_host.c_str()));
  next.mqtt_port = static_cast<uint16_t>(doc["mqtt_port"] | next.mqtt_port);
  next.mqtt_user = String(static_cast<const char *>(doc["mqtt_user"] | next.mqtt_user.c_str()));
  next.mqtt_password = String(static_cast<const char *>(doc["mqtt_password"] | next.mqtt_password.c_str()));
  next.mqtt_topic_root = String(static_cast<const char *>(doc["mqtt_topic_root"] | next.mqtt_topic_root.c_str()));
  next.sensor_temp_enabled = parseBoolField(doc["sensor_temp_enabled"], next.sensor_temp_enabled);
  next.sensor_temp_pin = 0;
  next.sensor_temp_interval_s = static_cast<uint16_t>(doc["sensor_temp_interval_s"] | next.sensor_temp_interval_s);

  String newAdmin = String(static_cast<const char *>(doc["admin_password"] | next.admin_password.c_str()));
  if (newAdmin.length() >= 8) {
    next.admin_password = newAdmin;
  }

  if (next.local_address < 1) next.local_address = 1;
  if (next.local_address > 254) next.local_address = 254;
  if (next.remote_address < 1) next.remote_address = 1;
  if (next.remote_address > 254) next.remote_address = 254;
  next.fleet_passphrase.trim();
  const bool hasFleetPassphraseField = !doc["fleet_passphrase"].isNull();
  const bool allowDefaultDeploymentKey = parseBoolField(doc["allow_default_deployment_key"], false);
  if (hasFleetPassphraseField && next.fleet_passphrase.length() < kMinDeploymentKeyLen) {
    server_.send(400, "text/plain",
                 String("deployment key too short (min ") + String(static_cast<unsigned>(kMinDeploymentKeyLen)) +
                     " chars)");
    return;
  }
  if (hasFleetPassphraseField && !allowDefaultDeploymentKey && isDefaultDeploymentKey(next.fleet_passphrase)) {
    server_.send(400, "text/plain", "deployment key cannot be default; set unique key");
    return;
  }
  if (hasFleetPassphraseField && !isDefaultDeploymentKey(next.fleet_passphrase)) {
    next.fleet_setup_prompt_dismissed = true;
  }
  if (next.lora_frequency_hz < kMinFrequencyHz || next.lora_frequency_hz > kMaxFrequencyHz) {
    next.lora_frequency_hz = kDefaultFrequencyHz;
  }
  if (next.heartbeat_ms < kMinHeartbeatMs) next.heartbeat_ms = kMinHeartbeatMs;
  if (next.heartbeat_ms > kMaxHeartbeatMs) next.heartbeat_ms = kMaxHeartbeatMs;
  if (next.ack_timeout_ms < kMinAckTimeoutMs) next.ack_timeout_ms = kMinAckTimeoutMs;
  if (next.ack_timeout_ms > kMaxAckTimeoutMs) next.ack_timeout_ms = kMaxAckTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms < kMinMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMinMqttRemoteRetryTimeoutMs;
  if (next.mqtt_remote_retry_timeout_ms > kMaxMqttRemoteRetryTimeoutMs) next.mqtt_remote_retry_timeout_ms = kMaxMqttRemoteRetryTimeoutMs;
  if (next.tx_mqtt_remote_default_poll_interval_ms < kMinTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMinTxPollDefaultIntervalMs;
  }
  if (next.tx_mqtt_remote_default_poll_interval_ms > kMaxTxPollDefaultIntervalMs) {
    next.tx_mqtt_remote_default_poll_interval_ms = kMaxTxPollDefaultIntervalMs;
  }
  if (next.rx_push_min_interval_ms < kMinRxPushIntervalMs) next.rx_push_min_interval_ms = kMinRxPushIntervalMs;
  if (next.rx_push_min_interval_ms > kMaxRxPushIntervalMs) next.rx_push_min_interval_ms = kMaxRxPushIntervalMs;
  if (next.mqtt_port == 0) next.mqtt_port = 1883;
  if (next.mqtt_topic_root.length() == 0) next.mqtt_topic_root = "lora";
  next.sensor_temp_pin = 0;
  if (next.sensor_temp_interval_s < 2) next.sensor_temp_interval_s = 2;
  if (next.sensor_temp_interval_s > 300) next.sensor_temp_interval_s = 300;
  next.audit_last_saved_by = "admin";
  next.audit_last_saved_ms = millis();

  const bool networkChanged = (next.wifi_sta_ssid != prevStaSsid) ||
                              (next.wifi_sta_password != prevStaPassword) ||
                              (next.lan_hostname != prevLanHost) ||
                              (next.ap_always_on != prevApAlwaysOn);
  const bool otaAuthChanged = (next.admin_password != prevAdminPassword);

  cfg = next;
  if (!config_->save()) {
    cfg = prev;
    server_.send(500, "text/plain", "save failed");
    return;
  }

  server_.send(200, "text/plain", "saved");
  if (on_apply_) on_apply_(networkChanged, otaAuthChanged);
}

void WebConsole::handleExportSettings() {
  if (!requireAuth(true)) return;
  server_.sendHeader("Content-Disposition", "attachment; filename=lrs-config.json");
  DynamicJsonDocument doc(2048);
  auto &cfg = config_->settings();
  doc["role_tx"] = cfg.role_tx;
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["lora_frequency_hz"] = cfg.lora_frequency_hz;
  doc["lora_tx_power"] = cfg.lora_tx_power;
  doc["lora_spreading_factor"] = cfg.lora_spreading_factor;
  doc["lora_bandwidth_hz"] = cfg.lora_bandwidth_hz;
  doc["lora_coding_rate"] = cfg.lora_coding_rate;
  doc["heartbeat_ms"] = cfg.heartbeat_ms;
  doc["ack_timeout_ms"] = cfg.ack_timeout_ms;
  doc["mqtt_remote_retry_timeout_ms"] = cfg.mqtt_remote_retry_timeout_ms;
  doc["tx_mqtt_remote_polling_enabled"] = cfg.tx_mqtt_remote_polling_enabled;
  doc["tx_mqtt_remote_default_poll_interval_ms"] = cfg.tx_mqtt_remote_default_poll_interval_ms;
  doc["rx_push_on_change_enabled"] = cfg.rx_push_on_change_enabled;
  doc["rx_push_min_interval_ms"] = cfg.rx_push_min_interval_ms;
  doc["tx_input_lora_control_enabled"] = cfg.tx_input_lora_control_enabled;
  doc["wifi_sta_ssid"] = cfg.wifi_sta_ssid;
  doc["wifi_sta_password"] = cfg.wifi_sta_password;
  doc["lan_hostname"] = cfg.lan_hostname;
  doc["fleet_passphrase"] = cfg.fleet_passphrase;
  doc["admin_password"] = cfg.admin_password;
  doc["ap_always_on"] = cfg.ap_always_on;
  doc["mqtt_enabled"] = cfg.mqtt_enabled;
  doc["mqtt_host"] = cfg.mqtt_host;
  doc["mqtt_port"] = cfg.mqtt_port;
  doc["mqtt_user"] = cfg.mqtt_user;
  doc["mqtt_password"] = cfg.mqtt_password;
  doc["mqtt_topic_root"] = cfg.mqtt_topic_root;
  doc["sensor_temp_enabled"] = cfg.sensor_temp_enabled;
  doc["sensor_temp_pin"] = cfg.sensor_temp_pin;
  doc["sensor_temp_interval_s"] = cfg.sensor_temp_interval_s;
  String out;
  serializeJsonPretty(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleImportSettings() { handlePostSettings(); }

void WebConsole::handleDiagnostics() {
  DynamicJsonDocument doc(1024);
  uint32_t loraTx = 0;
  uint32_t ackOk = 0;
  uint32_t ackTimeout = 0;
  uint32_t replayDrop = 0;
  uint32_t wifiConnectAttempts = 0;
  uint32_t wifiConnectFail = 0;
  uint32_t wifiDisconnects = 0;

  logs_->forEachEntry([&](const LogItem &e) {
    if (strcmp(e.event, "tx_packet") == 0) loraTx++;
    if (strcmp(e.event, "tx_ack") == 0) ackOk++;
    if (strcmp(e.event, "tx_ack_timeout") == 0) ackTimeout++;
    if (strcmp(e.event, "rx_replay_drop") == 0) replayDrop++;
    if (strcmp(e.event, "sta_connect_start") == 0) wifiConnectAttempts++;
    if (strcmp(e.event, "sta_connect_failed_fallback_ap") == 0) wifiConnectFail++;
    if (strcmp(e.event, "sta_disconnected") == 0) wifiDisconnects++;
  });

  auto &cfg = config_->settings();
  const wl_status_t st = WiFi.status();
  doc["role"] = cfg.role_tx ? "tx" : "rx";
  doc["local_address"] = cfg.local_address;
  doc["remote_address"] = cfg.remote_address;
  doc["fw_display"] = String(LRS_FW_VERSION) + " (" + String(LRS_GIT_SHA) + (LRS_GIT_DIRTY == 0 ? "" : ", dirty") + ")";
  doc["build_date"] = __DATE__;
  doc["build_time"] = __TIME__;
  doc["uptime_ms"] = millis();
  doc["free_heap_bytes"] = ESP.getFreeHeap();
  doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  doc["chip_id"] = config_->chipIdHex();
  doc["flash_real_size"] = ESP.getFlashChipRealSize();
  doc["flash_ide_size"] = ESP.getFlashChipSize();
  doc["sdk_version"] = ESP.getSdkVersion();
  doc["core_version"] = ESP.getCoreVersion();
  doc["lora_tx_packets"] = loraTx;
  doc["ack_ok"] = ackOk;
  doc["ack_timeout"] = ackTimeout;
  doc["replay_drop"] = replayDrop;
  doc["wifi_connect_attempts"] = wifiConnectAttempts;
  doc["wifi_connect_fail"] = wifiConnectFail;
  doc["wifi_disconnects"] = wifiDisconnects;
  doc["sta_status_code"] = static_cast<int>(st);
  doc["sta_status_text"] = wifiStatusText(st);
  doc["audit_last_saved_by"] = cfg.audit_last_saved_by;
  doc["audit_last_saved_ms"] = cfg.audit_last_saved_ms;
  doc["audit_last_reboot_reason"] = cfg.audit_last_reboot_reason;
  doc["audit_last_reboot_ms"] = cfg.audit_last_reboot_ms;
  doc["audit_boot_count"] = cfg.audit_boot_count;
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleTestSta() {
  if (!requireAuth(true)) return;
  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }

  const String ssid = String(static_cast<const char *>(body["wifi_sta_ssid"] | config_->settings().wifi_sta_ssid.c_str()));
  const String pass = String(static_cast<const char *>(body["wifi_sta_password"] | config_->settings().wifi_sta_password.c_str()));
  if (ssid.length() == 0) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"ssid required\"}");
    return;
  }

  const auto &cfg = config_->settings();
  const bool alreadyConnectedSameSsid = WiFi.isConnected() && WiFi.SSID() == ssid;
  const bool sameAsConfigured = (ssid == cfg.wifi_sta_ssid) && (pass == cfg.wifi_sta_password);
  if (alreadyConnectedSameSsid && sameAsConfigured) {
    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    doc["status_code"] = static_cast<int>(WL_CONNECTED);
    doc["status_text"] = wifiStatusText(WL_CONNECTED);
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    String out;
    serializeJson(doc, out);
    server_.send(200, "application/json", out);
    return;
  }

  WiFi.begin(ssid.c_str(), pass.c_str());
  wl_status_t st = WL_IDLE_STATUS;
  for (int i = 0; i < 120; i++) {
    delay(100);
    yield();
    st = WiFi.status();
    if (st == WL_CONNECTED) break;
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) break;
  }

  DynamicJsonDocument doc(256);
  const bool ok = (st == WL_CONNECTED);
  doc["ok"] = ok;
  doc["status_code"] = static_cast<int>(st);
  doc["status_text"] = wifiStatusText(st);
  if (ok) {
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  }

  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);

  // If test credentials differ from persisted settings, restore configured STA
  // after replying so the HTTP response has a chance to reach the browser.
  if (!sameAsConfigured) {
    delay(80);
    WiFi.disconnect();
    delay(20);
    if (cfg.wifi_sta_ssid.length() > 0) {
      WiFi.begin(cfg.wifi_sta_ssid.c_str(), cfg.wifi_sta_password.c_str());
    }
  }
}

void WebConsole::handleProvisionFleetWifi() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }

  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  const String ssid = String(static_cast<const char *>(body["wifi_sta_ssid"] | config_->settings().wifi_sta_ssid.c_str()));
  const String pass = String(static_cast<const char *>(body["wifi_sta_password"] | config_->settings().wifi_sta_password.c_str()));
  if (ssid.length() == 0) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
    return;
  }
  if (ssid.length() > 32 || pass.length() > 64) {
    sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"credentials_too_long\"}");
    return;
  }
  if (isDefaultDeploymentKey(config_->settings().fleet_passphrase)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"fleet_key_default\"}");
    return;
  }

  const uint32_t cooldownRemainingMs = sm_->fleetWifiProvisionCooldownRemainingMs();
  if (cooldownRemainingMs > 0) {
    DynamicJsonDocument cooldown(160);
    cooldown["ok"] = false;
    cooldown["error"] = "cooldown_active";
    cooldown["retry_after_ms"] = cooldownRemainingMs;
    cooldown["retry_after_s"] = (cooldownRemainingMs + 999U) / 1000U;
    String out;
    serializeJson(cooldown, out);
    sendTracked(429, "application/json", out);
    return;
  }

  if (!sm_->sendFleetWifiProvision(ssid, pass)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"send_failed\"}");
    return;
  }

  const size_t totalLen = static_cast<size_t>(ssid.length() + pass.length());
  const size_t chunks = (totalLen + 6U) / 7U;
  DynamicJsonDocument out(128);
  out["ok"] = true;
  out["packets"] = static_cast<uint32_t>(chunks + 2U);  // start + chunks + commit
  String json;
  serializeJson(out, json);
  sendTracked(200, "application/json", json);
  LRS_LOGI(API,
           "event=fleet_wifi_provision_tx ssid=%s password=%s packets=%lu",
           ssid.c_str(),
           lrslog::maskSecret(pass).c_str(),
           static_cast<unsigned long>(chunks + 2U));
}

void WebConsole::handleProvisioningStatus() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  const uint32_t heapFree = lrslog::heapFree();
  const uint32_t maxBlock = lrslog::heapMaxFreeBlock();
  const bool veryLowHeap =
      (heapFree < kApiProvStatusCompactFreeBytes || maxBlock < kApiProvStatusCompactMaxBlockBytes);
  const bool lowHeapForFull =
      (heapFree < kApiLowHeapRejectFreeBytes || maxBlock < kApiLowHeapRejectMaxBlockBytes);
  if (lowHeapForFull) {
    if (veryLowHeap &&
        rejectApiIfLowHeap("/api/provisioning/status",
                           kApiProvStatusCompactFreeBytes,
                           kApiProvStatusCompactMaxBlockBytes)) {
      return;
    }
    StaticJsonDocument<1536> doc;
    doc["ok"] = true;
    ProvisioningSessionSnapshot sess{};
    sm_->provisioningSession(sess);
    const size_t totalDevices = sm_->provisioningDeviceCount();
    const size_t maxCompactRows = 8;
    const size_t returnedDevices = (totalDevices < maxCompactRows) ? totalDevices : maxCompactRows;
    JsonObject s = doc.createNestedObject("session");
    s["active"] = sess.active;
    s["state"] = provisioningSessionStateText(sess.state);
    s["session_nonce"] = sess.session_nonce;
    s["estimated_count"] = sess.estimated_count;
    s["started_ms"] = sess.started_ms;
    s["phase_deadline_ms"] = sess.phase_deadline_ms;
    s["retry_enabled"] = sess.retry_enabled;
    s["retry_used"] = sess.retry_used;
    s["paused_normal_tx"] = sess.paused_normal_tx;
    s["discovered_count"] = sess.discovered_count;
    s["selected_count"] = sess.selected_count;
    s["conflict_count"] = sess.conflict_count;
    s["verified_count"] = sess.verified_count;
    s["failed_count"] = sess.failed_count;
    s["now_ms"] = millis();
    s["devices_total"] = totalDevices;
    s["devices_returned"] = returnedDevices;
    s["devices_truncated"] = (returnedDevices < totalDevices);
    s["compact"] = true;
    JsonArray arr = doc.createNestedArray("devices");
    for (size_t i = 0; i < returnedDevices; ++i) {
      ProvisioningDeviceSnapshot d{};
      if (!sm_->provisioningDeviceByIndex(i, d)) continue;
      JsonObject o = arr.createNestedObject();
      char chipHex[11];
      snprintf(chipHex, sizeof(chipHex), "0x%08lX", static_cast<unsigned long>(d.chip_id));
      o["chip_id_hex"] = chipHex;
      o["current_address"] = d.current_address;
      o["assigned_address"] = d.assigned_address;
      o["fw_major"] = d.fw_major;
      o["fw_minor"] = d.fw_minor;
      o["fw_patch"] = d.fw_patch;
      o["rssi"] = d.rssi;
      o["state"] = provisioningDeviceStateText(d.state);
      o["address_conflict"] = d.address_conflict;
    }

    if (last_logged_prov_state_ != static_cast<uint8_t>(sess.state)) {
      last_logged_prov_state_ = static_cast<uint8_t>(sess.state);
      LRS_LOGI(API,
               "event=provisioning_phase state=%s discovered=%u conflicts=%u verified=%u failed=%u",
               provisioningSessionStateText(sess.state),
               static_cast<unsigned>(sess.discovered_count),
               static_cast<unsigned>(sess.conflict_count),
               static_cast<unsigned>(sess.verified_count),
               static_cast<unsigned>(sess.failed_count));
    }

    const size_t len = measureJson(doc);
    server_.setContentLength(len);
    markResponseStatus(200);
    server_.send(200, "application/json", "");
    serializeJson(doc, server_.client());
    LRS_LOGW(API,
             "event=provisioning_status_compact heap_free=%lu heap_frag=%u max_free_block=%lu",
             static_cast<unsigned long>(heapFree),
             static_cast<unsigned>(lrslog::heapFragPercent()),
             static_cast<unsigned long>(maxBlock));
    return;
  }
  const size_t totalDevices = sm_->provisioningDeviceCount();
  const size_t maxDevicesReturned = 64;
  const size_t returnedDevices = (totalDevices < maxDevicesReturned) ? totalDevices : maxDevicesReturned;
  size_t docCap = 1024U + (returnedDevices * 192U);
  if (docCap < 2048U) docCap = 2048U;
  if (docCap > 12288U) docCap = 12288U;
  DynamicJsonDocument doc(docCap);
  doc["ok"] = true;
  ProvisioningSessionSnapshot sess{};
  sm_->provisioningSession(sess);
  JsonObject s = doc.createNestedObject("session");
  s["active"] = sess.active;
  s["state"] = provisioningSessionStateText(sess.state);
  s["session_nonce"] = sess.session_nonce;
  s["estimated_count"] = sess.estimated_count;
  s["started_ms"] = sess.started_ms;
  s["phase_deadline_ms"] = sess.phase_deadline_ms;
  s["retry_enabled"] = sess.retry_enabled;
  s["retry_used"] = sess.retry_used;
  s["paused_normal_tx"] = sess.paused_normal_tx;
  s["discovered_count"] = sess.discovered_count;
  s["selected_count"] = sess.selected_count;
  s["conflict_count"] = sess.conflict_count;
  s["verified_count"] = sess.verified_count;
  s["failed_count"] = sess.failed_count;
  s["now_ms"] = millis();
  s["devices_total"] = totalDevices;
  s["devices_returned"] = returnedDevices;
  s["devices_truncated"] = (returnedDevices < totalDevices);
  if (last_logged_prov_state_ != static_cast<uint8_t>(sess.state)) {
    last_logged_prov_state_ = static_cast<uint8_t>(sess.state);
    LRS_LOGI(API,
             "event=provisioning_phase state=%s discovered=%u conflicts=%u verified=%u failed=%u",
             provisioningSessionStateText(sess.state),
             static_cast<unsigned>(sess.discovered_count),
             static_cast<unsigned>(sess.conflict_count),
             static_cast<unsigned>(sess.verified_count),
             static_cast<unsigned>(sess.failed_count));
  }

  JsonArray arr = doc.createNestedArray("devices");
  for (size_t i = 0; i < returnedDevices; ++i) {
    ProvisioningDeviceSnapshot d{};
    if (!sm_->provisioningDeviceByIndex(i, d)) continue;
    JsonObject o = arr.createNestedObject();
    o["chip_id"] = d.chip_id;
    char chipHex[11];
    snprintf(chipHex, sizeof(chipHex), "0x%08lX", static_cast<unsigned long>(d.chip_id));
    o["chip_id_hex"] = chipHex;
    o["current_address"] = d.current_address;
    o["assigned_address"] = d.assigned_address;
    o["role"] = d.role_tx ? "tx" : "rx";
    o["fw_major"] = d.fw_major;
    o["fw_minor"] = d.fw_minor;
    o["fw_patch"] = d.fw_patch;
    o["rssi"] = d.rssi;
    o["selected"] = d.selected;
    o["address_conflict"] = d.address_conflict;
    o["state"] = provisioningDeviceStateText(d.state);
  }
  const size_t len = measureJson(doc);
  server_.setContentLength(len);
  markResponseStatus(200);
  server_.send(200, "application/json", "");
  serializeJson(doc, server_.client());
}

void WebConsole::handleProvisioningStart() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  DynamicJsonDocument body(256);
  if (server_.arg("plain").length() > 0) {
    auto err = deserializeJson(body, server_.arg("plain"));
    if (err) {
      sendTracked(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
      return;
    }
  }
  uint16_t estimated = 10;
  if (!body["estimated_count"].isNull()) {
    int v = body["estimated_count"].as<int>();
    if (v < 1) v = 1;
    if (v > 250) v = 250;
    estimated = static_cast<uint16_t>(v);
  }
  const bool retryOnce = parseBoolField(body["retry_once"], true);
  if (!sm_->provisioningStartDiscovery(estimated, retryOnce)) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"start_failed\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_start estimated=%u retry_once=%u heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned>(estimated),
           retryOnce ? 1U : 0U,
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningProvisionAll() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  if (!sm_->provisioningStartProvisionAll()) {
    sendTracked(409, "application/json", "{\"ok\":false,\"error\":\"invalid_state\"}");
    return;
  }
  LRS_LOGI(API,
           "event=provisioning_provision_all heap_free=%lu heap_frag=%u max_free_block=%lu",
           static_cast<unsigned long>(lrslog::heapFree()),
           static_cast<unsigned>(lrslog::heapFragPercent()),
           static_cast<unsigned long>(lrslog::heapMaxFreeBlock()));
  sendTracked(200, "application/json", "{\"ok\":true,\"started\":true}");
}

void WebConsole::handleProvisioningCancel() {
  if (!requireAuth(true)) return;
  if (sm_ == nullptr) {
    sendTracked(500, "application/json", "{\"ok\":false,\"error\":\"state_machine_unavailable\"}");
    return;
  }
  sm_->provisioningCancel();
  sendTracked(200, "application/json", "{\"ok\":true}");
  LRS_LOGI(API, "event=provisioning_cancel");
}

void WebConsole::handleTestMqtt() {
  if (!requireAuth(true)) return;
  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
    return;
  }
  const String host = String(static_cast<const char *>(body["mqtt_host"] | config_->settings().mqtt_host.c_str()));
  uint16_t port = config_->settings().mqtt_port;
  if (!body["mqtt_port"].isNull()) {
    long parsed = -1;
    if (body["mqtt_port"].is<uint16_t>()) {
      parsed = static_cast<long>(body["mqtt_port"].as<uint16_t>());
    } else {
      const String raw = String(static_cast<const char *>(body["mqtt_port"] | ""));
      if (raw.length() > 0) parsed = raw.toInt();
    }
    if (parsed < 1 || parsed > 65535) {
      server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid mqtt_port\"}");
      return;
    }
    port = static_cast<uint16_t>(parsed);
  }
  const String user = String(static_cast<const char *>(body["mqtt_user"] | config_->settings().mqtt_user.c_str()));
  const String pass = String(static_cast<const char *>(body["mqtt_password"] | config_->settings().mqtt_password.c_str()));
  if (!WiFi.isConnected()) {
    server_.send(200, "application/json", "{\"ok\":false,\"state\":-2}");
    return;
  }
  WiFiClient client;
  PubSubClient mqtt(client);
  mqtt.setServer(host.c_str(), port);
  mqtt.setSocketTimeout(2);
  const String clientId = "lrs-test-" + config_->chipIdHex();
  bool ok = false;
  if (user.length() > 0) {
    ok = mqtt.connect(clientId.c_str(), user.c_str(), pass.c_str());
  } else {
    ok = mqtt.connect(clientId.c_str());
  }
  DynamicJsonDocument doc(256);
  doc["ok"] = ok;
  doc["state"] = mqtt.state();
  doc["host"] = host;
  doc["port"] = port;
  if (ok) mqtt.disconnect();
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void WebConsole::handleOtaUpload() {
  if (!requireAuth(true)) return;
  if (!ota_upload_ok_) {
    server_.send(500, "text/plain", ota_upload_error_.length() ? ota_upload_error_ : "OTA failed");
    return;
  }
  server_.send(200, "text/plain", "ok");
  delay(150);
  ESP.restart();
}

void WebConsole::handleOtaUploadChunk() {
  if (!requireAuth(true)) return;
  HTTPUpload &upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    ota_upload_ok_ = false;
    ota_upload_error_ = "";
    const uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace)) {
      ota_upload_error_ = "Cannot start OTA";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      ota_upload_error_ = "Write failed";
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      ota_upload_error_ = "Finalize failed";
      ota_upload_ok_ = false;
      return;
    }
    ota_upload_ok_ = true;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    ota_upload_error_ = "Upload aborted";
    ota_upload_ok_ = false;
  }
}

void WebConsole::handleLogsCsv() {
  if (!requireAuth()) return;
  server_.sendHeader("Content-Disposition", "attachment; filename=lrs-logs.csv");
  server_.send(200, "text/csv", logs_->asCsv());
}

void WebConsole::handleLogsText() {
  if (!requireAuth()) return;
  server_.send(200, "text/plain", logs_->asText());
}

void WebConsole::handleFactoryReset() {
  if (!requireAuth(true)) return;

  DynamicJsonDocument body(512);
  auto err = deserializeJson(body, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }

  auto &cfg = config_->settings();
  const String postedPassword = String(static_cast<const char *>(body["admin_password"] | ""));
  if (postedPassword != cfg.admin_password) {
    server_.send(403, "application/json", "{\"ok\":false,\"error\":\"invalid_password\"}");
    return;
  }
  const bool keepSharedFleetKey = parseBoolField(body["keep_shared_fleet_key"], false);
  const bool keepWifiCredentials = parseBoolField(body["keep_wifi_credentials"], false);

  if (!config_->factoryReset(keepSharedFleetKey, keepWifiCredentials)) {
    server_.send(500, "application/json", "{\"ok\":false,\"error\":\"reset_save_failed\"}");
    return;
  }

  clearSession();
  server_.sendHeader("Set-Cookie", "lrs_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
  server_.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(120);
  ESP.restart();
}

void WebConsole::handleReboot() {
  if (!requireAuth(true)) return;
  auto &cfg = config_->settings();
  cfg.audit_last_reboot_reason = "web_reboot";
  cfg.audit_last_reboot_ms = millis();
  config_->save();
  server_.send(200, "text/plain", "rebooting");
  delay(150);
  ESP.restart();
}
