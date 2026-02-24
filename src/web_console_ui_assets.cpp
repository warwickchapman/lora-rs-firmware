#include "web_console_ui_assets.h"

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
<title>LRS Console Unavailable (Low Memory)</title>
<style>
:root{--bg:#08101d;--card:#0f1a2e;--txt:#e5e7eb;--muted:#94a3b8;--border:#31435f;--btn:#005f73;--ok:#1f8f5f;--warn:#b7791f;--crit:#b42318}
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
.banner{border-left:4px solid var(--warn);background:#16131d;padding:10px 12px;border-radius:10px;color:#ffe8bf}
.metric.ok{border-color:rgba(31,143,95,.65)} .metric.warn{border-color:rgba(183,121,31,.7)} .metric.crit{border-color:rgba(180,35,24,.8)}
.metric.ok .v{color:#7ee0b7} .metric.warn .v{color:#ffd08a} .metric.crit .v{color:#ffb6ae}
.small{font-size:12px;color:var(--muted)}
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
 const ids=['mqtt_host','mqtt_user','mqtt_topic_root'];
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

