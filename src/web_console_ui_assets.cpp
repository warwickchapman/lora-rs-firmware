#include "web_console_ui_assets.h"
#include "feature_flags.h"

const char kLoginHtml_Part1[] PROGMEM = R"HTML(
<!doctype html><html><head>
)HTML";

const char kLoginHtml_Part2[] PROGMEM =
    R"HTML(<meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Login</title>
<style>
:root{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%230f172a"/><circle cx="80" cy="20" r="50" fill="%234c1d95" opacity="0.4" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%231e3a8a" opacity="0.4" filter="blur(30px)"/></svg>');--card:rgba(15,23,42,0.4);--txt:#f8fafc;--muted:#cbd5e1;--border:rgba(255,255,255,0.1);--field:rgba(255,255,255,0.03);--btn:linear-gradient(135deg,#6366f1,#8b5cf6);--btn-hover:linear-gradient(135deg,#4f46e5,#7c3aed);--focus:rgba(139,92,246,0.5);--glass-shadow:0 4px 16px 0 rgba(0,0,0,0.2);--glass-border:1px solid rgba(255,255,255,0.1);--slug-bg:#8b5cf6;--slug-txt:#fff}
body.light{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%23f8fafc"/><circle cx="80" cy="20" r="50" fill="%23c4b5fd" opacity="0.5" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%2393c5fd" opacity="0.5" filter="blur(30px)"/></svg>');--card:rgba(255,255,255,0.4);--txt:#0f172a;--muted:#475569;--border:rgba(255,255,255,0.3);--field:rgba(255,255,255,0.4);--btn:linear-gradient(135deg,#3b82f6,#6366f1);--btn-hover:linear-gradient(135deg,#2563eb,#4f46e5);--focus:rgba(99,102,241,0.5);--glass-shadow:0 4px 16px 0 rgba(31,38,135,0.1);--glass-border:1px solid rgba(255,255,255,0.4);--slug-bg:#3b82f6;--slug-txt:#fff}
body{margin:0;min-height:100vh;display:grid;place-items:center;background:var(--bg);background-size:cover;background-position:center;background-attachment:fixed;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;color:var(--txt);-webkit-text-size-adjust:100%}
.card{width:min(90vw,360px);background:var(--card);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border:var(--glass-border);border-radius:16px;padding:28px;box-shadow:var(--glass-shadow)}
h1{margin:0 0 6px;font-size:1.5rem;font-weight:700;letter-spacing:-0.025em;background:linear-gradient(to right,var(--txt),var(--muted));-webkit-background-clip:text;-webkit-text-fill-color:transparent}
p{margin:0 0 20px;color:var(--muted);font-size:0.9rem;line-height:1.4}
label{display:block;margin:0 0 6px;color:var(--txt);font-size:0.85rem;font-weight:600;letter-spacing:0.01em}
input{box-sizing:border-box;width:100%;padding:10px 14px;border:var(--glass-border);border-radius:8px;font-size:0.95rem;background:var(--field);color:var(--txt);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);transition:all 0.2s ease;outline:none}
input:focus{border-color:rgba(255,255,255,0.3);box-shadow:0 0 0 3px var(--focus), inset 0 0 0 1px rgba(255,255,255,0.1)}
.pass-field{display:flex;align-items:center;gap:10px}
.pass-field input{flex:1 1 auto}
.pass-toggle{width:40px;height:40px;min-width:40px;margin:0;padding:0;border:var(--glass-border);border-radius:10px;background:var(--field);color:var(--txt);font-size:1rem;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);transition:all 0.2s ease}
.pass-toggle:hover{background:rgba(255,255,255,0.1);transform:translateY(-1px)}
body.light .pass-toggle:hover{background:rgba(255,255,255,0.6)}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;background:var(--btn);color:#fff;font-size:0.95rem;font-weight:600;letter-spacing:0.01em;cursor:pointer;transition:all 0.2s ease;box-shadow:0 4px 12px rgba(99,102,241,0.2)}
button:hover{background:var(--btn-hover);transform:translateY(-1px);box-shadow:0 6px 16px rgba(99,102,241,0.3)}
button:active{transform:translateY(1px)}
.msg{margin-top:16px;font-size:0.85rem;min-height:1.2em;text-align:center;font-weight:600;padding:8px;border-radius:6px;transition:all 0.2s;opacity:0}
.msg:not(:empty){opacity:1}
.err{background:rgba(239,68,68,0.1);color:#fca5a5;border:1px solid rgba(239,68,68,0.2)}
.ok{background:rgba(34,197,94,0.1);color:#86efac;border:1px solid rgba(34,197,94,0.2)}
.top{display:flex;justify-content:flex-end;margin-bottom:12px}
.theme-btn{width:auto;margin:0;padding:6px 12px;border:var(--glass-border);border-radius:999px;background:var(--field);color:var(--txt);cursor:pointer;font-size:0.85rem;box-shadow:none;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);transition:all 0.2s ease}
.theme-btn:hover{background:rgba(255,255,255,0.1);transform:translateY(-1px)}
body.light .theme-btn:hover{background:rgba(255,255,255,0.6)}
.slug{display:inline-block;padding:2px 8px;border-radius:6px;background:var(--slug-bg);color:var(--slug-txt);font-size:0.75rem;font-weight:800;text-transform:uppercase;letter-spacing:0.05em;margin-bottom:8px;box-shadow:0 2px 8px rgba(0,0,0,0.2)}
</style></head><body><div class="card">
<div class="top"><button id="themeBtn" class="theme-btn" type="button" onclick="toggleTheme()">☀</button></div>
<div class="slug">)HTML";

const char kLoginHtml_Part3[] PROGMEM = R"HTML(</div>
<h1>LRS Device Console Login</h1>
<p id="hint">Use the device admin password. Username is not required.</p>
<form id="loginForm" autocomplete="on">
<input id="uname" name="username" type="text" autocomplete="username" value="admin" aria-hidden="true" tabindex="-1" style="position:absolute;left:-9999px;width:1px;height:1px;opacity:0;pointer-events:none" />
<label for="pw">Admin password</label>
<div class="pass-field"><input id="pw" name="password" type="password" autocomplete="current-password" placeholder="Enter admin password" /><button class="pass-toggle" type="button" onclick="togglePasswordField('pw',this)" title="Show password" aria-label="Show password">👁</button></div>
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
 if(btn){
  btn.innerText=show?'🙈':'👁';
  btn.title=show?'Hide password':'Show password';
  btn.setAttribute('aria-label', btn.title);
 }
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
<title>LRS Commissioning</title>
<style>
body{margin:0;background:#0f172a;color:#e2e8f0;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:720px;margin:0 auto;padding:20px}
.card{background:rgba(15,23,42,.82);border:1px solid rgba(255,255,255,.12);border-radius:16px;padding:18px}
h1{margin:0 0 8px;font-size:1.45rem}
p{margin:0 0 16px;color:#94a3b8}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
label{display:block;font-size:.85rem;font-weight:700;margin:8px 0 4px}
input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid rgba(255,255,255,.16);border-radius:10px;background:rgba(255,255,255,.04);color:#e2e8f0}
.row{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}
button{border:0;border-radius:10px;padding:10px 14px;background:#4f46e5;color:white;font-weight:700;cursor:pointer}
button.alt{background:rgba(255,255,255,.1)}
.hint{font-size:.82rem;color:#94a3b8;margin-top:4px}
.msg{margin-top:14px;padding:10px;border-radius:10px;display:none}
.msg.show{display:block}
.msg.ok{background:rgba(34,197,94,.14);color:#86efac}
.msg.err{background:rgba(239,68,68,.14);color:#fca5a5}
.section{margin-top:14px;padding-top:10px;border-top:1px solid rgba(255,255,255,.12)}
.check{display:flex;gap:8px;align-items:center;margin-top:8px}
.check input{width:18px;height:18px}
.wifi-list{margin-top:10px;border:1px solid rgba(255,255,255,.12);border-radius:10px;overflow:auto;background:rgba(255,255,255,.03)}
.wifi-table{width:100%;border-collapse:collapse;font-size:.9rem}
.wifi-table th,.wifi-table td{padding:8px 10px;border-bottom:1px solid rgba(255,255,255,.1);text-align:left}
.wifi-table th:last-child,.wifi-table td:last-child{text-align:right;white-space:nowrap}
.wifi-table td:first-child{max-width:46vw;overflow-wrap:anywhere}
.wifi-table button{margin:0;padding:6px 10px;border-radius:8px}
@media(max-width:680px){.grid{grid-template-columns:1fr}}
</style></head><body><div class="wrap"><div class="card">
<h1>Commission device</h1>
<p>Set the installation mode, role, and control settings for first use.</p>

<div class="grid">
<div><label for="install_type">Installation type</label><select id="install_type"><option value="new">New installation</option><option value="join">Join existing installation</option></select></div>
<div><label for="fleet">Fleet key</label><input id="fleet" type="text" placeholder="At least 16 characters" /><div class="row" style="margin-top:8px"><button class="alt" type="button" onclick="applySuggestedFleetKey(true)">Suggest key</button></div></div>
</div>
<div class="hint">Use the same Fleet key on all devices in the installation. Suggested keys are read-aloud friendly.</div>

<div class="section grid">
<div><label for="mode">Mode</label><select id="mode" onchange="syncRoleOptions()"><option value="standalone">Standalone</option><option value="paired" selected>Paired</option><option value="mesh">Mesh</option></select></div>
<div><label for="role">Role</label><select id="role"></select></div>
<div id="paired_input_row" style="grid-column:1/-1">
<div class="check"><input id="input_control_paired_lora_enabled" type="checkbox" checked /><span>Local input drives LoRa control of paired relay</span></div>
<div class="hint" id="paired_input_hint">TX only in paired mode. Disabled when role is Receiver.</div>
</div>
</div>

<div class="section">
<label>MQTT</label>
<div class="check"><input id="mqtt_client_enabled" type="checkbox" /><span>MQTT client enabled</span></div>
<div class="hint" style="margin-top:2px;margin-left:26px">Connects to the MQTT broker and publishes this device state.</div>
<div class="check"><input id="mqtt_control_enabled" type="checkbox" /><span>MQTT control enabled</span></div>
<div class="hint" style="margin-top:2px;margin-left:26px">Enables MQTT command topics so remote systems can control this device.</div>
<div class="hint">When MQTT control is enabled, local automations are disabled.</div>
</div>

<div class="section">
<label>Connect to WiFi</label>
<div class="row" style="margin-top:6px"><button id="wifiScanBtn" class="alt" type="button" onclick="scanSetupWifi()">Scan</button></div>
<div id="wifi_scan_list_setup" class="wifi-list" style="display:none"></div>
<div class="grid" style="margin-top:10px">
<div><label for="wifi_sta_ssid">WiFi SSID (optional)</label><input id="wifi_sta_ssid" /></div>
<div><label for="wifi_sta_password">WiFi password (optional)</label><input id="wifi_sta_password" type="password" /></div>
</div>
</div>

<div class="row">
<button id="saveBtn" type="button" onclick="saveCommissioning()">Save commissioning</button>
<button id="skipBtn" class="alt" type="button" onclick="skipForNow()">Skip for now</button>
</div>
<div id="msg" class="msg"></div>
</div></div><script>
const msg=document.getElementById('msg');
const saveBtn=document.getElementById('saveBtn');
const skipBtn=document.getElementById('skipBtn');
const setupWifiScanBtn=document.getElementById('wifiScanBtn');
const setupWifiScanHost=document.getElementById('wifi_scan_list_setup');
const modeEl=document.getElementById('mode');
const roleEl=document.getElementById('role');
const installTypeEl=document.getElementById('install_type');
const fleetKeyEl=document.getElementById('fleet');
let setupWifiScanInFlight=false;
const READABLE_KEY_CONSONANTS='bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS='aeiou';
function setBusy(b){ saveBtn.disabled=b; skipBtn.disabled=b; }
function showMsg(text, ok){
 msg.className = `msg show ${ok ? 'ok' : 'err'}`;
 msg.innerText = text || '';
}
function sleep(ms){ return new Promise(resolve=>setTimeout(resolve, ms)); }
function escapeHtml(v){
 return String(v==null?'':v)
  .replace(/&/g,'&amp;')
  .replace(/</g,'&lt;')
  .replace(/>/g,'&gt;')
  .replace(/\"/g,'&quot;')
  .replace(/'/g,'&#39;');
}
function wifiBarsHtml(rssi){
 const dbm=Number(rssi||-120);
 const lv = dbm >= -60 ? 4 : dbm >= -70 ? 3 : dbm >= -80 ? 2 : dbm >= -90 ? 1 : 0;
 let bars='';
 for(let i=1;i<=4;i++){
  const on=i<=lv;
  const h=(3+i*2);
  bars += `<i style="display:inline-block;width:3px;height:${h}px;margin-right:2px;border-radius:2px;background:${on?'#4ade80':'rgba(255,255,255,.22)'}"></i>`;
 }
 return `<span aria-hidden="true" style="display:inline-flex;align-items:flex-end;vertical-align:-2px;margin-right:6px;height:14px">${bars}</span>`;
}
async function scanSetupWifi(){
 if(setupWifiScanInFlight || !setupWifiScanHost) return;
 setupWifiScanInFlight=true;
 setupWifiScanHost.style.display='';
 setupWifiScanHost.innerHTML='Scanning...';
 if(setupWifiScanBtn){ setupWifiScanBtn.disabled=true; setupWifiScanBtn.innerText='Scanning...'; }
 try{
  const start=Date.now();
  const timeoutMs=15000;
  let out=null;
  while((Date.now()-start) < timeoutMs){
   const res=await fetch('/api/wifi/scan',{cache:'no-store'});
   out=await res.json().catch(()=>null);
   if(!out || !res.ok){ setupWifiScanHost.innerHTML='Scan failed'; return; }
   if(String(out.status||'')==='ready') break;
   await sleep(500);
  }
  if(!out || String(out.status||'')!=='ready'){ setupWifiScanHost.innerHTML='Scan timed out'; return; }
  const nets=Array.isArray(out.networks) ? out.networks.slice() : [];
  if(!nets.length){ setupWifiScanHost.innerHTML='No SSIDs found'; return; }
  nets.sort((a,b)=>Number(b.rssi||-999)-Number(a.rssi||-999));
  setupWifiScanHost.innerHTML='<table class="wifi-table"><thead><tr><th>SSID</th><th>Signal</th><th></th></tr></thead><tbody></tbody></table>';
  const tbody=setupWifiScanHost.querySelector('tbody');
  nets.forEach(n=>{
   const tr=document.createElement('tr');
   tr.innerHTML=`<td>${escapeHtml(n.ssid)}</td><td>${wifiBarsHtml(n.rssi)}${escapeHtml(n.rssi)} dBm</td><td><button type="button" data-ssid="${escapeHtml(n.ssid)}">Use</button></td>`;
   tbody.appendChild(tr);
  });
  setupWifiScanHost.querySelectorAll('button[data-ssid]').forEach(btn=>{
   btn.addEventListener('click',()=>{
    const ssid=btn.getAttribute('data-ssid') || '';
    const ssidEl=document.getElementById('wifi_sta_ssid');
    const passEl=document.getElementById('wifi_sta_password');
    if(ssidEl) ssidEl.value=ssid;
    if(passEl){ passEl.focus(); passEl.select(); }
   });
  });
 }catch(e){
  setupWifiScanHost.innerHTML='Scan failed';
 }finally{
  setupWifiScanInFlight=false;
  if(setupWifiScanBtn){ setupWifiScanBtn.disabled=false; setupWifiScanBtn.innerText='Scan'; }
 }
}
function randomIndex(max){
 if(max <= 1) return 0;
 try{
  if(window.crypto && window.crypto.getRandomValues){
   const arr=new Uint32Array(1);
   const lim=Math.floor(0x100000000/max)*max;
   let v=0;
   do{
    window.crypto.getRandomValues(arr);
    v=arr[0];
   }while(v>=lim);
   return v%max;
  }
 }catch(e){}
 return Math.floor(Math.random()*max);
}
function generateReadableFleetKey(){
 const c=READABLE_KEY_CONSONANTS;
 const v=READABLE_KEY_VOWELS;
 const groups=[];
 for(let i=0;i<4;i++){
  const part=
   c[randomIndex(c.length)] +
   v[randomIndex(v.length)] +
   c[randomIndex(c.length)] +
   v[randomIndex(v.length)] +
   c[randomIndex(c.length)];
  groups.push(part);
 }
 return groups.join('-');
}
function applySuggestedFleetKey(force){
 if(!fleetKeyEl) return;
 const install=String((installTypeEl && installTypeEl.value) || 'new');
 const current=String(fleetKeyEl.value || '').trim();
 if(!force){
  if(install !== 'new') return;
  if(current.length) return;
 }
 fleetKeyEl.value = generateReadableFleetKey();
}
function syncRoleOptions(){
 const mode = String(modeEl.value || 'paired');
 const opts = [];
 if(mode === 'paired'){
  opts.push(['transmitter','Transmitter'], ['receiver','Receiver']);
 }else if(mode === 'mesh'){
  opts.push(['coordinator','Coordinator'], ['node','Node']);
 }else{
  opts.push(['none','None']);
 }
 const prev = roleEl.value;
 roleEl.innerHTML = opts.map(o=>`<option value="${o[0]}">${o[1]}</option>`).join('');
 roleEl.value = opts.some(o=>o[0]===prev) ? prev : opts[0][0];
 const pairedInputRow = document.getElementById('paired_input_row');
 const pairedInputEl = document.getElementById('input_control_paired_lora_enabled');
 const pairedInputHint = document.getElementById('paired_input_hint');
 if(pairedInputRow){
  const pairedMode = mode === 'paired';
  const txRole = roleEl.value === 'transmitter';
  pairedInputRow.style.display = pairedMode ? '' : 'none';
  if(!pairedMode && pairedInputEl){ pairedInputEl.checked = false; }
  if(pairedInputEl){ pairedInputEl.disabled = !(pairedMode && txRole); }
  if(pairedInputHint){ pairedInputHint.style.display = (pairedMode && !txRole) ? '' : 'none'; }
  if(pairedMode && !txRole && pairedInputEl){ pairedInputEl.checked = false; }
 }
}
roleEl.addEventListener('change', syncRoleOptions);
if(installTypeEl){
 installTypeEl.addEventListener('change', ()=>{
  if(String(installTypeEl.value||'new')==='new') applySuggestedFleetKey(false);
 });
}
document.getElementById('mqtt_control_enabled').addEventListener('change', ()=>{
 const control = document.getElementById('mqtt_control_enabled').checked;
 if(control){ document.getElementById('mqtt_client_enabled').checked = true; }
});
async function saveCommissioning(){
 const fleetKey = String(document.getElementById('fleet').value || '').trim();
 if(fleetKey.length < 16){ showMsg('Fleet key must be at least 16 characters.', false); return; }
 if(fleetKey === 'lora-default-passphrase'){ showMsg('Default Fleet key is blocked.', false); return; }
 const body = {
  fleet_passphrase: fleetKey,
  mode: String(modeEl.value || 'paired'),
  role: String(roleEl.value || 'transmitter'),
  mqtt_client_enabled: !!document.getElementById('mqtt_client_enabled').checked,
  mqtt_control_enabled: !!document.getElementById('mqtt_control_enabled').checked,
  input_control_paired_lora_enabled: !!document.getElementById('input_control_paired_lora_enabled').checked,
  wifi_sta_ssid: String(document.getElementById('wifi_sta_ssid').value || ''),
  wifi_sta_password: String(document.getElementById('wifi_sta_password').value || ''),
 };
 if(body.mqtt_control_enabled && !body.mqtt_client_enabled){
  showMsg('MQTT control requires MQTT client enabled.', false);
  return;
 }
 setBusy(true);
 showMsg('Saving...', true);
 try{
  const res=await fetch('/api/setup/commissioning',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const out=await res.json().catch(()=>({}));
  if(!res.ok){ showMsg(out.error || 'Save failed.', false); return; }
  showMsg('Commissioning saved.', true);
  location.href='/';
 }catch(e){
  showMsg(`Save failed: ${e.message}`, false);
 }finally{
  setBusy(false);
 }
}
async function skipForNow(){
 setBusy(true);
 showMsg('Skipping...', true);
 try{
  const res=await fetch('/api/setup/fleet-key',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({skip:true})});
  const out=await res.json().catch(()=>({}));
  if(!res.ok){ showMsg(out.error || 'Skip failed.', false); return; }
  location.href='/';
 }catch(e){
  showMsg(`Skip failed: ${e.message}`, false);
 }finally{
  setBusy(false);
 }
}
syncRoleOptions();
applySuggestedFleetKey(false);
scanSetupWifi().catch(()=>{});
document.getElementById('fleet').focus();
</script></body></html>
)HTML";

const char kIndexLowHeapHtml[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Console Unavailable (Low Memory)</title>
<style>
:root{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%230f172a"/><circle cx="80" cy="20" r="50" fill="%234c1d95" opacity="0.4" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%231e3a8a" opacity="0.4" filter="blur(30px)"/></svg>');--card:rgba(15,23,42,0.4);--txt:#f8fafc;--muted:#cbd5e1;--border:rgba(255,255,255,0.1);--btn:linear-gradient(135deg,#6366f1,#8b5cf6);--btn-hover:linear-gradient(135deg,#4f46e5,#7c3aed);--ok:#4ade80;--warn:#fbbf24;--crit:#f87171;--glass-shadow:0 4px 16px 0 rgba(0,0,0,0.2);--glass-border:1px solid rgba(255,255,255,0.1)}
body{margin:0;background:var(--bg);background-size:cover;background-position:center;background-attachment:fixed;color:var(--txt);font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;-webkit-text-size-adjust:100%}
.wrap{max-width:720px;margin:0 auto;padding:16px}
.card{background:var(--card);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border:var(--glass-border);border-radius:16px;padding:24px;margin-bottom:20px;box-shadow:var(--glass-shadow)}
h1{margin:0 0 12px;font-size:1.5rem;font-weight:700;letter-spacing:-0.025em;background:linear-gradient(to right,var(--txt),var(--muted));-webkit-background-clip:text;-webkit-text-fill-color:transparent} p{margin:0;color:var(--muted);font-size:0.9rem;line-height:1.4}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:20px}
.tile{border:var(--glass-border);border-radius:12px;padding:16px;background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);transition:all 0.2s}
.tile:hover{transform:translateY(-1px);background:rgba(255,255,255,0.08)}
.k{font-size:0.85rem;color:var(--muted);font-weight:600;text-transform:uppercase;letter-spacing:0.02em} .v{font-size:1.15rem;font-weight:700;margin-top:8px}
.row{display:flex;gap:12px;flex-wrap:wrap;margin-top:20px}
button,a.btn{background:var(--btn);color:#fff;border:0;border-radius:8px;padding:10px 14px;text-decoration:none;font-size:0.95rem;font-weight:600;letter-spacing:0.01em;cursor:pointer;transition:all 0.2s;box-shadow:0 4px 12px rgba(99,102,241,0.2)}
button:hover,a.btn:hover{background:var(--btn-hover);transform:translateY(-1px);box-shadow:0 6px 16px rgba(99,102,241,0.3)}
button:active,a.btn:active{transform:translateY(1px)}
a.btn.alt,button.alt{background:rgba(255,255,255,0.05);border:var(--glass-border);color:var(--txt);box-shadow:none;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
a.btn.alt:hover,button.alt:hover{background:rgba(255,255,255,0.1);border-color:rgba(255,255,255,0.2);box-shadow:0 4px 12px rgba(0,0,0,0.1)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace}
.banner{border-left:4px solid var(--warn);background:rgba(245,158,11,0.1);padding:12px 16px;border-radius:8px;color:#fcd34d;font-size:0.9rem;line-height:1.4;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border:1px solid rgba(245,158,11,0.2)}
.metric.ok{border-color:rgba(74,222,128,0.3);background:rgba(74,222,128,0.1)} .metric.warn{border-color:rgba(251,191,36,0.3);background:rgba(251,191,36,0.1)} .metric.crit{border-color:rgba(248,113,113,0.3);background:rgba(248,113,113,0.1)}
.metric.ok .v{color:var(--ok)} .metric.warn .v{color:var(--warn)} .metric.crit .v{color:var(--crit)}
.small{font-size:0.85rem;color:var(--muted);margin-top:6px}
</style></head><body><div class="wrap">
<div class="card">
 <h1>Console Unavailable (Low Memory)</h1>
 <div class="banner" id="notice">Full console was skipped to protect device stability. View metrics below, then retry when memory recovers.</div>
 <div class="row">
  <button onclick="refreshLite()">Refresh metrics</button>
  <button class="alt" onclick="location.href='/?force_full=1'">Retry full console</button>
  <a class="btn alt" href="/login">Login</a>
 </div>
 <div class="grid">
  <div class="tile"><div class="k">Unit</div><div class="v mono" id="unit">lrs-?</div><div class="small mono" id="chip">chip ?</div></div>
  <div class="tile"><div class="k">Role / Relay</div><div class="v" id="roleRelay">-</div><div class="small" id="updated">Updated: never</div></div>
  <div class="tile metric" id="tileHeap"><div class="k">Free Heap</div><div class="v" id="heap">-</div></div>
  <div class="tile metric" id="tileBlock"><div class="k">Max Free Block</div><div class="v" id="maxblock">-</div></div>
  <div class="tile metric" id="tileRatio"><div class="k">Block Ratio</div><div class="v" id="ratio">-</div></div>
  <div class="tile metric" id="tileFrag"><div class="k">Heap Fragmentation</div><div class="v" id="frag">-</div></div>
  <div class="tile"><div class="k">WiFi</div><div class="v" id="wifi">-</div></div>
  <div class="tile"><div class="k">LoRa</div><div class="v" id="lora">-</div></div>
 </div>
 <div style="margin-top:10px" class="mono" id="raw">Loading metrics...</div>
</div>
<script>
function setText(id,v){ const el=document.getElementById(id); if(el) el.innerText=String(v); }
function classifyMetric(kind,v){
 if(kind==='heap'){ if(v < 2000) return 'crit'; if(v < 3500) return 'warn'; return 'ok'; }
 if(kind==='block'){ if(v < 1024) return 'crit'; if(v < 2000) return 'warn'; return 'ok'; }
 if(kind==='ratio'){ if(v < 35) return 'crit'; if(v < 55) return 'warn'; return 'ok'; }
 if(kind==='frag'){ if(v >= 55) return 'crit'; if(v >= 35) return 'warn'; return 'ok'; }
 return '';
}
function paint(tileId, kind, value){
 const el=document.getElementById(tileId); if(!el) return;
 el.classList.remove('ok','warn','crit');
 el.classList.add(classifyMetric(kind, value));
}
function fmtKb(v){ return `${(v/1024).toFixed(1)} KB`; }
function fmtUptime(ms){
 const s=Math.floor(ms/1000); const m=Math.floor(s/60); const h=Math.floor(m/60);
 if(h>0) return `${h}h ${m%60}m`; if(m>0) return `${m}m ${s%60}s`; return `${s}s`;
}
async function refreshLite(){
 try{
  const res=await fetch('/api/status-lite',{cache:'no-store'});
  if(res.status===401){ location.href='/login?expired=1'; return; }
  if(!res.ok) throw new Error(`HTTP ${res.status}`);
  const st=await res.json();
  const chip=(st.chip_id||'?').toString();
  setText('unit', chip && chip!=='?' ? `lrs-${chip}` : 'lrs-?');
  setText('chip', `chip ${chip}`);
  setText('roleRelay', `${String(st.role||'-').toUpperCase()} / ${Number(st.relay_state)===1?'ON':'OFF'}`);
  setText('wifi', st.sta_connected ? `${st.sta_rssi} dBm` : 'disconnected');
  setText('lora', Number(st.lora_last_packet_ms||0)>0 ? `${st.lora_last_rssi} dBm` : 'no packets');
  const hf=Number(st.heap_free_bytes||0), mb=Number(st.max_free_block_bytes||0);
  const frag=Number(st.heap_frag_percent||0);
  const ratio=(hf>0 && mb>0) ? Math.round((mb*100)/hf) : 0;
  setText('heap', hf ? fmtKb(hf) : '-');
  setText('maxblock', mb ? fmtKb(mb) : '-');
  setText('ratio', (hf&&mb) ? `${ratio}%` : '-');
  setText('frag', `${frag}%`);
  paint('tileHeap','heap',hf); paint('tileBlock','block',mb); paint('tileRatio','ratio',ratio); paint('tileFrag','frag',frag);
  setText('updated', `Updated: ${new Date().toLocaleTimeString()}`);
  setText('raw', `heap=${hf} max=${mb} ratio=${ratio}% frag=${frag}% uptime=${fmtUptime(Number(st.uptime_ms||0))}`);
 }catch(e){
  setText('notice', `Low-memory mode: could not refresh metrics (${e.message}).`);
 }
}
refreshLite();
</script></body></html>
)HTML";

#include "web_console_index_gen.h"
