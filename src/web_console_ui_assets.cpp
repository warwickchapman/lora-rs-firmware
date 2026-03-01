#include "web_console_ui_assets.h"
#include "feature_flags.h"

const char kLoginHtml_Part1[] PROGMEM = R"HTML(
<!doctype html><html><head>
)HTML";

const char kLoginHtml_Part2[] PROGMEM = R"HTML(
<meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Login</title>
<style>
:root{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%230f172a"/><circle cx="80" cy="20" r="50" fill="%234c1d95" opacity="0.4" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%231e3a8a" opacity="0.4" filter="blur(30px)"/></svg>');--card:rgba(15,23,42,0.4);--txt:#f8fafc;--muted:#cbd5e1;--border:rgba(255,255,255,0.1);--field:rgba(255,255,255,0.03);--btn:linear-gradient(135deg,#6366f1,#8b5cf6);--btn-hover:linear-gradient(135deg,#4f46e5,#7c3aed);--focus:rgba(139,92,246,0.5);--glass-shadow:0 4px 16px 0 rgba(0,0,0,0.2);--glass-border:1px solid rgba(255,255,255,0.1)}
body.light{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%23f8fafc"/><circle cx="80" cy="20" r="50" fill="%23c4b5fd" opacity="0.5" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%2393c5fd" opacity="0.5" filter="blur(30px)"/></svg>');--card:rgba(255,255,255,0.4);--txt:#0f172a;--muted:#475569;--border:rgba(255,255,255,0.3);--field:rgba(255,255,255,0.4);--btn:linear-gradient(135deg,#3b82f6,#6366f1);--btn-hover:linear-gradient(135deg,#2563eb,#4f46e5);--focus:rgba(99,102,241,0.5);--glass-shadow:0 4px 16px 0 rgba(31,38,135,0.1);--glass-border:1px solid rgba(255,255,255,0.4)}
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
</style></head><body><div class="card">
<div class="top"><button id="themeBtn" class="theme-btn" type="button" onclick="toggleTheme()">☀</button></div>
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

const char kIndexHtml[] PROGMEM =
    R"HTML(
<!doctype html><html><head><meta charset="utf-8" /><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover" />
<title>LRS Console</title>
<style>
:root{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%230f172a"/><circle cx="80" cy="20" r="50" fill="%234c1d95" opacity="0.4" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%231e3a8a" opacity="0.4" filter="blur(30px)"/></svg>');--card:rgba(15,23,42,0.4);--accent:linear-gradient(135deg,#6366f1,#8b5cf6);--accent-hover:linear-gradient(135deg,#4f46e5,#7c3aed);--txt:#f8fafc;--border:rgba(255,255,255,0.1);--field:rgba(255,255,255,0.03);--muted:#cbd5e1;--link:#a78bfa;--focus:rgba(139,92,246,0.5);--glass-shadow:0 4px 16px 0 rgba(0,0,0,0.2);--glass-border:1px solid rgba(255,255,255,0.1)}
body.light{--bg:url('data:image/svg+xml;utf8,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" preserveAspectRatio="none"><rect width="100" height="100" fill="%23f8fafc"/><circle cx="80" cy="20" r="50" fill="%23c4b5fd" opacity="0.5" filter="blur(30px)"/><circle cx="20" cy="80" r="50" fill="%2393c5fd" opacity="0.5" filter="blur(30px)"/></svg>');--card:rgba(255,255,255,0.4);--accent:linear-gradient(135deg,#3b82f6,#6366f1);--accent-hover:linear-gradient(135deg,#2563eb,#4f46e5);--txt:#0f172a;--border:rgba(255,255,255,0.3);--field:rgba(255,255,255,0.4);--muted:#475569;--link:#4f46e5;--focus:rgba(99,102,241,0.5);--glass-shadow:0 4px 16px 0 rgba(31,38,135,0.1);--glass-border:1px solid rgba(255,255,255,0.4)}
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);background-size:cover;background-position:center;background-attachment:fixed;color:var(--txt);-webkit-text-size-adjust:100%;overflow-x:hidden}
header{background:rgba(15,23,42,0.3);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border-bottom:var(--glass-border);color:var(--txt);padding:12px 16px;font-weight:700;display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap;position:sticky;top:0;z-index:10;box-shadow:var(--glass-shadow)}
body.light header{background:rgba(255,255,255,0.3)}
header .title{font-size:1.15rem;font-weight:700;letter-spacing:-0.01em;background:linear-gradient(to right,var(--txt),var(--muted));-webkit-background-clip:text;-webkit-text-fill-color:transparent;flex:1 1 260px;min-width:220px}
header .wifi{font-size:.8rem;padding:0;white-space:nowrap;display:flex;align-items:center;gap:6px;font-weight:600;min-height:auto;background:transparent;border:0;border-radius:0;box-shadow:none;backdrop-filter:none;-webkit-backdrop-filter:none}
header .wifi span:last-child{display:none}
header .relay-head{font-size:.8rem;padding:0;white-space:nowrap;font-weight:600;min-height:auto;display:flex;align-items:center;background:transparent;border:0;border-radius:0;box-shadow:none;backdrop-filter:none;-webkit-backdrop-filter:none;transition:color 0.2s ease}
header .relay-head.on{color:#86efac}
body.light header .relay-head.on{color:#15803d}
header .relay-head.off{color:var(--txt)}
header .relay-head.mem-ok{color:#86efac}
header .relay-head.mem-warn{color:#fde047}
header .relay-head.mem-crit{color:#fca5a5}
header .reason-head{font-size:.8rem;background:rgba(255,255,255,.08);border:var(--glass-border);border-radius:999px;padding:6px 12px;white-space:nowrap;display:none;backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
header .reason-head.show{display:inline-flex}
header .right{display:flex;gap:10px;row-gap:6px;flex-wrap:wrap;align-items:center;flex:1 1 520px;justify-content:flex-end}
header .id-badge{font-size:.8rem;padding:0;display:inline-flex;align-items:center;gap:6px;white-space:nowrap;min-height:auto;max-width:100%;background:transparent;border:0;border-radius:0;box-shadow:none;backdrop-filter:none;-webkit-backdrop-filter:none}
header .id-badge #deviceBadgeText{overflow:hidden;text-overflow:ellipsis;max-width:40ch}
header .id-copy{margin:0;padding:2px 6px;font-size:.72rem;border-radius:6px;border:0;background:transparent;color:var(--muted);cursor:pointer}
header .id-copy:hover{color:var(--txt)}
header .theme{margin-top:0;padding:0;min-width:auto;min-height:auto;border:0;background:transparent;border-radius:0;cursor:pointer;transition:opacity 0.2s ease;box-shadow:none;backdrop-filter:none;-webkit-backdrop-filter:none}
header .logout{margin-top:0;padding:0;min-height:auto;border:0;background:transparent;border-radius:0;cursor:pointer;transition:opacity 0.2s ease;box-shadow:none;backdrop-filter:none;-webkit-backdrop-filter:none;font-weight:600}
header .theme:hover, header .logout:hover{opacity:.8;transform:none}
main{padding:24px 16px;display:flex;flex-direction:column;gap:20px;max-width:980px;margin:0 auto}
.card{background:var(--card);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border:var(--glass-border);border-radius:24px;padding:24px;box-shadow:var(--glass-shadow);transition:transform 0.3s ease}
label{display:block;font-size:0.85rem;margin-top:12px;font-weight:700;color:var(--txt);letter-spacing:0.02em}
.grid{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
input,select,textarea{width:100%;padding:10px 14px;border:var(--glass-border);border-radius:12px;font-size:0.95rem;background:var(--field);color:var(--txt);transition:all 0.2s ease;outline:none;backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
input:focus,select:focus,textarea:focus{border-color:rgba(255,255,255,0.3);box-shadow:0 0 0 3px var(--focus), inset 0 0 0 1px rgba(255,255,255,0.1)}
.pass-field{display:grid;grid-template-columns:1fr auto;align-items:center;gap:10px}
.pass-field input{flex:1 1 auto}
.pass-toggle{margin-top:0;padding:0;width:40px;height:40px;min-width:40px;border:var(--glass-border);border-radius:12px;background:var(--field);color:var(--txt);white-space:nowrap;cursor:pointer;font-weight:700;font-size:1rem;display:inline-flex;align-items:center;justify-content:center;transition:all 0.2s ease;backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
.pass-toggle:hover{background:rgba(255,255,255,0.1);transform:translateY(-1px)}
body.light .pass-toggle:hover{background:rgba(255,255,255,0.6)}
input[type=checkbox]{width:20px;height:20px;padding:0;accent-color:#8b5cf6;cursor:pointer}
body.light input[type=checkbox]{accent-color:#6366f1}
.check-row{display:flex;align-items:center;gap:10px;margin-top:12px}
.network-soft-ap .check-row{margin-top:8px;min-height:42px}
.radio-row{display:flex;align-items:center;gap:0;margin-top:8px;border:var(--glass-border);border-radius:12px;overflow:hidden;width:100%;max-width:100%;background:var(--field);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
.radio-row label{margin:0;display:flex;align-items:center;justify-content:center;gap:10px;padding:10px 14px;cursor:pointer;user-select:none;font-weight:700;flex:1 1 0}
.radio-row label + label{border-left:var(--glass-border)}
.radio-row input[type=radio]{width:18px;height:18px;accent-color:#8b5cf6;flex:0 0 auto;cursor:pointer}
body.light .radio-row input[type=radio]{accent-color:#6366f1}
.lora-field{display:flex;flex-direction:column}
.lora-field .radio-row{margin-top:8px}
.freq-wrap{display:flex;flex-direction:column;gap:8px}
.freq-wrap input[readonly]{opacity:.7}
.check-row label{margin:0}
button{margin-top:16px;padding:10px 16px;border:0;border-radius:12px;background:var(--accent);color:#fff;font-weight:700;font-size:0.95rem;letter-spacing:0.01em;cursor:pointer;transition:all 0.2s ease;box-shadow:0 4px 12px rgba(99,102,241,0.2)}
button:hover{background:var(--accent-hover);transform:translateY(-1px);box-shadow:0 6px 16px rgba(99,102,241,0.3)}
button:active{transform:translateY(1px)}
button:disabled{opacity:.5;cursor:not-allowed;transform:none;box-shadow:none}
.small{font-size:0.85rem;color:var(--muted);line-height:1.4;overflow-wrap:anywhere}
.key-strength{margin-top:8px;font-size:0.85rem;font-weight:700}
.key-strength.weak{color:#fca5a5}
.key-strength.ok{color:#fde047}
.key-strength.strong{color:#86efac}
.actions{display:flex;gap:10px 12px;flex-wrap:wrap;align-items:center;margin-top:6px}
.actions.action-commit{justify-content:flex-end}
#status{overflow-wrap:anywhere;line-height:1.5}
.inline-row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.hint{font-size:0.8rem;opacity:.9;margin-top:6px;color:var(--muted)}
details summary{cursor:pointer;font-weight:700;margin:8px 0;padding:8px 0;outline:none;font-size:1.05rem}
.wifi-icon{display:inline-flex;align-items:center;justify-content:center;width:20px;height:16px}
.wifi-icon svg{width:20px;height:16px;display:block}
.wifi-icon .arc,.wifi-icon .dot{stroke:rgba(255,255,255,.4);fill:none;stroke-width:2.5;stroke-linecap:round;transition:stroke 0.3s}
body.light .wifi-icon .arc, body.light .wifi-icon .dot{stroke:rgba(0,0,0,.3)}
.wifi-icon .dot{fill:rgba(255,255,255,.4);stroke:none;transition:fill 0.3s}
body.light .wifi-icon .dot{fill:rgba(0,0,0,.3)}
.wifi-icon .x{stroke:#f87171;stroke-width:2.5;stroke-linecap:round;display:none}
.wifi-icon.lv1 .dot,.wifi-icon.lv2 .dot,.wifi-icon.lv3 .dot,.wifi-icon.lv4 .dot{fill:#4ade80}
.wifi-icon.lv2 .a3,.wifi-icon.lv3 .a3,.wifi-icon.lv4 .a3{stroke:#4ade80}
.wifi-icon.lv3 .a2,.wifi-icon.lv4 .a2{stroke:#4ade80}
.wifi-icon.lv4 .a1{stroke:#4ade80}
.wifi-icon.lv0 .x{display:block}
.wifi-list{margin-top:16px;background:rgba(255,255,255,0.05);border-radius:12px;overflow:auto;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border:var(--glass-border);box-shadow:0 4px 12px rgba(0,0,0,0.1)}
body.light .wifi-list{background:rgba(255,255,255,0.4);box-shadow:0 4px 12px rgba(31,38,135,0.05)}
.wifi-table{width:100%;border-collapse:collapse;font-size:0.9rem}
.wifi-table th,.wifi-table td{padding:10px 14px;border-bottom:var(--glass-border);text-align:left}
.wifi-table th{font-weight:700;color:var(--muted);background:rgba(0,0,0,0.2)}
body.light .wifi-table th{background:rgba(0,0,0,0.05)}
.wifi-table th:last-child,.wifi-table td:last-child{text-align:right}
.wifi-table td:first-child{max-width:48vw;overflow-wrap:anywhere}
.wifi-table td:nth-child(2){white-space:nowrap}
.wifi-table td:last-child{white-space:nowrap}
.wifi-table button{margin-top:0;padding:6px 10px;font-size:0.85rem;border-radius:10px}
.sig{display:inline-flex;align-items:flex-end;gap:3px;height:14px;margin-right:10px;vertical-align:-2px}
.sig i{display:block;width:3px;background:rgba(255,255,255,0.2);border-radius:2px;transition:background 0.3s}
body.light .sig i{background:rgba(0,0,0,0.1)}
.sig i:nth-child(1){height:4px}.sig i:nth-child(2){height:6px}.sig i:nth-child(3){height:9px}.sig i:nth-child(4){height:12px}
.sig.scan.lv1 i:nth-child(1),
.sig.scan.lv2 i:nth-child(-n+2),
.sig.scan.lv3 i:nth-child(-n+3),
.sig.scan.lv4 i:nth-child(-n+4){background:#4ade80}
.sig.lora i{background:rgba(255,255,255,0.2)}
body.light .sig.lora i{background:rgba(0,0,0,0.1)}
.sig.lora.lv1 i:nth-child(1),
.sig.lora.lv2 i:nth-child(-n+2),
.sig.lora.lv3 i:nth-child(-n+3),
.sig.lora.lv4 i:nth-child(-n+4){background:#60a5fa}
body.light .sig.lora.lv1 i:nth-child(1),
body.light .sig.lora.lv2 i:nth-child(-n+2),
body.light .sig.lora.lv3 i:nth-child(-n+3),
body.light .sig.lora.lv4 i:nth-child(-n+4){background:#3b82f6}
.sec-chip{display:inline-flex;align-items:center;justify-content:center;min-width:20px;height:20px;border-radius:999px;font-size:0.7rem;font-weight:700;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.sec-chip.y{background:rgba(74,222,128,0.2);color:#86efac;border:1px solid rgba(74,222,128,0.3)}
.sec-chip.n{background:rgba(255,255,255,0.1);color:var(--muted);border:var(--glass-border)}
body.light .sec-chip.y{background:rgba(74,222,128,0.2);color:#15803d;border:1px solid rgba(74,222,128,0.4)}
body.light .sec-chip.n{background:rgba(255,255,255,0.4);border:var(--glass-border)}
.link{color:var(--link);text-decoration:none;font-weight:600;transition:all 0.2s}
.link:hover{color:#c4b5fd;text-decoration:none;text-shadow:0 0 8px rgba(196,181,253,0.5)}
body.light .link:hover{color:#6366f1;text-shadow:none}
.menu-btn{margin-top:0;padding:6px 12px;min-width:40px;border:var(--glass-border);background:rgba(255,255,255,0.05);border-radius:10px;cursor:pointer;font-size:1.1rem;display:flex;align-items:center;justify-content:center;box-shadow:0 4px 12px rgba(0,0,0,0.1);transition:all 0.2s ease;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.menu-btn:hover{background:rgba(255,255,255,0.15);transform:translateY(-1px)}
body.light .menu-btn:hover{background:rgba(255,255,255,0.8)}
.drawer-backdrop{position:fixed;inset:0;background:rgba(0,0,0,.5);backdrop-filter:blur(4px);opacity:0;pointer-events:none;transition:opacity .3s ease;z-index:20}
.drawer{position:fixed;left:0;top:-10px;bottom:-10px;width:min(85vw,300px);padding:24px 16px;background:rgba(15,23,42,0.6);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border-right:var(--glass-border);transform:translateX(-100%);transition:transform .3s cubic-bezier(0.19, 1, 0.22, 1);z-index:21;overflow-y:auto;box-shadow:12px 0 32px rgba(0,0,0,0.3);display:flex;flex-direction:column}
body.light .drawer{background:rgba(255,255,255,0.6);box-shadow:12px 0 32px rgba(31,38,135,0.1)}
.drawer h4{margin:8px 12px 20px 12px;font-size:0.8rem;color:var(--muted);text-transform:uppercase;letter-spacing:.1em;font-weight:700}
.navbtn{display:flex;align-items:center;gap:12px;width:100%;margin-top:0;margin-bottom:6px;padding:12px 16px;border:var(--glass-border);border-color:transparent;border-radius:12px;background:transparent;color:var(--txt);text-align:left;font-weight:600;font-size:0.95rem;transition:all 0.2s;box-shadow:none}
.navbtn:hover{background:rgba(255,255,255,0.1);border-color:rgba(255,255,255,0.1)}
body.light .navbtn:hover{background:rgba(255,255,255,0.5);border-color:rgba(255,255,255,0.4)}
.navbtn.active{background:rgba(99,102,241,0.2);color:#a78bfa;border-color:rgba(99,102,241,0.3);box-shadow:inset 0 0 12px rgba(99,102,241,0.2)}
body.light .navbtn.active{background:rgba(99,102,241,0.15);color:#4f46e5;border-color:rgba(99,102,241,0.2)}
.navbtn.cog::before{content:'⚙';font-size:1.2rem;opacity:0.9}
.drawer-footer{margin-top:auto;display:flex;align-items:center;gap:12px;padding:12px 8px 6px}
.drawer-tool{margin:0;padding:0;border:0;background:transparent;color:var(--txt);font-size:1.15rem;line-height:1;cursor:pointer;opacity:.9}
.drawer-tool:hover{opacity:1}
body.nav-open .drawer{transform:translateX(0)}
body.nav-open .drawer-backdrop{opacity:1;pointer-events:auto}
body.nav-open{overflow:hidden}
.tabbtn{background:rgba(255,255,255,0.05);color:var(--muted);border:var(--glass-border);border-radius:10px;padding:10px 16px;min-height:42px;font-weight:700;cursor:pointer;transition:all 0.2s;box-shadow:none;margin-top:0;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.tabbtn:hover{background:rgba(255,255,255,0.15);color:var(--txt);transform:translateY(-1px)}
body.light .tabbtn:hover{background:rgba(255,255,255,0.8);color:var(--txt)}
.tabbtn.active{background:var(--accent);color:#fff;border-color:transparent;box-shadow:0 4px 12px rgba(99,102,241,0.2)}
body.light .tabbtn.active{color:#fff}
.page{display:none}
.page.active{display:block;animation:fadeIn 0.3s cubic-bezier(0.19, 1, 0.22, 1)}
@keyframes fadeIn{from{opacity:0;transform:translateY(8px)}to{opacity:1;transform:translateY(0)}}
.settings-tabs{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:20px}
.settings-pane .grid{align-items:start}
#page-settings .settings-pane>.actions{justify-content:flex-end;margin-top:16px}
#page-settings .settings-pane>.actions button{margin-top:0}
#settings-pane-lora>.grid{grid-template-columns:repeat(3,minmax(200px,1fr))}
#settings-pane-lora details .grid{grid-template-columns:repeat(2,minmax(220px,1fr))}
#settings-pane-lora #mode_select{margin-top:8px;min-height:42px}
#settings-pane-lora #tx_input_lora_control_row{padding-top:8px;margin-top:4px;border-top:var(--glass-border)}
#page-settings .settings-tabs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}
#page-settings .settings-tabs .tabbtn{width:100%;padding:8px 10px;min-height:42px}
.settings-pane{display:none}
.settings-pane.active{display:block;animation:fadeIn 0.3s ease}
.fleet-pane{display:none}
.fleet-pane.active{display:block;animation:fadeIn 0.3s ease}
.system-tabs{display:flex;gap:12px;flex-wrap:wrap;margin:16px 0 20px 0}
.system-pane{display:none}
.system-pane.active{display:block;animation:fadeIn 0.3s ease}
.fleet-table{width:100%;border-collapse:separate;border-spacing:0 8px;font-size:0.95rem;margin-top:16px}
.fleet-table th{color:var(--muted);font-weight:700;padding:12px 16px;text-align:left;background:transparent;border:none}
body.light .fleet-table th{background:transparent}
.fleet-table td{padding:14px 16px;border-top:var(--glass-border);border-bottom:var(--glass-border);vertical-align:middle;background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);transition:all 0.2s}
body.light .fleet-table td{background:rgba(255,255,255,0.4)}
.fleet-table tr:hover td{background:rgba(255,255,255,0.08);cursor:pointer;transform:scale(1.01)}
body.light .fleet-table tr:hover td{background:rgba(255,255,255,0.7)}
.fleet-table tr td:first-child{border-left:var(--glass-border);border-top-left-radius:12px;border-bottom-left-radius:12px}
.fleet-table tr td:last-child{border-right:var(--glass-border);border-top-right-radius:12px;border-bottom-right-radius:12px}
.fleet-table tr.selected td{background:rgba(99,102,241,0.15);border-color:rgba(99,102,241,0.3)}
body.light .fleet-table tr.selected td{background:rgba(99,102,241,0.1);border-color:rgba(99,102,241,0.2)}
.fleet-row-actions{display:flex;gap:10px;flex-wrap:wrap}
.fleet-row-actions button{margin-top:0;padding:8px 12px;font-size:0.85rem;border-radius:8px}
.fleet-detail{margin-top:20px;border:var(--glass-border);border-radius:12px;padding:20px;background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);box-shadow:inset 0 0 16px rgba(0,0,0,0.1)}
body.light .fleet-detail{background:rgba(255,255,255,0.4);box-shadow:inset 0 0 16px rgba(31,38,135,0.02)}
.fleet-detail-tabs{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:20px}
.fleet-detail-tabs .tabbtn{margin-top:0;padding:8px 14px;font-size:0.9rem}
.fleet-detail-grid{display:grid;grid-template-columns:160px 1fr;gap:12px 16px;font-size:0.95rem}
.fleet-detail-grid .k{color:var(--muted);font-weight:600}
.fleet-detail-grid .v{font-weight:700;overflow-wrap:anywhere}
.chip{display:inline-flex;align-items:center;justify-content:center;padding:4px 10px;border-radius:999px;font-size:0.75rem;font-weight:700;letter-spacing:0.02em;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.chip.ok{background:rgba(74,222,128,0.2);color:#86efac;border:1px solid rgba(74,222,128,0.3)}
body.light .chip.ok{color:#15803d;background:rgba(74,222,128,0.2)}
.chip.warn{background:rgba(251,191,36,0.2);color:#fde047;border:1px solid rgba(251,191,36,0.3)}
body.light .chip.warn{color:#b45309;background:rgba(251,191,36,0.2)}
.chip.err{background:rgba(248,113,113,0.2);color:#fca5a5;border:1px solid rgba(248,113,113,0.3)}
body.light .chip.err{color:#b91c1c;background:rgba(248,113,113,0.2)}
.chip.neutral{background:rgba(255,255,255,0.1);color:var(--txt);border:var(--glass-border)}
body.light .chip.neutral{background:rgba(255,255,255,0.4);border:var(--glass-border)}
.status-grid{display:grid;grid-template-columns:1.3fr 1fr;gap:20px;align-items:start}
.status-head{margin-bottom:8px;display:flex;align-items:center;justify-content:space-between;gap:10px}
.status-head h3{margin:0;display:inline-flex;align-items:center;gap:8px}
.status-live-dot{font-size:1.1rem;line-height:1;display:inline-flex;align-items:center;justify-content:center;min-width:1.1em}
.status-head-device{display:inline-flex;align-items:center;gap:8px;min-width:0;font-size:.82rem;color:var(--muted)}
.status-head-device .name{max-width:32ch;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.status-head-device .copy-btn{padding:4px 9px;font-size:.72rem}
.deploy-note{padding:16px;border:var(--glass-border);border-radius:12px;background:rgba(255,255,255,0.05);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);margin-bottom:20px;line-height:1.5}
.deploy-note.warn{border-color:rgba(251,191,36,0.4);background:rgba(251,191,36,0.1);color:#fde047}
body.light .deploy-note.warn{color:#b45309;background:rgba(251,191,36,0.1)}
.status-table{display:grid;grid-template-columns:160px 1fr;gap:12px 16px;font-size:0.95rem;background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);padding:20px;border-radius:12px;border:var(--glass-border);box-shadow:inset 0 0 16px rgba(0,0,0,0.1)}
body.light .status-table{background:rgba(255,255,255,0.4);box-shadow:inset 0 0 16px rgba(31,38,135,0.02)}
.status-table .k{color:var(--muted);font-weight:600}
.status-table .v{font-weight:700;overflow-wrap:anywhere}
.status-table .v.copyable{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.sta-line{display:inline-flex;align-items:center;gap:8px}
.sta-dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:rgba(255,255,255,.35)}
.sta-dot.on{background:#4ade80;box-shadow:0 0 8px rgba(74,222,128,.5)}
.sta-dot.off{background:rgba(255,255,255,.35)}
body.light .sta-dot.off{background:rgba(0,0,0,.25)}
.status-table .section{grid-column:1/-1;font-weight:800;margin-top:12px;padding-top:12px;border-top:var(--glass-border);color:var(--txt);font-size:1.05rem}
.copy-btn{margin:0;padding:6px 12px;font-size:0.8rem;border-radius:8px;background:rgba(255,255,255,0.05);color:var(--txt);border:var(--glass-border);box-shadow:none;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);font-weight:700}
.copy-btn:hover{background:rgba(255,255,255,0.15);transform:translateY(-1px)}
body.light .copy-btn:hover{background:rgba(255,255,255,0.8)}
.status-side{display:flex;flex-direction:column;gap:14px}
.relay-card{display:flex;flex-direction:column;justify-content:center;align-items:center;background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border:var(--glass-border);border-radius:16px;padding:32px 24px;box-shadow:inset 0 0 20px rgba(0,0,0,0.1)}
body.light .relay-card{background:rgba(255,255,255,0.4);box-shadow:inset 0 0 20px rgba(31,38,135,0.05)}
.relay-badge{width:120px;height:120px;border-radius:50%;display:flex;align-items:center;justify-content:center;font-size:0.95rem;font-weight:800;letter-spacing:0.05em;transition:all 0.4s cubic-bezier(0.19, 1, 0.22, 1);position:relative}
.relay-badge::before{content:'';position:absolute;inset:-6px;border-radius:50%;border:2px solid transparent;transition:all 0.4s ease}
.relay-badge.on{background:rgba(74,222,128,0.2);color:#86efac;box-shadow:0 0 32px rgba(74,222,128,0.3), inset 0 0 16px rgba(74,222,128,0.2);text-shadow:0 0 8px rgba(134,239,172,0.5)}
.relay-badge.on::before{border-color:rgba(74,222,128,0.5);transform:scale(1.05)}
body.light .relay-badge.on{color:#15803d;box-shadow:0 0 32px rgba(74,222,128,0.2), inset 0 0 16px rgba(74,222,128,0.1)}
.relay-badge.off{background:rgba(255,255,255,0.05);color:var(--muted);box-shadow:inset 0 0 16px rgba(0,0,0,0.2);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.relay-badge.off::before{border-color:rgba(255,255,255,0.1)}
body.light .relay-badge.off{background:rgba(255,255,255,0.4);box-shadow:inset 0 0 16px rgba(31,38,135,0.1)}
body.light .relay-badge.off::before{border-color:rgba(255,255,255,0.3)}
.sensor-grid{display:grid;grid-template-columns:repeat(auto-fill, minmax(200px, 1fr));gap:16px;margin-top:16px}
.status-sensor-grid{display:grid;grid-template-columns:1fr;gap:10px}
.status-sensor-grid .sensor-tile{padding:14px 16px;font-size:0.9rem}
.sensor-tile{background:rgba(255,255,255,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border:var(--glass-border);border-radius:12px;padding:20px;font-size:0.95rem;transition:all 0.2s;box-shadow:inset 0 0 16px rgba(0,0,0,0.1);font-weight:600}
body.light .sensor-tile{background:rgba(255,255,255,0.4);box-shadow:inset 0 0 16px rgba(31,38,135,0.02)}
.sensor-tile:hover{transform:translateY(-2px);background:rgba(255,255,255,0.08);box-shadow:0 8px 16px rgba(0,0,0,0.2)}
.sensor-state{display:inline-flex;align-items:center;justify-content:center;min-width:70px;padding:4px 12px;border-radius:999px;font-size:0.8rem;font-weight:700;letter-spacing:0.02em;backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px)}
.sensor-state.open{background:rgba(255,255,255,0.1);color:var(--muted);border:var(--glass-border)}
body.light .sensor-state.open{background:rgba(255,255,255,0.5);color:var(--muted)}
.sensor-state.closed{background:rgba(74,222,128,0.2);color:#86efac;border:1px solid rgba(74,222,128,0.3)}
body.light .sensor-state.closed{background:rgba(74,222,128,0.2);color:#15803d}
.toast{position:fixed;right:24px;bottom:24px;background:rgba(15,23,42,0.8);backdrop-filter:blur(24px);-webkit-backdrop-filter:blur(24px);border:var(--glass-border);color:var(--txt);padding:16px 24px;border-radius:16px;box-shadow:0 20px 40px rgba(0,0,0,.5);font-size:1rem;font-weight:700;z-index:9999;display:none;animation:slideUp 0.4s cubic-bezier(0.19, 1, 0.22, 1)}
@keyframes slideUp{from{transform:translateY(100px);opacity:0}to{transform:translateY(0);opacity:1}}
body.light .toast{background:rgba(255,255,255,0.8);color:#0f172a;box-shadow:0 20px 40px rgba(31,38,135,.15);border-color:rgba(255,255,255,0.8)}
.toast.show{display:block}
.toast.err{border-left:4px solid #f87171}
.mobile-action-bar{display:none;position:fixed;left:10px;right:10px;bottom:max(10px,env(safe-area-inset-bottom));z-index:30;padding:8px;background:rgba(15,23,42,0.8);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);border:var(--glass-border);border-radius:14px;gap:8px;box-shadow:0 12px 28px rgba(0,0,0,.35)}
body.light .mobile-action-bar{background:rgba(255,255,255,0.85);box-shadow:0 12px 28px rgba(31,38,135,.15)}
.mobile-action-bar button{margin-top:0;min-height:40px;padding:8px 12px;border-radius:10px}
.mobile-action-bar .primary{flex:1 1 auto}
.mobile-action-bar .secondary{flex:0 0 auto;background:rgba(255,255,255,0.08);border:var(--glass-border);box-shadow:none}
body.light .mobile-action-bar .secondary{background:rgba(255,255,255,0.7)}
.result-line{margin-top:16px;padding:14px 20px;border-radius:12px;border:var(--glass-border);font-weight:700;font-size:0.95rem;display:none;backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px)}
.result-line.show{display:block;animation:fadeIn 0.3s ease}
.result-line.ok{background:rgba(74,222,128,0.15);color:#86efac;border-color:rgba(74,222,128,0.4)}
body.light .result-line.ok{color:#15803d;background:rgba(74,222,128,0.1)}
.result-line.err{background:rgba(248,113,113,0.15);color:#fca5a5;border-color:rgba(248,113,113,0.4)}
body.light .result-line.err{color:#b91c1c;background:rgba(248,113,113,0.1)}
.spin{display:inline-block;width:16px;height:16px;border:3px solid rgba(255,255,255,.2);border-top-color:#8b5cf6;border-radius:50%;animation:sp 0.8s linear infinite;margin-right:12px;vertical-align:-3px}
body.light .spin{border-color:rgba(0,0,0,0.1);border-top-color:#6366f1}
@keyframes sp{to{transform:rotate(360deg)}}
@media(max-width:920px){
 #settings-pane-lora>.grid{grid-template-columns:repeat(2,minmax(180px,1fr))}
}
@media(max-width:850px){.status-grid{grid-template-columns:1fr}}
@media(max-width:650px){
 .grid{grid-template-columns:1fr}
 main{padding:16px 10px}
 .card{padding:18px;border-radius:18px}
 .fleet-detail-grid{grid-template-columns:140px 1fr}
 .status-table{grid-template-columns:140px 1fr}
 .relay-card{flex-direction:row;justify-content:space-between;padding:24px}
 .relay-badge{width:100px;height:100px;font-size:0.85rem}
 header{padding:10px 12px;display:grid;grid-template-columns:auto 1fr auto;align-items:start;column-gap:10px;row-gap:4px}
 header .title{width:auto;font-size:.96rem;font-weight:700;min-width:0;padding-top:0;line-height:1.2}
 header .right{width:auto;display:flex;flex-wrap:nowrap;gap:8px;align-items:center;justify-content:flex-end}
 header .right > *{min-width:0}
 header .id-badge{display:none}
 header .relay-head,
 header .wifi,
 header .theme,
 header .logout{
  margin:0;
  padding:0;
  min-height:auto;
  background:transparent;
  border:0;
  border-radius:0;
  box-shadow:none;
  backdrop-filter:none;
  -webkit-backdrop-filter:none;
 }
 header .relay-head{font-size:.74rem;font-weight:600;white-space:nowrap}
 header .wifi{font-size:.74rem;font-weight:600;gap:4px}
 header .wifi span:last-child{display:none}
 header .theme,header .logout{font-size:1rem;line-height:1}
 header .theme:hover, header .logout:hover{background:transparent;transform:none}
 header .wifi-icon{width:16px;height:14px}
 header .wifi-icon svg{width:16px;height:14px}
 header #loraIcon.sig{height:12px}
 .status-head-device{font-size:.75rem}
 .status-head-device .name{max-width:16ch}
 #page-settings .settings-tabs{grid-template-columns:repeat(2,minmax(0,1fr))}
 #page-settings .settings-tabs .tabbtn{min-height:40px}
 .system-tabs{display:grid;grid-template-columns:1fr 1fr;gap:10px}
 .system-tabs .tabbtn{width:100%}
 .wifi-table th,.wifi-table td{padding:8px 10px}
 .wifi-table{font-size:0.85rem}
 .wifi-table td:first-child{max-width:44vw}
 .wifi-table button{padding:6px 8px}
 .pass-toggle{width:36px;height:36px;min-width:36px}
 .actions.mobile-action-source{display:none}
 .mobile-action-bar.show{display:flex}
 main{padding-bottom:90px}
}
@media(max-width:430px){
 .status-head-device{display:none}
}
</style></head>
<body><div id="drawerBackdrop" class="drawer-backdrop" onclick="toggleDrawer(false)"></div><aside id="appDrawer" class="drawer" aria-label="Main navigation"><h4>Menu</h4><button class="navbtn active" id="nav-status" onclick="showPage('status')">Status</button><button class="navbtn" id="nav-fleet" onclick="showPage('fleet')">Fleet</button>

)HTML"
#if LRS_ENABLE_AUTOMATIONS
    R"HTML(<button class="navbtn" id="nav-automations" onclick="showPage('automations')">Automations</button>
)HTML"
#endif
    R"HTML(<button class="navbtn" id="nav-sensors" onclick="showPage('sensors')">Sensors</button><button class="navbtn cog" id="nav-settings" onclick="showPage('settings')">Settings</button><div class="drawer-footer"><button class="drawer-tool" onclick="logout()" title="Logout" aria-label="Logout">⎋</button><button class="drawer-tool theme-toggle" id="themeBtnDrawer" onclick="toggleTheme()" title="Toggle theme" aria-label="Toggle theme">☀</button></div></aside><header><button class="menu-btn" id="menuBtn" onclick="toggleDrawer()" title="Open menu" aria-label="Open menu">☰</button><div id="consoleTitle" class="title">lrs-00000000</div><div class="right"><div id="relayHeader" class="relay-head off" title="Relay off" aria-label="Relay off">⚪</div><div id="loraBadge" class="wifi"><span id="loraIcon" class="sig lora lv0"><i></i><i></i><i></i><i></i></span><span id="loraText">LoRa</span></div><div id="wifiBadge" class="wifi"><span id="wifiIcon" class="wifi-icon lv0"><svg viewBox="0 0 20 14" aria-hidden="true"><path class="arc a1" d="M1 6.5c5-5 13-5 18 0"></path><path class="arc a2" d="M4.5 9c3-3 8-3 11 0"></path><path class="arc a3" d="M7.8 11.2c1.2-1.2 3.2-1.2 4.4 0"></path><circle class="dot" cx="10" cy="12.6" r="1.2"></circle><path class="x" d="M2 2l3 3"></path><path class="x" d="M5 2l-3 3"></path></svg></span><span id="wifiText">WiFi</span></div></div></header><main>
<section class="card page active" id="page-status">
<div class="status-head"><h3>Status <span id="statusLiveState" class="status-live-dot" title="Waiting for device updates..." aria-label="Waiting for device updates...">🟡</span></h3><div id="statusHeadDevice" class="status-head-device"><span id="statusHeadDeviceText" class="name">Device: -</span><button id="statusHeadDeviceCopy" type="button" class="copy-btn" data-copy="" data-label="Device identity" onclick="copyFromButton(this)">Copy</button></div></div>
<div class="status-grid">
<div>
<div id="statusTable">Loading status...</div>
</div>
<div class="status-side">
<div class="relay-card">
<div id="relayBadge" class="relay-badge off">RELAY OFF</div>
<div id="relayMeta" class="small" style="margin-top:8px">Input: -, Link: -</div>
</div>
<h4 style="margin:0 0 2px 0">Sensors</h4>
<div class="status-sensor-grid">
<div class="sensor-tile" id="sensorTempTile">Temperature: n/a</div>
<div class="sensor-tile" id="sensorRemoteTempTile">Remote LoRa temp: n/a</div>
<div class="sensor-tile" id="sensorInputTile">Dry contact input: <span class="sensor-state open">OPEN</span></div>
<div class="sensor-tile">Tank: n/a</div>
<div class="sensor-tile">Float: n/a</div>
<div class="sensor-tile">Flow: n/a</div>
</div>
</div>
</div>
</section>
<section class="card page" id="page-fleet">
<h3>Fleet</h3>
<div class="settings-tabs"><button class="tabbtn active" id="fleet-tab-devices" onclick="showFleetTab('devices')">Devices</button><button class="tabbtn" id="fleet-tab-manage" onclick="showFleetTab('manage')">Manage</button></div>
<div class="fleet-pane active" id="fleet-pane-devices"><div class="small" id="fleetSummary">Loading...</div><div class="grid" style="margin-top:8px"><div><label>Scan start address</label><input id="fleetScanStart" type="number" min="1" max="254" value="1" /></div><div><label>Scan end address</label><input id="fleetScanEnd" type="number" min="1" max="254" value="80" /></div><div><label>Scan interval (ms)</label><input id="fleetScanIntervalMs" type="number" min="80" max="2000" value="120" /></div><div style="display:flex;align-items:end"><div class="actions"><button type="button" id="fleetScanBtn" onclick="toggleFleetScan()">Scan Fleet</button></div></div></div><div class="small" id="fleetScanSummary" style="margin-top:4px">Scan idle.</div><div id="fleetTableHost" style="margin-top:8px">Loading device list...</div><div id="fleetDetailHost" class="fleet-detail">Select a device to view details.</div></div>
<div class="fleet-pane" id="fleet-pane-manage"><div class="settings-tabs"><button class="tabbtn active" id="fleet-manage-tab-lora" onclick="showFleetManageTab('lora')">LoRa</button><button class="tabbtn" id="fleet-manage-tab-wifi" onclick="showFleetManageTab('wifi')">WiFi</button></div><div class="settings-pane" id="fleet-manage-pane-wifi"><div class="grid"><div style="grid-column:1/-1"><label>WiFi provisioning</label><div class="small">Uses STA SSID/password from Settings > Network and broadcasts them to devices in the same fleet.</div><div class="actions"><button type="button" onclick="provisionFleetWifi()">Send WiFi to Fleet (LoRa)</button></div><div id="wifiProvisionResult" class="result-line"></div></div></div></div><div class="settings-pane active" id="fleet-manage-pane-lora"><div class="grid"><div style="grid-column:1/-1"><label>LoRa provisioning</label><div class="small">Discover factory-key devices, auto-resolve duplicate addresses, and provision them into this fleet in batches of up to 8 devices.</div><div class="grid"><div><label>Estimated devices (max 8)</label><input id="prov_estimated_count" type="number" min="1" max="8" value="8" /></div></div><div class="actions"><button type="button" onclick="startFleetProvisioningDiscovery()">Start Discovery</button><button type="button" onclick="searchMoreFleetProvisioning()" title="Search more" aria-label="Search more">↻</button><button type="button" onclick="cancelFleetProvisioning()">Cancel</button></div><div id="provWizardResult" class="result-line"></div><div id="provWizardSummary" class="small" style="margin-top:6px"></div><div style="overflow:auto;max-height:260px;border:1px solid var(--border);border-radius:10px;margin-top:8px"><table class="table" style="margin:0"><thead><tr><th>Chip ID</th><th>Cur</th><th>New</th><th>FW</th><th>RSSI</th><th>Status</th></tr></thead><tbody id="provWizardRows"><tr><td colspan="6" class="small">No provisioning session active.</td></tr></tbody></table></div><div class="actions" style="margin-top:8px"><button type="button" onclick="provisionFleetAll()" id="provProvisionAllBtn" disabled>Provision All</button></div><div class="small">If more than 8 devices respond, provision this batch first, then run discovery again.</div></div></div></div></div>
</section>
 )HTML"
#if LRS_ENABLE_AUTOMATIONS
    R"HTML(<section class="card page" id="page-automations">
<h3>Automations</h3>
<div class="small" style="margin-bottom:8px">Builder shell (Phase 2). Rules are saved and validated only; runtime execution is not enabled in this phase.</div>
<div class="small" style="margin-bottom:8px">v1 limits: max 8 rules, max 4 conditions per rule, max 4 actions per rule, action type <code>set_relay</code> only.</div>
<div class="grid">
<div><div class="check-row"><input id="auto_enabled" type="checkbox" onchange="automationTopChanged()" /><label for="auto_enabled">Enable automations on this device</label></div></div>
<div><label>Execution mode (v1)</label><select id="auto_execution_mode" onchange="automationTopChanged()"><option value="standalone">standalone</option><option value="paired+rules">paired+rules</option></select></div>
<div><label>Peer display</label><select id="auto_peer_display" onchange="automationTopChanged()"><option value="addresses">Addresses</option><option value="names">Names</option></select></div>
<div><label>Action target (v1)</label><input id="auto_action_target" value="self" oninput="automationTopChanged()" /><div class="small">Use <code>self</code> or an address like <code>82</code> / <code>0x52</code>.</div></div>
</div>
<div class="actions"><button type="button" onclick="reloadAutomations()">Reload</button><button type="button" onclick="addAutomationRule()">Add Rule</button></div>
<div id="autoResult" class="result-line"></div>
<div id="automationsRulesHost" style="margin-top:8px">Open this page to load automations.</div>
<div class="actions action-commit" style="margin-top:20px;margin-bottom:20px"><button type="button" onclick="saveAutomations()">Save Rules</button></div>
<details style="margin-top:8px"><summary>JSON Preview</summary><div class="small" style="margin:6px 0">Generated from the form builder. You can paste JSON here and apply it back to the form.</div><textarea id="auto_json_preview" rows="14" style="width:100%;font-family:monospace" spellcheck="false"></textarea><div class="actions"><button type="button" onclick="applyAutomationsJsonFromPreview()">Apply JSON</button><button type="button" onclick="copyAutomationJsonPreview()">Copy JSON</button></div></details>
</section>
)HTML"
#endif
    R"HTML(<section class="card page" id="page-settings"><h3>Settings</h3><div class="settings-tabs"><button class="tabbtn active" id="settings-tab-network" onclick="showSettingsTab('network')">Network</button><button class="tabbtn" id="settings-tab-lora" onclick="showSettingsTab('lora')">LoRa</button><button class="tabbtn" id="settings-tab-mqtt" onclick="showSettingsTab('mqtt')">MQTT</button><button class="tabbtn" id="settings-tab-system" onclick="showSettingsTab('system')">System</button></div><div class="settings-pane" id="settings-pane-lora"><div class="grid">
<div class="lora-field"><label>Mode</label><select id="mode_select"><option value="standalone">Standalone</option><option value="paired">Paired</option><option value="mesh">Mesh</option></select></div>
<div class="lora-field"><label id="role_label">Role</label><div class="radio-row"><label><input type="radio" name="role_tx_radio" id="role_tx_true" checked /> <span id="role_tx_text">Transmitter</span></label><label><input type="radio" name="role_tx_radio" id="role_tx_false" /> <span id="role_rx_text">Receiver</span></label></div><input id="role_tx" type="hidden" value="true" /><input id="role_name" type="hidden" value="transmitter" /></div>
<div class="lora-field"><label>Frequency (MHz)</label><div class="freq-wrap"><div class="radio-row"><label><input type="radio" name="freq_preset" id="freq_433" /> 433</label><label><input type="radio" name="freq_preset" id="freq_915" /> 915</label></div><div class="small" id="freq_selected_text">Selected: 433.000 MHz</div><input id="lora_frequency_mhz" type="hidden" /></div></div>
<div style="grid-column:1/-1"><label>Fleet key (encryption)</label><input id="fleet_passphrase" /><div class="actions" style="margin-top:8px"><button type="button" onclick="suggestReadableFleetKeyForSettings()">Suggest readable key</button></div><div id="fleet_passphrase_strength" class="key-strength"></div><div class="small">Must be unique per installation to prevent nearby systems from controlling each other.<br>Use at least 16 characters.<br>Suggested format is read-aloud friendly.</div></div>
<div><label id="local_address_label">Local address</label><input id="local_address" type="text" /><div class="hint" id="local_address_hex"></div></div>
<div><label id="remote_address_label">Remote address</label><input id="remote_address" type="text" /><div class="hint" id="remote_address_hex"></div></div>
<div id="tx_input_lora_control_row" style="grid-column:1/-1"><div class="check-row"><input id="input_control_paired_lora_enabled" type="checkbox" /><label for="input_control_paired_lora_enabled">Local input drives LoRa control of paired relay</label></div><div class="small">When disabled, TX still reports local input but does not send input-driven LoRa relay commands.</div></div>
</div>
<details><summary>Advanced</summary><div class="grid">
<div><label>TX power</label><input id="lora_tx_power" type="number" min="2" max="20" /></div>
<div><label>Spreading factor</label><input id="lora_spreading_factor" type="number" min="6" max="12" /></div>
<div><label>Bandwidth (Hz)</label><input id="lora_bandwidth_hz" type="number" /></div>
<div><label>Coding rate (5-8)</label><input id="lora_coding_rate" type="number" min="5" max="8" /></div>
<div id="heartbeat_row"><label>Heartbeat (seconds)</label><input id="heartbeat_s" type="number" min="60" max="3600" /></div>
<div><label>ACK timeout (seconds)</label><input id="ack_timeout_s" type="number" min="5" max="600" /></div>
<div id="tx_mqtt_remote_retry_row"><label>MQTT remote retry timeout (seconds)</label><input id="mqtt_remote_retry_timeout_s" type="number" min="5" max="3600" /><div class="small">TX only. Retry remote MQTT LoRa commands until this timeout is reached.</div></div>
<div id="tx_polling_enabled_row" style="grid-column:1/-1"><div class="check-row"><input id="tx_mqtt_remote_polling_enabled" type="checkbox" /><label for="tx_mqtt_remote_polling_enabled">Enable scheduled remote polling</label></div><div class="small">TX only. When disabled, `poll_interval_s` schedules are ignored but `poll_now` still works.</div></div>
<div id="tx_polling_default_row"><label>Default remote poll interval (seconds)</label><input id="tx_mqtt_remote_default_poll_interval_s" type="number" min="60" max="3600" /><div class="small">TX only. Applied to newly discovered remote nodes. Minimum 60s to reduce LoRa duty-cycle risk.</div></div>
<div id="rx_push_on_change_row" style="grid-column:1/-1"><div class="check-row"><input id="rx_push_on_change_enabled" type="checkbox" /><label for="rx_push_on_change_enabled">RX push on input change</label></div><div class="small">RX only. Sends a LoRa status update immediately on dry-contact change, rate-limited by minimum interval.</div></div>
<div id="rx_push_interval_row"><label>RX push minimum interval (seconds)</label><input id="rx_push_min_interval_s" type="number" min="60" max="3600" /><div class="small">RX only. Guardrail range 60..3600 seconds.</div></div>
</div><div class="small" id="heartbeat_guardrail_hint">Guardrail: heartbeat is limited to >= 60 seconds to reduce LoRa duty-cycle risk.</div></details><div class="actions action-commit"><button onclick="saveLora()">Save</button></div></div><div class="settings-pane active" id="settings-pane-network"><div class="grid">
<div style="grid-column:1/-1"><div class="inline-row"><button id="wifiScanBtn" type="button" onclick="scanWifi()">Rescan SSIDs</button></div><div id="wifi_scan_list" class="wifi-list"></div></div>
<div><label>STA SSID</label><input id="wifi_sta_ssid" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" data-1p-ignore="true" data-lpignore="true" /></div><div><label>STA Password</label><div class="pass-field"><input id="wifi_sta_password" type="password" autocomplete="new-password" autocapitalize="none" autocorrect="off" spellcheck="false" data-1p-ignore="true" data-lpignore="true" /><button class="pass-toggle" type="button" onclick="togglePasswordField('wifi_sta_password',this)" title="Show password" aria-label="Show password">👁</button></div></div>
<div class="network-soft-ap"><label>Soft AP</label><div class="check-row"><input id="ap_always_on" type="checkbox" /><label for="ap_always_on">Keep Soft AP enabled</label></div></div><div></div>
<div style="grid-column:1/-1"><label id="lan_hostname_label">LAN hostname</label><input id="lan_hostname" /><div class="hint" id="lan_hostname_hint">Used as the device hostname for WiFi and OTA.</div><div class="hint" id="lan_hostname_preview_wrap" style="display:none">URL: <span id="lan_hostname_preview">http://lrs.local</span></div></div>
</div><div class="actions action-commit"><button onclick="saveNetwork()">Save</button><button id="btnTestSta" onclick="testSta()">Test</button></div><div id="netTestResult" class="result-line"></div></div><div class="settings-pane" id="settings-pane-mqtt"><div class="grid">
<div style="grid-column:1/-1"><div class="check-row"><input id="mqtt_client_enabled" type="checkbox" /><label for="mqtt_client_enabled">MQTT client enabled</label></div></div>
<div style="grid-column:1/-1"><div class="check-row"><input id="mqtt_control_enabled" type="checkbox" /><label for="mqtt_control_enabled">MQTT control enabled</label></div><div class="small">When enabled, inbound MQTT control commands are accepted and local automations are disabled.</div></div>
<div><label>Broker host</label><input id="mqtt_host" /></div>
<div><label>Broker port</label><input id="mqtt_port" type="number" min="1" max="65535" /></div>
<div><label>MQTT user</label><input id="mqtt_user" /></div>
<div><label>MQTT password</label><input id="mqtt_password" /></div>
<div style="grid-column:1/-1"><label>Topic root</label><input id="mqtt_topic_root" /></div>
<div style="grid-column:1/-1"><label>MQTT controller addresses</label><input id="mqtt_controller_addresses" placeholder="e.g. 1,84" /></div>
</div><div class="small" style="margin-top:4px">Control topics are per-device under &lt;topic_root&gt;/lrs-&lt;chipid&gt;.</div><div class="actions action-commit"><button onclick="saveMqtt()">Save</button><button onclick="testMqtt()">Test</button></div><div id="mqttTestResult" class="result-line"></div></div><div class="settings-pane" id="settings-pane-system"><div class="system-tabs"><button class="tabbtn active" id="system-tab-security" onclick="showSystemTab('security')">Security</button><button class="tabbtn" id="system-tab-configuration" onclick="showSystemTab('configuration')">Configuration</button><button class="tabbtn" id="system-tab-maintenance" onclick="showSystemTab('maintenance')">Maintenance</button></div><div class="system-pane active" id="system-pane-security"><div class="grid"><div><label>Admin password</label><input id="admin_password" type="password" /></div></div><div class="actions action-commit"><button onclick="saveSystem()">Save</button></div></div><div class="system-pane" id="system-pane-configuration"><div class="grid"><div style="grid-column:1/-1"><label>Configuration</label><div class="actions"><button onclick="window.location='/api/settings/export'">Export Config</button><button onclick="document.getElementById('importFile').click()">Import Config</button><input type="file" id="importFile" accept="application/json" style="display:none" onchange="importConfig(this.files&&this.files[0])"></div></div></div><pre id="factory"></pre></div><div class="system-pane" id="system-pane-maintenance"><div class="grid"><div style="grid-column:1/-1"><label>Firmware OTA</label><div class="actions"><input id="otaFile" type="file" accept=".bin,application/octet-stream" /><button onclick="uploadOta()">Upload OTA</button><span id="otaResult" class="small"></span></div></div><div style="grid-column:1/-1"><label>Device actions</label><div class="actions"><button onclick="window.location='/api/logs.csv'">Download Logs CSV</button><button onclick="reboot()">Reboot</button></div></div><div style="grid-column:1/-1"><label>Factory reset</label><div class="grid"><div><label>Confirm admin password</label><input id="factory_reset_password" type="password" autocomplete="current-password" /></div><div><label>Confirmation word</label><input id="factory_reset_confirm_word" type="text" autocapitalize="characters" autocomplete="off" spellcheck="false" placeholder="RESET or REMOVE" /><div class="small">Type <code>RESET</code> to keep the LoRa fleet key, or <code>REMOVE</code> to clear it.</div></div><div><div class="check-row"><input id="factory_reset_keep_wifi_local" type="checkbox" /><label for="factory_reset_keep_wifi_local">Keep WiFi credentials</label></div><div class="small">Tick to keep STA SSID/password after reset.</div></div></div><div class="actions"><button onclick="factoryResetLocal()">Factory Reset Device</button></div><div id="factoryResetResult" class="result-line"></div></div></div></div></section>
<section class="card page" id="page-sensors"><h3>Sensors</h3>
<h4 style="margin:6px 0 8px 0">Temperature Sensor</h4>
<div class="check-row" style="margin-bottom:8px"><input id="sensor_temp_enabled" type="checkbox" /><label for="sensor_temp_enabled">Enable DS18B20 (GPIO0)</label></div>
<div id="sensorDiag" class="sensor-grid" style="margin-bottom:10px">
<div class="sensor-tile" id="sensorDiagState">DS18B20: checking...</div>
<div class="sensor-tile" id="sensorDiagTemp">Temperature: n/a</div>
<div class="sensor-tile" id="sensorDiagAddr">Address: n/a</div>
<div class="sensor-tile" id="sensorDiagLast">Last read: n/a</div>
</div>
<div class="small">Data pin is fixed to GPIO0 on this hardware. Temperature is sampled automatically at heartbeat/2 (twice per heartbeat period, minimum 2s).</div><div class="actions action-commit"><button onclick="saveSensors()">Save</button></div></section>
</main>
<div id="mobileActionBar" class="mobile-action-bar" aria-live="polite">
 <button id="mobileActionPrimary" class="primary" type="button">Save</button>
 <button id="mobileActionSecondary" class="secondary" type="button" style="display:none">Test</button>
</div>
<footer style="max-width:860px;margin:0 auto 12px;padding:0 12px;"><div class="small card">HW: v1.2 | Batch: 251101 | <span id="footerMem">Mem -/-</span> | <span id="footerFw">FW: -</span></div></footer>
<div id="toast" class="toast"></div>
<script>
const FREQ_MIN_MHZ = 400.0;
const FREQ_MAX_MHZ = 1000.0;
const MIN_DEPLOYMENT_KEY_LEN = 16;
const READABLE_KEY_CONSONANTS = 'bdfghjkmnprstvwz';
const READABLE_KEY_VOWELS = 'aeiou';
)HTML"
#if LRS_ENABLE_MDNS
    R"HTML(const LRS_ENABLE_MDNS = true;
)HTML"
#else
    R"HTML(const LRS_ENABLE_MDNS = false;
)HTML"
#endif
#if LRS_ENABLE_AUTOMATIONS
    R"HTML(const UI_AUTOMATIONS_ENABLED = true;
)HTML"
#else
    R"HTML(const UI_AUTOMATIONS_ENABLED = false;
)HTML"
#endif
    R"HTML(
let statusFailCount = 0;
let activePage = 'status';
let activeSettingsTab = 'network';
let activeSystemTab = 'security';
let currentTheme = 'dark';
let staIsConnected = false;
let connectedStaSsid = '';
let staTestInFlight = false;
let currentStaIp = '';
let currentLanMdns = '';
let automationsPageLoaded = false;
let automationsPageLoadInFlight = false;
let automationsDoc = null;
let currentApIp = '';
let currentApMdns = '';
let lastRoleIsTx = false;
let fleetDevicesCache = [];
let selectedFleetDeviceAddr = 0;
let fleetDeviceDetailTab = 'state';
let activeFleetTab = 'devices';
let activeFleetManageTab = 'lora';
let headerStatusRefreshInFlight = false;
let sessionRefreshInFlight = false;
let fleetRefreshInFlight = false;
let fleetRefreshDebounceTimer = 0;
let fleetScanState = {active:false,start_address:1,end_address:80,next_address:1,interval_ms:120,sent:0,total:0,scanned:0,progress_pct:0};
let provStatusInFlight = false;
let provLastStatusRefreshMs = 0;
let provUiSessionActive = false;
let provUiSessionState = 'idle';
let settingsPageLoaded = false;
let settingsPageLoadInFlight = false;
let wifiScanInFlight = false;
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
let mobileActionSourceEl = null;
let mobileActionPrimaryEl = null;
let mobileActionSecondaryEl = null;
let mobileActionBarEl = null;

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
 const modeEl=document.getElementById('mode_select');
 const mode=String((modeEl && modeEl.value) || 'paired');
 const txTextEl=document.getElementById('role_tx_text');
 const rxTextEl=document.getElementById('role_rx_text');
 const roleNameEl=document.getElementById('role_name');
 if(mode === 'mesh'){
  if(txTextEl) txTextEl.innerText='Coordinator';
  if(rxTextEl) rxTextEl.innerText='Node';
 }else if(mode === 'standalone'){
  if(txTextEl) txTextEl.innerText='Host';
  if(rxTextEl) rxTextEl.innerText='Disabled';
 }else{
  if(txTextEl) txTextEl.innerText='Transmitter';
  if(rxTextEl) rxTextEl.innerText='Receiver';
 }
 const tx=document.getElementById('role_tx').value==='true';
 const localLabel=document.getElementById('local_address_label');
 const remoteLabel=document.getElementById('remote_address_label');
 if(localLabel && remoteLabel){
  if(mode === 'paired'){
   localLabel.innerText=tx?'TX local address (source)':'RX local address';
   remoteLabel.innerText=tx?'RX remote address (destination)':'TX remote address (source)';
  }else if(mode === 'mesh'){
   localLabel.innerText=tx?'Coordinator local address':'Node local address';
   remoteLabel.innerText=tx?'Node address (target)':'Coordinator address (parent)';
  }else{
   localLabel.innerText='Local address';
   remoteLabel.innerText='Peer address (optional)';
  }
 }
 if(roleNameEl){
  if(mode === 'mesh'){
   roleNameEl.value = tx ? 'coordinator' : 'node';
  }else if(mode === 'standalone'){
   roleNameEl.value = 'none';
  }else{
   roleNameEl.value = tx ? 'transmitter' : 'receiver';
  }
 }
 const txInputRow=document.getElementById('tx_input_lora_control_row');
 if(txInputRow){ txInputRow.style.display = (mode === 'paired' && tx) ? '' : 'none'; }
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
 const heartbeatRow=document.getElementById('heartbeat_row');
 const heartbeatHint=document.getElementById('heartbeat_guardrail_hint');
 const heartbeatInput=document.getElementById('heartbeat_s');
 const heartbeatVisible = (mode === 'paired');
 if(heartbeatRow){ heartbeatRow.style.display = heartbeatVisible ? '' : 'none'; }
 if(heartbeatHint){ heartbeatHint.style.display = heartbeatVisible ? '' : 'none'; }
 if(heartbeatInput){
  heartbeatInput.disabled = !heartbeatVisible;
  if(!heartbeatVisible){ heartbeatInput.value = '60'; }
 }
}
function refreshHostnamePreview(){
 if(!LRS_ENABLE_MDNS) return;
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
function randomIndex(max){
 if(max<=1) return 0;
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
function suggestReadableFleetKeyForSettings(){
 const input=document.getElementById('fleet_passphrase');
 if(!input) return;
 input.value = generateReadableFleetKey();
 updateDeploymentKeyStrength();
 showToast('Suggested readable key generated.');
}
function deploymentKeyStrength(v){
 const s=String(v||'').trim();
 const readablePattern = /^(?:[bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz])(?:-(?:[bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz])){3}$/;
 if(!s.length){ return {cls:'', text:`Enter deployment key (min ${MIN_DEPLOYMENT_KEY_LEN} chars).`}; }
 if(isDefaultDeploymentKey(s)){ return {cls:'weak', text:'Weak: default key is blocked.'}; }
 if(readablePattern.test(s)){ return {cls:'strong', text:'Strong: read-aloud key format (~80-bit).'}; }
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
 return String(v===undefined || v===null ? '' : v).replace(/[&<>"']/g,(m)=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}
function copyButtonHtml(value, label='Value'){
 const txt=String(value===undefined || value===null ? '' : value).trim();
 const low=txt.toLowerCase();
 if(!txt || low==='n/a' || low==='not_set') return '';
 return `<button type="button" class="copy-btn" data-copy="${escapeHtml(txt)}" data-label="${escapeHtml(label)}" onclick="copyFromButton(this)">Copy</button>`;
}
function applyAutomationsFeatureVisibility(){
 const nav=document.getElementById('nav-automations');
 const page=document.getElementById('page-automations');
 if(nav) nav.style.display = UI_AUTOMATIONS_ENABLED ? '' : 'none';
 if(page) page.style.display = UI_AUTOMATIONS_ENABLED ? '' : 'none';
 if(!UI_AUTOMATIONS_ENABLED && activePage==='automations'){ activePage='status'; }
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
function sleep(ms){
 return new Promise((resolve)=>setTimeout(resolve, ms));
}
function isCompactMobile(){
 return window.matchMedia && window.matchMedia('(max-width:650px)').matches;
}
function getActiveActionContainer(){
 const page=document.querySelector('.page.active');
 if(!page) return null;
 if(page.id==='page-settings'){
  const pane=page.querySelector('.settings-pane.active');
  if(pane){
   const own=Array.from(pane.children).find((el)=>el.classList && el.classList.contains('actions') && el.classList.contains('action-commit'));
   if(own) return own;
   const fallback=Array.from(pane.children).find((el)=>el.classList && el.classList.contains('actions'));
   if(fallback) return fallback;
  }
 }
 const pageCommit=Array.from(page.children||[]).find((el)=>el.classList && el.classList.contains('actions') && el.classList.contains('action-commit'));
 if(pageCommit) return pageCommit;
 const own=Array.from(page.children||[]).find((el)=>el.classList && el.classList.contains('actions'));
 if(own) return own;
 const nestedCommit=page.querySelector('.actions.action-commit');
 if(nestedCommit) return nestedCommit;
 const nested=page.querySelector('.actions');
 if(nested) return nested;
 return null;
}
function setMobileActionBarBindings(primaryBtn, secondaryBtn){
 if(mobileActionPrimaryEl){
  mobileActionPrimaryEl.onclick = primaryBtn ? ()=>primaryBtn.click() : null;
  mobileActionPrimaryEl.textContent = primaryBtn ? String(primaryBtn.textContent||'Save').trim() : 'Save';
 }
 if(mobileActionSecondaryEl){
  if(secondaryBtn){
   mobileActionSecondaryEl.style.display='';
   mobileActionSecondaryEl.textContent = String(secondaryBtn.textContent||'Test').trim();
   mobileActionSecondaryEl.onclick = ()=>secondaryBtn.click();
  }else{
   mobileActionSecondaryEl.style.display='none';
   mobileActionSecondaryEl.onclick = null;
  }
 }
}
function updateMobileActionBar(){
 if(!mobileActionBarEl) return;
 if(mobileActionSourceEl){
  mobileActionSourceEl.classList.remove('mobile-action-source');
  mobileActionSourceEl = null;
 }
 if(!isCompactMobile()){
  mobileActionBarEl.classList.remove('show');
  setMobileActionBarBindings(null, null);
  return;
 }
 const actions=getActiveActionContainer();
 if(!actions){
  mobileActionBarEl.classList.remove('show');
  setMobileActionBarBindings(null, null);
  return;
 }
 const buttons=Array.from(actions.querySelectorAll('button'));
 const primary=buttons.find((b)=>/save/i.test(String(b.textContent||'')));
 if(!primary){
  mobileActionBarEl.classList.remove('show');
  setMobileActionBarBindings(null, null);
  return;
 }
 const secondary=buttons.find((b)=>/test/i.test(String(b.textContent||'')));
 mobileActionSourceEl = actions;
 mobileActionSourceEl.classList.add('mobile-action-source');
 setMobileActionBarBindings(primary, secondary || null);
 mobileActionBarEl.classList.add('show');
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
 if(r==='ack_timeout') return 'ack timeout';
 if(r==='no_lora_link') return 'no LoRa link';
 if(r==='input_open') return 'input open';
 if(r==='boot') return 'boot';
 if(r==='wait_ack') return 'waiting ack';
 if(r==='ok') return 'normal';
 if(r==='automation_on') return 'automation on';
 if(r==='automation_off') return 'automation off';
 if(r==='mqtt_on') return 'mqtt command on';
 if(r==='mqtt_off') return 'mqtt command off';
 if(r==='lora_on') return 'LoRa command on';
 if(r==='lora_off') return 'LoRa command off';
 return (r || 'unknown').replace(/_/g,' ');
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
 const disabled=staTestInFlight || !hasSsid;
 btn.disabled=disabled;
 if(unchanged){
  btn.title='Already connected with these STA credentials. Test will re-check current connection.';
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
 if(!LRS_ENABLE_MDNS) return;
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
 if(LRS_ENABLE_MDNS && lanMdns && host===lanMdns) return true;
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
 updateMobileActionBar();
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
 updateMobileActionBar();
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
function defaultAutomationPredicate(){
 return {peer:'self', field:'input', op:'==', value:0};
}
function defaultAutomationAction(){
 return {type:'set_relay', peer:'self', value:1};
}
function defaultAutomationRule(idx){
 return {
  id:`rule_${idx+1}`,
  name:`Rule ${idx+1}`,
  enabled:true,
  for_ms:0,
  cooldown_ms:60000,
  when:{all:[defaultAutomationPredicate()]},
  then:[defaultAutomationAction()]
 };
}
function defaultAutomationsDoc(){
 return {schema_version:1, enabled:false, execution_mode:'standalone', peer_display:'addresses', action_target:'self', rules:[]};
}
function automationNormalizeFieldDefaults(pred){
 if(!pred) return;
 if(pred.field==='temp_c'){
  pred.op='>';
  pred.value=Number(pred.value);
  if(!Number.isFinite(pred.value)) pred.value=0;
  return;
 }
 if(pred.field==='reachable'){
  pred.op='==';
  pred.value = (pred.value===true || pred.value===1 || String(pred.value).toLowerCase()==='true');
  return;
 }
 pred.op='==';
 const n=Number(pred.value);
 pred.value = (n===1) ? 1 : 0;
}
function normalizeAutomationsDoc(doc){
 const out = (doc && typeof doc === 'object') ? doc : {};
 if(typeof out.schema_version !== 'number') out.schema_version = 1;
 if(typeof out.enabled !== 'boolean') out.enabled = false;
 if(out.execution_mode!=='paired+rules') out.execution_mode='standalone';
 if(out.peer_display!=='names') out.peer_display='addresses';
 if(typeof out.action_target !== 'string' && typeof out.action_target !== 'number') out.action_target='self';
 if(!Array.isArray(out.rules)) out.rules = [];
 out.rules = out.rules.slice(0,8).map((r,i)=>{
  const rule = (r && typeof r==='object') ? r : {};
  if(!rule.id) rule.id = `rule_${i+1}`;
  if(!rule.name) rule.name = `Rule ${i+1}`;
  rule.enabled = (rule.enabled !== false);
  rule.for_ms = Math.max(0, Number(rule.for_ms||0)|0);
  rule.cooldown_ms = Math.max(0, Number(rule.cooldown_ms||0)|0);
  if(!rule.when || !Array.isArray(rule.when.all)) rule.when = {all:[defaultAutomationPredicate()]};
  rule.when.all = rule.when.all.slice(0,4).map((p)=>{
   const pred = (p && typeof p==='object') ? p : defaultAutomationPredicate();
   pred.peer = (pred.peer===undefined || pred.peer===null || pred.peer==='') ? 'self' : pred.peer;
   pred.field = String(pred.field||'input');
   if(!['temp_c','reachable','input','relay'].includes(pred.field)) pred.field='input';
   pred.op = String(pred.op||'==');
   if(pred.for_ms !== undefined && pred.for_ms !== null) pred.for_ms = Math.max(0, Number(pred.for_ms)||0);
   automationNormalizeFieldDefaults(pred);
   return pred;
  });
  if(!rule.when.all.length) rule.when.all = [defaultAutomationPredicate()];
  let actions = Array.isArray(rule.then) ? rule.then : (rule.then ? [rule.then] : [defaultAutomationAction()]);
  actions = actions.slice(0,4).map((a)=>{
   const act = (a && typeof a==='object') ? a : defaultAutomationAction();
   act.type = 'set_relay';
   act.peer = (act.peer===undefined || act.peer===null || act.peer==='') ? 'self' : act.peer;
   const v = Number(act.value);
   act.value = (v===1) ? 1 : 0;
   return act;
  });
  if(!actions.length) actions = [defaultAutomationAction()];
  rule.then = actions;
  return rule;
 });
 return out;
}
function automationShowResult(msg, kind){
 const el=document.getElementById('autoResult');
 if(!el) return;
 if(!msg){ el.className='result-line'; el.innerText=''; return; }
 el.className=`result-line show${kind==='ok'?' ok':''}${kind==='err'?' err':''}`;
 el.innerText=msg;
}
function automationTopChanged(){
 if(!automationsDoc) automationsDoc = defaultAutomationsDoc();
 const autoEnabledEl = document.getElementById('auto_enabled');
 const autoModeEl = document.getElementById('auto_execution_mode');
 const autoPeerDisplayEl = document.getElementById('auto_peer_display');
 const autoTargetEl = document.getElementById('auto_action_target');
 automationsDoc.enabled = !!(autoEnabledEl && autoEnabledEl.checked);
 automationsDoc.execution_mode = (autoModeEl && autoModeEl.value === 'paired+rules') ? 'paired+rules' : 'standalone';
 automationsDoc.peer_display = (autoPeerDisplayEl && autoPeerDisplayEl.value === 'names') ? 'names' : 'addresses';
 const target = String((autoTargetEl && autoTargetEl.value) || 'self').trim();
 automationsDoc.action_target = target || 'self';
 refreshAutomationsJsonPreview();
}
function automationFormatValueForInput(pred){
 if(!pred) return '';
 if(pred.field==='reachable') return pred.value ? 'true' : 'false';
 return String(pred.value === undefined || pred.value === null ? '' : pred.value);
}
function automationFieldHint(field){
 if(field==='temp_c') return 'numeric (e.g. 35)';
 if(field==='reachable') return 'true / false';
 return '0=open, 1=closed';
}
function automationIsToken(s){
 const t=String(s||'').trim();
 return /^[A-Za-z0-9_-]{1,32}$/.test(t);
}
function automationIsAddressToken(v, allowSelf){
 const t=String(v===undefined || v===null ? '' : v).trim();
 if(!t.length) return false;
 if(allowSelf && t==='self') return true;
 const n=parseAddress(t);
 return Number.isInteger(n) && n>=1 && n<=254;
}
function automationFriendlyApiError(out){
 if(!out) return 'Request failed (no response).';
 const code=String(out.error||'request_failed');
 const detail=(out.detail!==undefined && out.detail!==null) ? String(out.detail) : '';
 const msgMap={
  disabled:'Automations are disabled in this firmware build.',
  payload_too_large:'Automation rules JSON is too large (max 6 KB in v1).',
  empty_body:'Automation rules payload was empty.',
  invalid_json:'Automation JSON is invalid.',
  bad_root:'Top-level JSON must be an object.',
  bad_schema:'Unsupported automation schema schema_version.',
  bad_mode:'Execution mode must be standalone or paired+rules.',
  bad_peer_display:'Peer display must be addresses or names.',
  bad_action_target:'Action target must be self or an address (1..254).',
  bad_rules:'Missing or invalid rules array.',
  too_many_rules:'Too many rules (max 8).',
  bad_rule:'A rule has an invalid field, condition, or action.',
  open_failed:'Could not open automation rules file.',
  open_tmp:'Could not create temporary rules file.',
  write_failed:'Could not write rules file.',
  rename_failed:'Could not finalize rules file save.'
 };
 let msg = msgMap[code] || `Save failed (${code}).`;
 if(detail) msg += ` (${detail})`;
 return msg;
}
function automationValidateDoc(doc){
 const errs=[];
 if(!doc || typeof doc!=='object'){ errs.push('Document must be a JSON object.'); return errs; }
 if(!Array.isArray(doc.rules)){ errs.push('Rules must be an array.'); return errs; }
 if(doc.rules.length>8) errs.push('Max rules is 8 in v1.');
 if(doc.execution_mode && !['standalone','paired+rules'].includes(String(doc.execution_mode))) errs.push('Execution mode must be standalone or paired+rules.');
 if(doc.peer_display && !['addresses','names'].includes(String(doc.peer_display))) errs.push('Peer display must be addresses or names.');
 if(doc.action_target!==undefined && doc.action_target!==null && !automationIsAddressToken(doc.action_target, true)){
  errs.push('Action target must be self or address 1..254.');
 }
 doc.rules.slice(0,8).forEach((r,ri)=>{
  if(!r || typeof r!=='object'){ errs.push(`Rule ${ri+1}: must be an object.`); return; }
  if(!automationIsToken(r.id||'')) errs.push(`Rule ${ri+1}: id must use only letters, numbers, _ or - (max 32).`);
  const all = r.when && Array.isArray(r.when.all) ? r.when.all : null;
  if(!all){ errs.push(`Rule ${ri+1}: WHEN all[] is required.`); return; }
  if(all.length<1 || all.length>4) errs.push(`Rule ${ri+1}: conditions must be 1..4.`);
  all.slice(0,4).forEach((p,pi)=>{
   if(!p || typeof p!=='object'){ errs.push(`Rule ${ri+1} condition ${pi+1}: invalid.`); return; }
   if(!automationIsAddressToken(p.peer, true)) errs.push(`Rule ${ri+1} condition ${pi+1}: peer must be self or address 1..254.`);
   if(!['temp_c','reachable','input','relay'].includes(String(p.field||''))) errs.push(`Rule ${ri+1} condition ${pi+1}: unsupported field.`);
   if(p.value===undefined || p.value===null) errs.push(`Rule ${ri+1} condition ${pi+1}: value is required.`);
  });
  const acts = Array.isArray(r.then) ? r.then : (r.then ? [r.then] : []);
  if(acts.length<1 || acts.length>4) errs.push(`Rule ${ri+1}: actions must be 1..4.`);
  acts.slice(0,4).forEach((a,ai)=>{
   if(!a || typeof a!=='object'){ errs.push(`Rule ${ri+1} action ${ai+1}: invalid.`); return; }
   if(String(a.type||'')!=='set_relay') errs.push(`Rule ${ri+1} action ${ai+1}: action must be set_relay.`);
   if(!automationIsAddressToken(a.peer, true)) errs.push(`Rule ${ri+1} action ${ai+1}: target peer must be self or address 1..254.`);
   if(!(Number(a.value)===0 || Number(a.value)===1)) errs.push(`Rule ${ri+1} action ${ai+1}: relay state must be 0 or 1.`);
  });
 });
 return errs;
}
function renderAutomationsRules(){
 const host=document.getElementById('automationsRulesHost');
 if(!host) return;
 if(!automationsDoc){ host.innerHTML='Automations not loaded.'; return; }
 const rules = Array.isArray(automationsDoc.rules) ? automationsDoc.rules : [];
 if(!rules.length){
  host.innerHTML='<div class="small">No rules yet. Click <b>Add Rule</b> to create one.</div>';
  refreshAutomationsJsonPreview();
  return;
 }
 let html='';
 rules.forEach((r,ri)=>{
  html += `<div style="border:1px solid var(--border);border-radius:10px;padding:10px;margin-top:8px;background:rgba(255,255,255,.02)">`;
  html += `<div class="grid">`;
  html += `<div><label>Rule name</label><input value="${escapeHtml(r.name||'')}" oninput="automationSetRuleField(${ri},'name',this.value)"></div>`;
  html += `<div><label>Rule id</label><input value="${escapeHtml(r.id||'')}" oninput="automationSetRuleField(${ri},'id',this.value)"></div>`;
  html += `<div><div class="check-row"><input type="checkbox" ${r.enabled!==false?'checked':''} onchange="automationSetRuleEnabled(${ri},this.checked)"><label>Enabled</label></div></div><div></div>`;
  html += `<div><label>Start after condition is true for (seconds)</label><input type="number" min="0" value="${Math.floor(Number(r.for_ms||0)/1000)}" oninput="automationSetRuleMs(${ri},'for_ms',this.value)"></div>`;
  html += `<div><label>Do not trigger again for (seconds)</label><input type="number" min="0" value="${Math.floor(Number(r.cooldown_ms||0)/1000)}" oninput="automationSetRuleMs(${ri},'cooldown_ms',this.value)"><div class="small">Recommended default: 60s</div></div>`;
  html += `</div>`;
  html += `<div class="small" style="margin-top:6px">WHEN (ALL conditions) - first matching rule wins and processing stops.</div>`;
  (((r.when && r.when.all) || [])).forEach((p,pi)=>{
   html += `<div style="border:1px solid var(--border);border-radius:8px;padding:8px;margin-top:6px">`;
   html += `<div class="grid">`;
   html += `<div><label>Peer</label><input value="${escapeHtml(String(p.peer === undefined || p.peer === null ? 'self' : p.peer))}" oninput="automationSetPredicateField(${ri},${pi},'peer',this.value)"></div>`;
   html += `<div><label>Field</label><select onchange="automationSetPredicateField(${ri},${pi},'field',this.value)">`;
   ['temp_c','reachable','input','relay'].forEach(f=>{ html += `<option value="${f}" ${p.field===f?'selected':''}>${f}</option>`; });
   html += `</select></div>`;
   html += `<div><label>Op</label><input value="${escapeHtml(String(p.op||'=='))}" oninput="automationSetPredicateField(${ri},${pi},'op',this.value)"></div>`;
   html += `<div><label>Value</label><input value="${escapeHtml(automationFormatValueForInput(p))}" oninput="automationSetPredicateField(${ri},${pi},'value',this.value)"><div class="small">${automationFieldHint(p.field)}</div></div>`;
   html += `</div>`;
   html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationRemovePredicate(${ri},${pi})">Remove Condition</button></div>`;
   html += `</div>`;
  });
  html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationAddPredicate(${ri})">Add Condition (AND)</button></div>`;
  html += `<div class="small" style="margin-top:6px">THEN</div>`;
  (r.then||[]).forEach((a,ai)=>{
   html += `<div style="border:1px solid var(--border);border-radius:8px;padding:8px;margin-top:6px">`;
   html += `<div class="grid">`;
   html += `<div><label>Action</label><select disabled><option selected>set_relay</option></select></div>`;
   html += `<div><label>Target peer</label><input value="${escapeHtml(String(a.peer === undefined || a.peer === null ? 'self' : a.peer))}" oninput="automationSetActionField(${ri},${ai},'peer',this.value)"></div>`;
   html += `<div><label>Relay state</label><select onchange="automationSetActionField(${ri},${ai},'value',this.value)"><option value="0" ${Number(a.value)===0?'selected':''}>Open relay (0)</option><option value="1" ${Number(a.value)===1?'selected':''}>Close relay (1)</option></select></div>`;
   html += `<div></div>`;
   html += `</div>`;
   html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationRemoveAction(${ri},${ai})">Remove Action</button></div>`;
   html += `</div>`;
  });
  html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationAddAction(${ri})">Add Action</button><button type="button" onclick="automationDeleteRule(${ri})">Delete Rule</button></div>`;
  html += `</div>`;
 });
 host.innerHTML = html;
 refreshAutomationsJsonPreview();
}
function automationSetRuleField(ri,key,val){
 if(!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
 automationsDoc.rules[ri][key]=String(val||'');
 refreshAutomationsJsonPreview();
}
function automationSetRuleEnabled(ri,val){
 if(!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
 automationsDoc.rules[ri].enabled=!!val;
 refreshAutomationsJsonPreview();
}
function automationSetRuleMs(ri,key,valSec){
 if(!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
 const sec=Math.max(0, Number(valSec)||0);
 automationsDoc.rules[ri][key]=Math.round(sec*1000);
 refreshAutomationsJsonPreview();
}
function automationSetPredicateField(ri,pi,key,val){
 const pred=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when &&
             automationsDoc.rules[ri].when.all) ? automationsDoc.rules[ri].when.all[pi] : null;
 if(!pred) return;
 if(key==='field'){
  pred.field=String(val||'input');
  automationNormalizeFieldDefaults(pred);
  renderAutomationsRules();
  return;
 }
 if(key==='value'){
  if(pred.field==='temp_c'){ pred.value = Number(val); if(!Number.isFinite(pred.value)) pred.value = 0; }
  else if(pred.field==='reachable'){ pred.value = ['1','true','yes','on'].includes(String(val).trim().toLowerCase()); }
  else { pred.value = (Number(val)===1) ? 1 : 0; }
 } else {
  pred[key]=String(val||'');
 }
 refreshAutomationsJsonPreview();
}
function automationSetActionField(ri,ai,key,val){
 const act=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].then)
   ? automationsDoc.rules[ri].then[ai] : null;
 if(!act) return;
 if(key==='value') act.value = (Number(val)===1) ? 1 : 0;
 else act[key]=String(val||'');
 refreshAutomationsJsonPreview();
}
function automationAddPredicate(ri){
 const arr=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when)
   ? automationsDoc.rules[ri].when.all : null;
 if(!arr || arr.length>=4) return;
 arr.push(defaultAutomationPredicate());
 renderAutomationsRules();
}
function automationRemovePredicate(ri,pi){
 const arr=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when)
   ? automationsDoc.rules[ri].when.all : null;
 if(!arr) return;
 arr.splice(pi,1);
 if(!arr.length) arr.push(defaultAutomationPredicate());
 renderAutomationsRules();
}
function automationAddAction(ri){
 const arr=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri]) ? automationsDoc.rules[ri].then : null;
 if(!arr || arr.length>=4) return;
 arr.push(defaultAutomationAction());
 renderAutomationsRules();
}
function automationRemoveAction(ri,ai){
 const arr=(automationsDoc && automationsDoc.rules && automationsDoc.rules[ri]) ? automationsDoc.rules[ri].then : null;
 if(!arr) return;
 arr.splice(ai,1);
 if(!arr.length) arr.push(defaultAutomationAction());
 renderAutomationsRules();
}
function automationDeleteRule(ri){
 if(!automationsDoc || !automationsDoc.rules) return;
 automationsDoc.rules.splice(ri,1);
 renderAutomationsRules();
}
function addAutomationRule(){
 if(!automationsDoc) automationsDoc = defaultAutomationsDoc();
 if(!Array.isArray(automationsDoc.rules)) automationsDoc.rules = [];
 if(automationsDoc.rules.length>=8){
  automationShowResult('Max rules is 8 in v1.', 'err');
  return;
 }
 automationsDoc.rules.push(defaultAutomationRule(automationsDoc.rules.length));
 renderAutomationsRules();
}
function refreshAutomationsJsonPreview(){
 if(!automationsDoc) return;
 const out=document.getElementById('auto_json_preview');
 if(!out) return;
 out.value = JSON.stringify(automationsDoc, null, 2);
}
function applyAutomationsDocToForm(){
 if(!automationsDoc) automationsDoc = defaultAutomationsDoc();
 const d=automationsDoc;
 const enabledEl=document.getElementById('auto_enabled');
 const modeEl=document.getElementById('auto_execution_mode');
 const peerDisplayEl=document.getElementById('auto_peer_display');
 const targetEl=document.getElementById('auto_action_target');
 if(enabledEl) enabledEl.checked = !!d.enabled;
 if(modeEl) modeEl.value = d.execution_mode==='paired+rules' ? 'paired+rules' : 'standalone';
 if(peerDisplayEl) peerDisplayEl.value = d.peer_display==='names' ? 'names' : 'addresses';
 if(targetEl) targetEl.value = String(d.action_target === undefined || d.action_target === null ? 'self' : d.action_target);
 renderAutomationsRules();
}
async function loadAutomationsPageData(force){
 if(!UI_AUTOMATIONS_ENABLED) return;
 if(automationsPageLoadInFlight) return;
 if(automationsPageLoaded && !force) return;
 automationsPageLoadInFlight = true;
 automationShowResult(force ? 'Reloading automations...' : 'Loading automations...', '');
 try{
  const out=await apiJson('/api/automation-rules',{silent:true,timeoutMs:7000});
  if(!out){ automationShowResult('Automation rules unavailable.', 'err'); return; }
  if(out && out.ok===false){ automationShowResult(automationFriendlyApiError(out), 'err'); return; }
  automationsDoc = normalizeAutomationsDoc(out);
  applyAutomationsDocToForm();
  automationsPageLoaded = true;
  automationShowResult('Automations loaded.', 'ok');
 }catch(e){
  automationShowResult(`Load failed: ${e.message||e}`, 'err');
 }finally{
  automationsPageLoadInFlight = false;
 }
}
function reloadAutomations(){ automationsPageLoaded = false; return loadAutomationsPageData(true); }
async function saveAutomations(){
 if(!UI_AUTOMATIONS_ENABLED){ automationShowResult('Automations feature disabled in this firmware build.', 'err'); return; }
 if(!automationsDoc) automationsDoc = defaultAutomationsDoc();
 automationTopChanged();
 const validationErrors = automationValidateDoc(automationsDoc);
 if(validationErrors.length){
  automationShowResult(validationErrors[0], 'err');
  return;
 }
 automationShowResult('Saving automations...', '');
 try{
  const body = JSON.stringify(automationsDoc);
  const out = await apiJson('/api/automation-rules',{method:'POST',headers:{'Content-Type':'application/json'},body,timeoutMs:8000,silent:true});
  if(out && out.ok){
   automationsPageLoaded = true;
   automationShowResult(`Automation rules saved${out.saved_bytes?` (${out.saved_bytes} bytes)`:''}.`, 'ok');
   return;
  }
  automationShowResult(automationFriendlyApiError(out), 'err');
 }catch(e){
  automationShowResult(`Save failed: ${e.message||e}`, 'err');
 }
}
function applyAutomationsJsonFromPreview(){
 if(!UI_AUTOMATIONS_ENABLED) return;
 const ta=document.getElementById('auto_json_preview');
 if(!ta) return;
 try{
  const parsed=JSON.parse(ta.value||'{}');
  automationsDoc = normalizeAutomationsDoc(parsed);
  applyAutomationsDocToForm();
  automationShowResult('JSON applied to form.', 'ok');
 }catch(e){
  automationShowResult(`Invalid JSON: ${e.message||e}`, 'err');
 }
}
async function copyAutomationJsonPreview(){
 if(!UI_AUTOMATIONS_ENABLED) return;
 const ta=document.getElementById('auto_json_preview');
 if(!ta) return;
 try{
  await writeClipboard(String(ta.value||''));
  showToast('Automation JSON copied');
 }catch(e){
  showToast(`Copy failed: ${e.message}`, true);
 }
}
function showPage(page){
 const allowed = UI_AUTOMATIONS_ENABLED
  ? ['status','fleet','automations','sensors','settings']
  : ['status','fleet','sensors','settings'];
 activePage = allowed.includes(page) ? page : 'status';
 allowed.forEach(p=>{
  const sec=document.getElementById(`page-${p}`);
  const nav=document.getElementById(`nav-${p}`);
  if(sec) sec.classList.toggle('active', p===activePage);
  if(nav) nav.classList.toggle('active', p===activePage);
 });
 if(activePage==='settings'){ showSettingsTab(activeSettingsTab); loadSettingsPageData(false).catch(()=>{}); scanWifi().catch(()=>{}); }
 if(activePage==='automations'){ loadAutomationsPageData(false).catch(()=>{}); }
 if(activePage==='status'){
  statusStaticCache = null;
  statusStaticLoadInFlight = false;
  statusLiveHasLiveData = false;
  statusDegradedLiteMode = false;
  statusLiveSseLastMessageMs = 0;
  ensureStatusStatic(true).catch(()=>{});
  refreshStatusLiveNotice();
 }
 if(activePage==='fleet'){ showFleetTab(activeFleetTab); }
 applyAutomationsFeatureVisibility();
 toggleDrawer(false);
 syncPagePolling();
 updateMobileActionBar();
}
function applyFleetTabVisibility(){
 const fleetTabBtn=document.getElementById('nav-fleet');
 if(fleetTabBtn){ fleetTabBtn.style.display = lastRoleIsTx ? '' : 'none'; }
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
 const icons = document.querySelectorAll('.theme-toggle');
 icons.forEach((btn)=>{ btn.innerText = currentTheme === 'dark' ? '☀' : '🌙'; });
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
  chip_id: String((lite && lite.chip_id) || ''),
  factory_serial: String((lite && lite.factory_serial) || ''),
  mode: String((lite && lite.mode) || 'paired'),
  role_name: String((lite && lite.role_name) || ''),
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
  lan_hostname: String((lite && lite.lan_hostname) || ''),
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
 const el=document.getElementById('statusLiveState');
 if(!el) return;
 const t = String(text || '');
 let dot = '⚪';
 if(t.includes('connected')){
  dot = '🟢';
 }else if(t.includes('disconnected')){
  dot = '🔴';
 }else if(t.includes('paused') || t.includes('waiting') || t.includes('reconnect') || t.includes('limited')){
  dot = '🟡';
 }
 el.innerText = dot;
 el.title = t;
 el.setAttribute('aria-label', t || 'status updates');
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
 const modeRaw = String(st.mode || 'paired').toLowerCase();
 const roleRaw = String(st.role_name || '').toLowerCase();
 const modeDisplay = modeRaw === 'mesh' ? 'Mesh' : (modeRaw === 'standalone' ? 'Standalone' : 'Paired');
 let roleDisplayName = roleRaw;
 if(!roleDisplayName){
  roleDisplayName = roleIsTx ? 'transmitter' : 'receiver';
 }
 if(roleDisplayName === 'transmitter') roleDisplayName = 'Transmitter';
 else if(roleDisplayName === 'receiver') roleDisplayName = 'Receiver';
 else if(roleDisplayName === 'coordinator') roleDisplayName = 'Coordinator';
 else if(roleDisplayName === 'node') roleDisplayName = 'Node';
 else if(roleDisplayName === 'none') roleDisplayName = 'None';
 const roleDisplay = `${modeDisplay} / ${roleDisplayName}`;
 const addressDisplay = modeRaw === 'paired'
   ? `tx ${st.local_address}, rx ${st.remote_address}`
   : `local ${st.local_address}, peer ${st.remote_address}`;
 const localAddrHex = `0x${Number(st.local_address || 0).toString(16).toUpperCase()}`;
 const remoteAddrHex = `0x${Number(st.remote_address || 0).toString(16).toUpperCase()}`;
 const addressHexDisplay = modeRaw === 'paired'
   ? `tx ${localAddrHex}, rx ${remoteAddrHex}`
   : `local ${localAddrHex}, peer ${remoteAddrHex}`;
 const loraRssiText = hasLora ? `${st.lora_last_rssi} dBm` : 'n/a';
 const loraLastText = hasLora ? humanAgeMsShort(loraAgoMs) : '—';
 const loraLastTxText = hasLoraTx ? humanAgeMsShort(loraTxAgoMs) : '—';
 const linkStateRaw = String(st.link_state || 'unknown').toLowerCase();
 let linkEmoji = '⚪';
 if(linkStateRaw==='ok' || linkStateRaw==='linked' || linkStateRaw==='connected' || linkStateRaw==='active'){
  linkEmoji = '🟢';
 }else if(linkStateRaw==='timeout' || linkStateRaw==='lost' || linkStateRaw==='down' || linkStateRaw==='error' || linkStateRaw==='failed'){
  linkEmoji = '🔴';
 }else if(linkStateRaw==='syncing' || linkStateRaw==='searching' || linkStateRaw==='degraded'){
  linkEmoji = '🟡';
 }
 const linkLine = `${linkEmoji} ${String(st.link_state || 'unknown')} • RSSI ${loraRssiText}`;
 const loraActivityLine = `tx ${loraLastTxText} • rx ${loraLastText}`;
 const relayReasonRaw = String(st.relay_reason || '').toLowerCase();
 let relaySource = '';
 if(relayReasonRaw.startsWith('mqtt_')) relaySource = 'mqtt command';
 else if(relayReasonRaw.startsWith('automation_')) relaySource = 'automation';
 else if(relayReasonRaw.startsWith('lora_')) relaySource = 'LoRa command';
 else if(relayReasonRaw.startsWith('input_')) relaySource = 'input';
 staIsConnected = !!st.sta_connected;
 currentStaIp = String(st.sta_ip || '');
 currentLanMdns = LRS_ENABLE_MDNS ? String(st.mdns_lan || (st.lan_hostname ? `${st.lan_hostname}.local` : '')) : '';
 currentApIp = String(st.ap_ip || '');
 currentApMdns = LRS_ENABLE_MDNS ? String(st.mdns_ap || 'lrs.local') : '';
 if(staIsConnected){
  const connectedSsid=String(st.sta_ssid || '');
  if(connectedSsid.length){
   connectedStaSsid = connectedSsid;
  }
 }else{
  connectedStaSsid = '';
 }
 updateStaTestButtonState();
 const lanHost = LRS_ENABLE_MDNS ? String(st.mdns_lan || (st.lan_hostname ? `${st.lan_hostname}.local` : '')).replace(/\/+$/,'') : '';
 const apHost = LRS_ENABLE_MDNS ? String(st.mdns_ap || 'lrs.local').replace(/\/+$/,'') : '';
 const apUrl = apHost ? `http://${apHost}/` : '';
 const lanUrl = lanHost ? `http://${lanHost}/` : '';
 const lanMdnsHtml = lanUrl ? `<a class="link" href="${escapeHtml(lanUrl)}">${escapeHtml(lanUrl)}</a>` : 'n/a';
 const apMdnsHtml = apUrl ? `<a class="link" href="${escapeHtml(apUrl)}">${escapeHtml(apUrl)}</a>` : 'n/a';
 const statusMdnsRows = LRS_ENABLE_MDNS
   ? `<div class="k">LAN mDNS URL</div><div class="v copyable">${copyableValueHtml(lanMdnsHtml, lanUrl, 'LAN mDNS URL')}</div>`
   : '';
 const apMdnsRow = LRS_ENABLE_MDNS
   ? `<div class="k">AP mDNS URL</div><div class="v copyable">${copyableValueHtml(apMdnsHtml, apUrl, 'AP mDNS URL')}</div>`
   : '';
 const rb=document.getElementById('relayBadge');
 if(rb){
  rb.className = `relay-badge ${relayOn ? 'on' : 'off'}`;
  rb.innerText = relayOn ? 'RELAY ON' : 'RELAY OFF';
 }
 const rm=document.getElementById('relayMeta');
 if(rm){
  const relayMeta = relaySource ? `Link: ${linkEmoji} ${st.link_state} • via ${relaySource}` : `Link: ${linkEmoji} ${st.link_state}`;
  rm.innerText = relayMeta;
 }
 const table=document.getElementById('statusTable');
 const deployKey=String(st.deployment_key || '');
 const footerFw=document.getElementById('footerFw');
 if(footerFw){
  footerFw.innerText = `FW: ${String(st.fw_display || st.fw_version || '-')}`;
 }
 if(table){
 table.className='status-table';
  table.innerHTML=
   `<div class="section">LoRa</div>
    <div class="k">Role</div><div class="v">${escapeHtml(roleDisplay)}</div>
    <div class="k">Address</div><div class="v copyable">${copyableValueHtml(`${escapeHtml(addressDisplay)}<div class="small">${escapeHtml(addressHexDisplay)}</div>`, addressDisplay, 'Address')}</div>
    <div class="k">Fleet key</div><div class="v copyable">${copyableValueHtml(escapeHtml(deployKey || 'not_set'), deployKey, 'Fleet key')}</div>
    <div class="k">Link</div><div class="v">${escapeHtml(linkLine)}</div>
    <div class="k">Activity</div><div class="v">${escapeHtml(loraActivityLine)}</div>
    <div class="k">Relay reason</div><div class="v">${escapeHtml(reasonLabel(st.relay_reason))}</div>
    <div class="section">WiFi Station</div>
    <div class="k">STA</div><div class="v"><span class="sta-line">${escapeHtml(st.sta_ssid || st.sta_target_ssid || 'not configured')}<span class="sta-dot ${st.sta_connected ? 'on' : 'off'}" title="${st.sta_connected ? 'connected' : 'not connected'}"></span></span><div class="small">${escapeHtml(st.sta_connected ? `${st.sta_rssi} dBm` : 'not connected')}</div></div>
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
function applyHeaderStatus(st){
 if(!st) return;
 const footerMem=document.getElementById('footerMem');
 const modeRaw = String(st.mode || 'paired').toLowerCase();
 const titleEl = document.getElementById('consoleTitle');
 const hostLabel = String(st.lan_hostname || st.mdns_lan || '').replace(/\.local$/i,'').trim();
 const headerTitle = hostLabel || `lrs-${String(st.chip_id || '').trim()}`;
 if(titleEl){ titleEl.innerText = headerTitle; }
 document.title = headerTitle || 'LRS Console';
 const chipId = String(st.chip_id || '').trim();
 const serial = String(st.factory_serial || '').trim();
 const identity = serial || (chipId ? `lrs-${chipId}` : '-');
 const statusHeadText=document.getElementById('statusHeadDeviceText');
 const statusHeadCopy=document.getElementById('statusHeadDeviceCopy');
 if(statusHeadText){ statusHeadText.innerText = `Device: ${identity}`; }
 if(statusHeadCopy){
  statusHeadCopy.dataset.copy = identity;
  statusHeadCopy.dataset.label = 'Device identity';
 }
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
  rh.innerText=relayOn ? '🟢' : '⚪';
  rh.title=relayOn ? 'Relay on' : 'Relay off';
  rh.setAttribute('aria-label', relayOn ? 'Relay on' : 'Relay off');
 }
 if(footerMem){
  const heapBytes=Number(st.heap_free_bytes||0);
  const maxBlockBytes=Number(st.max_free_block_bytes||0);
  const heapFrag=Number(st.heap_frag_percent||0);
  const heapK = heapBytes>0 ? (heapBytes/1024) : 0;
  const maxK = maxBlockBytes>0 ? (maxBlockBytes/1024) : 0;
  const heapTxt = heapBytes>0 ? (heapK>=10 ? String(Math.round(heapK)) : heapK.toFixed(1)) : '-';
  const maxTxt = maxBlockBytes>0 ? (maxK>=10 ? String(Math.round(maxK)) : maxK.toFixed(1)) : '-';
  footerMem.innerText = (heapBytes>0 && maxBlockBytes>0) ? `Mem ${heapTxt}/${maxTxt}` : 'Mem -/-';
  if(heapBytes>0 || maxBlockBytes>0){
   footerMem.title = `Free heap: ${heapBytes} B | Max block: ${maxBlockBytes} B | Frag: ${heapFrag}%`;
  }else{
   footerMem.title = 'Memory metrics unavailable';
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
 try{ headerStatusRefreshInFlight = false; }catch(e){}
 try{ fleetRefreshInFlight = false; }catch(e){}
}
async function apiJson(url, options){
 let t=null;
 try{
  const merged=Object.assign({cache:'no-store',silent:false,timeoutMs:8000}, options||{});
  const controller = new AbortController();
  const timeoutMs = Number(merged.timeoutMs || 8000);
  const allowHttpError = !!merged.allowHttpError;
  t = setTimeout(()=>controller.abort(), timeoutMs);
  delete merged.timeoutMs;
  delete merged.allowHttpError;
  merged.signal = controller.signal;
  const res=await fetch(url,merged);
  if(res.status===401){
   stopAllUiPollingForAuthExpiry();
   location.href='/login?expired=1';
   return null;
  }
  if(!res.ok){
   if(allowHttpError){
    const out = await res.json();
    if(out && typeof out === 'object' && out._http_status == null){ out._http_status = res.status; }
    return out;
   }
   throw new Error(`HTTP ${res.status}`);
  }
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
  applyAutomationsFeatureVisibility();
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
  const fleetEl=document.getElementById('fleet_passphrase');
  if(fleetEl){
   fleetEl.value='';
   fleetEl.placeholder = s.fleet_passphrase_set ? 'Stored (hidden). Enter new value to change.' : '';
  }
  const staPassEl=document.getElementById('wifi_sta_password');
  if(staPassEl){
   staPassEl.value='';
   staPassEl.placeholder = s.wifi_sta_password_set ? 'Stored (hidden). Enter new value to change.' : '';
  }
  const mqttPassEl=document.getElementById('mqtt_password');
  if(mqttPassEl){
   mqttPassEl.value='';
   mqttPassEl.placeholder = s.mqtt_password_set ? 'Stored (hidden). Enter new value to change.' : '';
  }
  const adminPassEl=document.getElementById('admin_password');
  if(adminPassEl){
   adminPassEl.value='';
   adminPassEl.placeholder = s.admin_password_set ? 'Stored (hidden). Enter new value to change.' : '';
  }
  updateDeploymentKeyStrength();
  updateStaTestButtonState();
  const modeSelect=document.getElementById('mode_select');
  if(modeSelect){
   modeSelect.value = String(s.mode || 'paired');
  }
  document.getElementById('role_tx').value = String(!!s.role_tx);
  document.getElementById('role_tx_true').checked = !!s.role_tx;
  document.getElementById('role_tx_false').checked = !s.role_tx;
  document.getElementById('ap_always_on').checked = !!s.ap_always_on;
  document.getElementById('mqtt_client_enabled').checked = !!s.mqtt_client_enabled;
  document.getElementById('mqtt_control_enabled').checked = !!s.mqtt_control_enabled;
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
async function scanWifi(){
 if(wifiScanInFlight) return;
 const btn=document.getElementById('wifiScanBtn');
 const host=document.getElementById('wifi_scan_list');
 if(!host) return;
 wifiScanInFlight = true;
 if(btn){ btn.disabled=true; btn.innerText='Scanning...'; }
 host.innerHTML='Scanning...';
 try{
  const start=Date.now();
  const timeoutMs=15000;
  let out=null;
  while((Date.now()-start) < timeoutMs){
   out=await apiJson('/api/wifi/scan',{silent:true,timeoutMs:5000});
   if(!out){ host.innerHTML='Scan failed'; return; }
   if(String(out.status||'')==='ready'){ break; }
   await sleep(500);
  }
  if(!out || String(out.status||'')!=='ready'){
   host.innerHTML='Scan timed out';
   return;
  }
  if(!out.networks || !out.networks.length){
   host.innerHTML='No SSIDs found';
   return;
  }
  out.networks.sort((a,b)=>Number(b.rssi)-Number(a.rssi));
  host.innerHTML='<table class="wifi-table"><thead><tr><th>SSID</th><th>Signal</th><th></th></tr></thead><tbody></tbody></table>';
  const tbody=host.querySelector('tbody');
  out.networks.forEach(n=>{
   const tr=document.createElement('tr');
   tr.innerHTML=`<td>${escapeHtml(n.ssid)}</td><td>${sigIconHtml(n.rssi,'scan')}${escapeHtml(n.rssi)} dBm</td><td><button type="button" data-ssid="${escapeHtml(n.ssid)}">Use</button></td>`;
   tbody.appendChild(tr);
  });
  host.querySelectorAll('button[data-ssid]').forEach(useBtn=>{
   useBtn.addEventListener('click',()=>{
    document.getElementById('wifi_sta_ssid').value=useBtn.getAttribute('data-ssid');
    updateStaTestButtonState();
   });
  });
 }catch(e){
  host.innerHTML='Scan failed';
 } finally {
  wifiScanInFlight = false;
  if(btn){ btn.disabled=false; btn.innerText='Rescan SSIDs'; }
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
 const mode=String((document.getElementById('mode_select')||{}).value || 'paired');
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
 if(mode === 'paired' && (!Number.isFinite(hbSec)||hbSec<60||hbSec>3600)){alert('Heartbeat must be between 60 and 3600 seconds.'); return;}
 if(!Number.isFinite(ackSec)||ackSec<5||ackSec>600){alert('ACK timeout must be between 5 and 600 seconds.'); return;}
 if(!Number.isFinite(mqttRetrySec)||mqttRetrySec<5||mqttRetrySec>3600){alert('MQTT remote retry timeout must be between 5 and 3600 seconds.'); return;}
 if(!Number.isFinite(txPollDefaultSec)||txPollDefaultSec<60||txPollDefaultSec>3600){alert('Default poll interval must be between 60 and 3600 seconds.'); return;}
 if(!Number.isFinite(rxPushMinSec)||rxPushMinSec<60||rxPushMinSec>3600){alert('RX push minimum interval must be between 60 and 3600 seconds.'); return;}
 const ids=['lora_tx_power','lora_spreading_factor','lora_bandwidth_hz','lora_coding_rate'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 const fleetPassphrase=String(document.getElementById('fleet_passphrase').value||'').trim();
 if(fleetPassphrase.length){
  if(fleetPassphrase.length < MIN_DEPLOYMENT_KEY_LEN){
    alert(`Deployment Key must be at least ${MIN_DEPLOYMENT_KEY_LEN} characters.`);
    return;
  }
  if(isDefaultDeploymentKey(fleetPassphrase)){
    alert('Deployment Key cannot be the default value. Please set a unique installation key.');
    return;
  }
  body.fleet_passphrase=fleetPassphrase;
 }
 const roleName=String((document.getElementById('role_name')||{}).value || 'transmitter');
 body.mode=mode;
 body.role=roleName;
 body.role_tx=document.getElementById('role_tx_true').checked;
 body.local_address=local;
 body.remote_address=remote;
 body.input_control_paired_lora_enabled=document.getElementById('input_control_paired_lora_enabled').checked;
 body.tx_mqtt_remote_polling_enabled=document.getElementById('tx_mqtt_remote_polling_enabled').checked;
 body.rx_push_on_change_enabled=document.getElementById('rx_push_on_change_enabled').checked;
 body.lora_frequency_hz=Math.round(mhz*1000000);
 body.heartbeat_ms=(mode === 'paired') ? (hbSec*1000) : 60000;
 body.ack_timeout_ms=ackSec*1000;
 body.mqtt_remote_retry_timeout_ms=mqttRetrySec*1000;
 body.tx_mqtt_remote_default_poll_interval_ms=txPollDefaultSec*1000;
 body.rx_push_min_interval_ms=rxPushMinSec*1000;
 return body;
}
function collectNetworkBody(){
 const ids=['wifi_sta_ssid','lan_hostname'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 const staPass=String(document.getElementById('wifi_sta_password').value||'');
 if(staPass.length){ body.wifi_sta_password=staPass; }
 body.ap_always_on=document.getElementById('ap_always_on').checked;
 return body;
}
function collectSystemBody(){
 const body={};
 const adminPass=String(document.getElementById('admin_password').value||'');
 if(adminPass.length){ body.admin_password=adminPass; }
 return body;
}
function collectMqttBody(){
 const ids=['mqtt_host','mqtt_user','mqtt_topic_root','mqtt_controller_addresses'];
 const body={}; ids.forEach(id=>body[id]=document.getElementById(id).value);
 const mqttPass=String(document.getElementById('mqtt_password').value||'');
 if(mqttPass.length){ body.mqtt_password=mqttPass; }
 const portRaw=String(document.getElementById('mqtt_port').value||'').trim();
 const port=Number(portRaw);
 if(portRaw.length===0 || !Number.isFinite(port) || port<1 || port>65535){
  alert('MQTT port must be in range 1..65535.');
  return null;
 }
 body.mqtt_port=Math.floor(port);
 body.mqtt_client_enabled=document.getElementById('mqtt_client_enabled').checked;
 body.mqtt_control_enabled=document.getElementById('mqtt_control_enabled').checked;
 if(body.mqtt_control_enabled && !body.mqtt_client_enabled){
  alert('MQTT control enabled requires MQTT client enabled.');
  return null;
 }
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
 if(LRS_ENABLE_MDNS){
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
 const confirmEl=document.getElementById('factory_reset_confirm_word');
 const keepWifiEl=document.getElementById('factory_reset_keep_wifi_local');
 if(!el || !pwEl || !confirmEl || !keepWifiEl) return;
 const password=String(pwEl.value||'');
 const confirmWord=String(confirmEl.value||'').trim().toUpperCase();
 let keepFleet=false;
 if(confirmWord==='RESET') keepFleet=true;
 else if(confirmWord==='REMOVE') keepFleet=false;
 const keepWifi=!!keepWifiEl.checked;
 if(!password.length){
  el.className='result-line show err';
  el.innerText='Enter admin password to factory reset.';
  return;
 }
 if(confirmWord!=='RESET' && confirmWord!=='REMOVE'){
  el.className='result-line show err';
  el.innerText='Type RESET (keep fleet key) or REMOVE (clear fleet key).';
  return;
 }
 const keepBits=[];
 if(keepFleet) keepBits.push('shared fleet key');
 if(keepWifi) keepBits.push('WiFi credentials');
 el.className='result-line show';
 el.innerText=`Factory reset requested${keepBits.length ? ` (keep ${keepBits.join(' + ')})` : ''}...`;
 const out=await apiJson('/api/system/factory-reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({admin_password:password,keep_shared_fleet_key:keepFleet,keep_wifi_credentials:keepWifi}),silent:true});
 if(out && out.ok){
  el.className='result-line show ok';
  el.innerText='Factory reset started. Device is rebooting...';
  pwEl.value='';
  confirmEl.value='';
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
  const apHint=currentApIp ? `http://${currentApIp}` : (LRS_ENABLE_MDNS && currentApMdns ? `http://${currentApMdns}` : 'the Soft AP URL');
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
function provisioningDeviceStatusDisplay(d){
 const state=String((d&&d.state)||'unknown');
 if(state==='verified') return '🟢 verified';
 if(state==='failed'){
  // If an address was assigned, this is often a verify-timeout/missed final ack on LoRa.
  // The target may already have applied provisioning successfully.
  if(Number(d&&d.assigned_address||0)>0) return '🟠 unverified';
  return '🔴 failed';
 }
 if(state==='await_verify') return '🟠 await verify';
 return state;
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
  return `<tr><td>${escapeHtml(String(d.chip_id_hex||d.chip_id||''))}</td><td>${cur||'-'}</td><td>${nxt||'-'}</td><td>${escapeHtml(String(fw))}</td><td>${Number(d.rssi||0)}</td><td>${escapeHtml(provisioningDeviceStatusDisplay(d))}${conflict}</td></tr>`;
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
 return 1500;
}
function headerPollDelayMs(){
 if(document.hidden) return 8000;
 if(activePage==='fleet' && activeFleetTab==='manage' && activeFleetManageTab==='lora') return 8000;
 return 6000;
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
 }else{
  return;
 }
 const loop=async()=>{
  if(generation !== pagePollGeneration) return;
  if(document.hidden){
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
 const est=Math.max(1, Math.min(8, Number(estEl && estEl.value || 8) || 8));
 const retry=false;
 suspendGlobalPollsUntilMs = Date.now() + 5000;
 if(result){ result.className='result-line show'; result.innerText = searchMore ? 'Searching for more devices...' : 'Starting discovery...'; }
 const out=await apiJson('/api/provisioning/start',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({estimated_count:est,retry_once:retry}),silent:true,allowHttpError:true});
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
 const out=await apiJson('/api/provisioning/provision-all',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',silent:true,allowHttpError:true});
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
 await apiJson('/api/provisioning/cancel',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}',silent:true,allowHttpError:true});
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
function updateFleetScanUi(scan){
 const st = scan && typeof scan === 'object' ? scan : {};
 fleetScanState = Object.assign({}, fleetScanState || {}, st);
 const active = !!fleetScanState.active;
 const start = Number(fleetScanState.start_address||1);
 const end = Number(fleetScanState.end_address||80);
 const nextAddr = Number(fleetScanState.next_address||start);
 const intervalMs = Number(fleetScanState.interval_ms||120);
 const sent = Number(fleetScanState.sent||0);
 const total = Number(fleetScanState.total||Math.max(0, end - start + 1));
 const scanned = Number(fleetScanState.scanned||0);
 const pct = Number(fleetScanState.progress_pct||0);
 const startEl=document.getElementById('fleetScanStart');
 const endEl=document.getElementById('fleetScanEnd');
 const intEl=document.getElementById('fleetScanIntervalMs');
 const btn=document.getElementById('fleetScanBtn');
 const summary=document.getElementById('fleetScanSummary');
 if(startEl && !active && Number.isFinite(start) && start>=1 && start<=254) startEl.value = start;
 if(endEl && !active && Number.isFinite(end) && end>=1 && end<=254) endEl.value = end;
 if(intEl && !active && Number.isFinite(intervalMs)) intEl.value = intervalMs;
 if(startEl) startEl.disabled = active;
 if(endEl) endEl.disabled = active;
 if(intEl) intEl.disabled = active;
 if(btn) btn.innerText = active ? 'Cancel Scan' : 'Scan Fleet';
 if(summary){
  if(active){
   summary.innerText = `Scan running: ${Math.max(0, Math.min(100, pct))}% · sent ${sent}/${Math.max(0, total)} · next ${toHexByte(Math.max(1, Math.min(254, nextAddr)))}`;
  }else{
   summary.innerText = (sent > 0 && total > 0)
    ? `Last scan: ${Math.max(0, Math.min(100, pct))}% · sent ${sent}/${total}.`
    : 'Scan idle.';
  }
 }
}
async function toggleFleetScan(){
 const active = !!(fleetScanState && fleetScanState.active);
 const body = {};
 if(active){
  body.cancel = true;
 }else{
  const startEl=document.getElementById('fleetScanStart');
  const endEl=document.getElementById('fleetScanEnd');
  const intEl=document.getElementById('fleetScanIntervalMs');
  let startAddr=Math.floor(Number(startEl && startEl.value));
  let endAddr=Math.floor(Number(endEl && endEl.value));
  let intervalMs=Math.floor(Number(intEl && intEl.value));
  if(!Number.isFinite(startAddr) || startAddr<1 || startAddr>254){ alert('Start address must be 1..254.'); return; }
  if(!Number.isFinite(endAddr) || endAddr<1 || endAddr>254){ alert('End address must be 1..254.'); return; }
  if(startAddr > endAddr){ alert('Start address must be <= end address.'); return; }
  if(!Number.isFinite(intervalMs) || intervalMs < 80 || intervalMs > 2000){ alert('Scan interval must be 80..2000 ms.'); return; }
  body.start_address = startAddr;
  body.end_address = endAddr;
  body.interval_ms = intervalMs;
 }
 const out=await apiJson('/api/fleet/scan',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),silent:true,allowHttpError:true});
 if(!out || out.ok !== true){
  const err=(out && out.error) ? out.error : 'request_failed';
  showToast(`Fleet scan failed: ${err}`, true);
  return;
 }
 updateFleetScanUi(out);
 await refreshFleet();
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
 updateFleetScanUi(out.scan || {});
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
 const modeSelect=document.getElementById('mode_select');
  const local=document.getElementById('local_address');
  const remote=document.getElementById('remote_address');
 const host=document.getElementById('lan_hostname');
 const staSsid=document.getElementById('wifi_sta_ssid');
 const staPass=document.getElementById('wifi_sta_password');
 const roleTx=document.getElementById('role_tx_true');
 const roleRx=document.getElementById('role_tx_false');
 const fleetKey=document.getElementById('fleet_passphrase');
 mobileActionBarEl=document.getElementById('mobileActionBar');
 mobileActionPrimaryEl=document.getElementById('mobileActionPrimary');
 mobileActionSecondaryEl=document.getElementById('mobileActionSecondary');
 const syncRole=()=>{
  if(!role) return;
  const mode = String((modeSelect && modeSelect.value) || 'paired');
  if(mode === 'standalone'){
   if(roleTx) roleTx.checked = true;
   if(roleRx) roleRx.checked = false;
   if(roleRx) roleRx.disabled = true;
  }else{
   if(roleRx) roleRx.disabled = false;
  }
  role.value = roleTx && roleTx.checked ? 'true' : 'false';
  refreshRoleLabels();
 };
 if(roleTx) roleTx.addEventListener('change',syncRole);
 if(roleRx) roleRx.addEventListener('change',syncRole);
 if(modeSelect) modeSelect.addEventListener('change',syncRole);
 if(local) local.addEventListener('input',refreshAddressHints);
 if(remote) remote.addEventListener('input',refreshAddressHints);
 if(host && LRS_ENABLE_MDNS) host.addEventListener('input',refreshHostnamePreview);
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
 window.addEventListener('resize', ()=>{ updateMobileActionBar(); });
 showPage('status');
 window.addEventListener('beforeunload', ()=>{ stopProvisioningPolling(); stopPagePolling(); stopHeaderPolling(); closeStatusLiveSse(); });
 document.addEventListener('visibilitychange', ()=>{ syncPagePolling(); });
 load();
 syncPagePolling();
}
initPage();
</script></body></html>
)HTML";
