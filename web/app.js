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
let activeFleetTab = 'manage';
let activeFleetManageTab = 'lora';
let headerStatusRefreshInFlight = false;
let sessionRefreshInFlight = false;
let fleetRefreshInFlight = false;
let fleetRefreshDebounceTimer = 0;
let fleetScanState = { active: false, start_address: 1, end_address: 32, next_address: 1, interval_ms: 120, sent: 0, total: 0, scanned: 0, progress_pct: 0 };
let fleetLandingDecisionToken = 0;
let provStatusInFlight = false;
let provLastStatusRefreshMs = 0;
let provUiSessionActive = false;
let provUiSessionState = 'idle';
let settingsPageLoaded = false;
let settingsPageLoadInFlight = false;
let wifiScanInFlight = false;
let wifiProvisionResultTimer = 0;
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

function parseAddress(v) {
  const t = String(v || '').trim();
  if (!t.length) return NaN;
  if (/^0x[0-9a-f]+$/i.test(t)) return parseInt(t, 16);
  return parseInt(t, 10);
}
function toHexByte(n) { return '0x' + Number(n).toString(16).toUpperCase().padStart(2, '0'); }

function refreshAddressHints() {
  const local = parseAddress(document.getElementById('local_address').value);
  const remote = parseAddress(document.getElementById('remote_address').value);
  document.getElementById('local_address_hex').innerText = Number.isInteger(local) ? `hex ${toHexByte(local)}` : 'enter dec or hex (e.g. 10 or 0x0A)';
  document.getElementById('remote_address_hex').innerText = Number.isInteger(remote) ? `hex ${toHexByte(remote)}` : 'enter dec or hex (e.g. 10 or 0x0A)';
}
function refreshRoleLabels() {
  const modeEl = document.getElementById('mode_select');
  const mode = String((modeEl && modeEl.value) || 'paired');
  const txTextEl = document.getElementById('role_tx_text');
  const rxTextEl = document.getElementById('role_rx_text');
  const roleNameEl = document.getElementById('role_name');
  if (mode === 'mesh') {
    if (txTextEl) txTextEl.innerText = 'Coordinator';
    if (rxTextEl) rxTextEl.innerText = 'Node';
  } else if (mode === 'standalone') {
    if (txTextEl) txTextEl.innerText = 'Host';
    if (rxTextEl) rxTextEl.innerText = 'Disabled';
  } else {
    if (txTextEl) txTextEl.innerText = 'Transmitter';
    if (rxTextEl) rxTextEl.innerText = 'Receiver';
  }
  const tx = document.getElementById('role_tx').value === 'true';
  const localLabel = document.getElementById('local_address_label');
  const remoteLabel = document.getElementById('remote_address_label');
  if (localLabel && remoteLabel) {
    if (mode === 'paired') {
      localLabel.innerText = tx ? 'TX local address (source)' : 'RX local address';
      remoteLabel.innerText = tx ? 'RX remote address (destination)' : 'TX remote address (source)';
    } else if (mode === 'mesh') {
      localLabel.innerText = tx ? 'Coordinator local address' : 'Node local address';
      remoteLabel.innerText = tx ? 'Node address (target)' : 'Coordinator address (parent)';
    } else {
      localLabel.innerText = 'Local address';
      remoteLabel.innerText = 'Peer address (optional)';
    }
  }
  if (roleNameEl) {
    if (mode === 'mesh') {
      roleNameEl.value = tx ? 'coordinator' : 'node';
    } else if (mode === 'standalone') {
      roleNameEl.value = 'none';
    } else {
      roleNameEl.value = tx ? 'transmitter' : 'receiver';
    }
  }
  const txInputRow = document.getElementById('tx_input_lora_control_row');
  if (txInputRow) { txInputRow.style.display = (mode === 'paired' && tx) ? '' : 'none'; }
  const txMqttRetryRow = document.getElementById('tx_mqtt_remote_retry_row');
  if (txMqttRetryRow) { txMqttRetryRow.style.display = tx ? '' : 'none'; }
  const txPollingEnabledRow = document.getElementById('tx_polling_enabled_row');
  if (txPollingEnabledRow) { txPollingEnabledRow.style.display = tx ? '' : 'none'; }
  const txPollingDefaultRow = document.getElementById('tx_polling_default_row');
  if (txPollingDefaultRow) { txPollingDefaultRow.style.display = tx ? '' : 'none'; }
  const rxPushOnChangeRow = document.getElementById('rx_push_on_change_row');
  if (rxPushOnChangeRow) { rxPushOnChangeRow.style.display = tx ? 'none' : ''; }
  const rxPushIntervalRow = document.getElementById('rx_push_interval_row');
  if (rxPushIntervalRow) { rxPushIntervalRow.style.display = tx ? 'none' : ''; }
  const rxFailsafeModeRow = document.getElementById('rx_failsafe_mode_row');
  if (rxFailsafeModeRow) { rxFailsafeModeRow.style.display = tx ? 'none' : ''; }
  const rxFailsafeTimeoutRow = document.getElementById('rx_failsafe_timeout_row');
  if (rxFailsafeTimeoutRow) { rxFailsafeTimeoutRow.style.display = tx ? 'none' : ''; }
  const heartbeatRow = document.getElementById('heartbeat_row');
  const heartbeatHint = document.getElementById('heartbeat_guardrail_hint');
  const heartbeatInput = document.getElementById('heartbeat_s');
  const heartbeatVisible = (mode === 'paired');
  if (heartbeatRow) { heartbeatRow.style.display = heartbeatVisible ? '' : 'none'; }
  if (heartbeatHint) { heartbeatHint.style.display = heartbeatVisible ? '' : 'none'; }
  if (heartbeatInput) {
    heartbeatInput.disabled = !heartbeatVisible;
    if (!heartbeatVisible) { heartbeatInput.value = '60'; }
  }
}
function refreshHostnamePreview() {
  if (!LRS_ENABLE_MDNS) return;
  const label = document.getElementById('lan_hostname_label');
  const hint = document.getElementById('lan_hostname_hint');
  const wrap = document.getElementById('lan_hostname_preview_wrap');
  const preview = document.getElementById('lan_hostname_preview');
  if (label) label.innerText = 'LAN hostname (mDNS)';
  if (hint) hint.innerText = 'Used as the device hostname for WiFi, OTA, and LAN mDNS.';
  if (wrap) wrap.style.display = '';
  if (!preview) return;
  const raw = (document.getElementById('lan_hostname').value || '').trim() || 'lrs';
  preview.innerHTML = `<a class="link" href="http://${raw}.local">http://${raw}.local</a>`;
}
function isDefaultDeploymentKey(v) {
  return String(v || '').trim() === 'lora-default-passphrase';
}
function randomIndex(max) {
  if (max <= 1) return 0;
  try {
    if (window.crypto && window.crypto.getRandomValues) {
      const arr = new Uint32Array(1);
      const lim = Math.floor(0x100000000 / max) * max;
      let v = 0;
      do {
        window.crypto.getRandomValues(arr);
        v = arr[0];
      } while (v >= lim);
      return v % max;
    }
  } catch (e) { }
  return Math.floor(Math.random() * max);
}
function generateReadableFleetKey() {
  const c = READABLE_KEY_CONSONANTS;
  const v = READABLE_KEY_VOWELS;
  const groups = [];
  for (let i = 0; i < 4; i++) {
    const part =
      c[randomIndex(c.length)] +
      v[randomIndex(v.length)] +
      c[randomIndex(c.length)] +
      v[randomIndex(v.length)] +
      c[randomIndex(c.length)];
    groups.push(part);
  }
  return groups.join('-');
}
function suggestReadableFleetKeyForSettings() {
  const input = document.getElementById('fleet_passphrase');
  if (!input) return;
  input.value = generateReadableFleetKey();
  updateDeploymentKeyStrength();
  showToast('Suggested readable key generated.');
}
function deploymentKeyStrength(v) {
  const s = String(v || '').trim();
  const readablePattern = /^(?:[bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz])(?:-(?:[bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz][aeiou][bdfghjkmnprstvwz])){3}$/;
  if (!s.length) { return { cls: '', text: `Enter deployment key (min ${MIN_DEPLOYMENT_KEY_LEN} chars).` }; }
  if (isDefaultDeploymentKey(s)) { return { cls: 'weak', text: 'Weak: default key is blocked.' }; }
  if (readablePattern.test(s)) { return { cls: 'strong', text: 'Strong: read-aloud key format (~80-bit).' }; }
  const hasLower = /[a-z]/.test(s);
  const hasUpper = /[A-Z]/.test(s);
  const hasDigit = /\d/.test(s);
  const hasSymbol = /[^A-Za-z0-9]/.test(s);
  let score = 0;
  if (s.length >= MIN_DEPLOYMENT_KEY_LEN) score++;
  if (s.length >= 24) score++;
  if (hasLower && hasUpper) score++;
  if (hasDigit) score++;
  if (hasSymbol) score++;
  if (s.length < MIN_DEPLOYMENT_KEY_LEN) {
    return { cls: 'weak', text: `Weak: too short (${s.length}/${MIN_DEPLOYMENT_KEY_LEN}).` };
  }
  if (score >= 4) {
    return { cls: 'strong', text: 'Strong: good length and character diversity.' };
  }
  if (score >= 2) {
    return { cls: 'ok', text: 'OK: acceptable, but longer/more diverse is better.' };
  }
  return { cls: 'weak', text: 'Weak: increase length and mix characters.' };
}
function updateDeploymentKeyStrength() {
  const input = document.getElementById('fleet_passphrase');
  const out = document.getElementById('fleet_passphrase_strength');
  if (!input || !out) return;
  const s = deploymentKeyStrength(input.value);
  out.className = `key-strength${s.cls ? ` ${s.cls}` : ''}`;
  out.innerText = s.text;
}
function refreshFreqPreset() {
  const input = document.getElementById('lora_frequency_mhz');
  const f433 = document.getElementById('freq_433');
  const f915 = document.getElementById('freq_915');
  const txt = document.getElementById('freq_selected_text');
  if (!input || !f433 || !f915) return;
  const mhz = Number(input.value);
  const near = (a, b) => Math.abs(a - b) < 0.01;
  if (near(mhz, 433.0)) { f433.checked = true; if (txt) txt.innerText = 'Selected: 433.000 MHz'; return; }
  if (near(mhz, 915.0)) { f915.checked = true; if (txt) txt.innerText = 'Selected: 915.000 MHz'; return; }
  f433.checked = true;
  input.value = '433.000';
  if (txt) txt.innerText = 'Selected: 433.000 MHz';
}
function bindFreqPreset() {
  const input = document.getElementById('lora_frequency_mhz');
  const f433 = document.getElementById('freq_433');
  const f915 = document.getElementById('freq_915');
  if (!input || !f433 || !f915) return;
  const apply = () => {
    if (f433.checked) { input.value = '433.000'; refreshFreqPreset(); return; }
    input.value = '915.000';
    refreshFreqPreset();
  };
  f433.addEventListener('change', apply);
  f915.addEventListener('change', apply);
}
function escapeHtml(v) {
  return String(v === undefined || v === null ? '' : v).replace(/[&<>"']/g, (m) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m]));
}
function copyButtonHtml(value, label = 'Value') {
  const txt = String(value === undefined || value === null ? '' : value).trim();
  const low = txt.toLowerCase();
  if (!txt || low === 'n/a' || low === 'not_set') return '';
  return `<button type="button" class="copy-btn" data-copy="${escapeHtml(txt)}" data-label="${escapeHtml(label)}" onclick="copyFromButton(this)">Copy</button>`;
}
function applyAutomationsFeatureVisibility() {
  const nav = document.getElementById('nav-automations');
  const page = document.getElementById('page-automations');
  if (nav) nav.style.display = UI_AUTOMATIONS_ENABLED ? '' : 'none';
  if (page) page.style.display = UI_AUTOMATIONS_ENABLED ? '' : 'none';
  if (!UI_AUTOMATIONS_ENABLED && activePage === 'automations') { activePage = 'status'; }
}
function copyableValueHtml(contentHtml, copyValue, label = 'Value') {
  return `<span>${contentHtml}</span>${copyButtonHtml(copyValue, label)}`;
}
async function writeClipboard(text) {
  if (navigator.clipboard && navigator.clipboard.writeText) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', 'readonly');
  ta.style.position = 'fixed';
  ta.style.opacity = '0';
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  document.execCommand('copy');
  document.body.removeChild(ta);
}
async function copyFromButton(btn) {
  if (!btn) return;
  const text = String(btn.dataset.copy || '').trim();
  if (!text) { showToast('Nothing to copy', true); return; }
  const label = String(btn.dataset.label || 'Value');
  try {
    await writeClipboard(text);
    showToast(`${label} copied`);
  } catch (e) {
    showToast(`Copy failed: ${e.message}`, true);
  }
}
function rssiToLevel(rssi) {
  if (rssi >= -67) return 4;
  if (rssi >= -75) return 3;
  if (rssi >= -85) return 2;
  if (rssi > -120) return 1;
  return 0;
}
function sigIconHtml(rssi, mode = 'scan') {
  const lv = rssiToLevel(Number(rssi));
  return `<span class="sig ${mode} lv${lv}"><i></i><i></i><i></i><i></i></span>`;
}
function setWifiBadge(level, text) {
  const icon = document.getElementById('wifiIcon');
  const wt = document.getElementById('wifiText');
  if (icon) { icon.className = `wifi-icon lv${level}`; }
  if (wt) { wt.innerText = text; }
}
function setLoraBadge(level, text) {
  const icon = document.getElementById('loraIcon');
  const lt = document.getElementById('loraText');
  if (icon) { icon.className = `sig lora lv${level}`; }
  if (lt) { lt.innerText = text; }
}
function humanAgeMs(ms) {
  const s = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  if (s < 60) return `${s}s ago`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ago`;
  const h = Math.floor(m / 60);
  return `${h}h ago`;
}
function humanAgeMsShort(ms) {
  const s = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m`;
  const h = Math.floor(m / 60);
  return `${h}h`;
}
function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
function isCompactMobile() {
  return window.matchMedia && window.matchMedia('(max-width:650px)').matches;
}
function getActiveActionContainer() {
  const page = document.querySelector('.page.active');
  if (!page) return null;
  if (page.id === 'page-settings') {
    const pane = page.querySelector('.settings-pane.active');
    if (pane) {
      const own = Array.from(pane.children).find((el) => el.classList && el.classList.contains('actions') && el.classList.contains('action-commit'));
      if (own) return own;
      const fallback = Array.from(pane.children).find((el) => el.classList && el.classList.contains('actions'));
      if (fallback) return fallback;
    }
  }
  const pageCommit = Array.from(page.children || []).find((el) => el.classList && el.classList.contains('actions') && el.classList.contains('action-commit'));
  if (pageCommit) return pageCommit;
  const own = Array.from(page.children || []).find((el) => el.classList && el.classList.contains('actions'));
  if (own) return own;
  const nestedCommit = page.querySelector('.actions.action-commit');
  if (nestedCommit) return nestedCommit;
  const nested = page.querySelector('.actions');
  if (nested) return nested;
  return null;
}
function setMobileActionBarBindings(primaryBtn, secondaryBtn) {
  if (mobileActionPrimaryEl) {
    mobileActionPrimaryEl.onclick = primaryBtn ? () => primaryBtn.click() : null;
    mobileActionPrimaryEl.textContent = primaryBtn ? String(primaryBtn.textContent || 'Save').trim() : 'Save';
  }
  if (mobileActionSecondaryEl) {
    if (secondaryBtn) {
      mobileActionSecondaryEl.style.display = '';
      mobileActionSecondaryEl.textContent = String(secondaryBtn.textContent || 'Test').trim();
      mobileActionSecondaryEl.onclick = () => secondaryBtn.click();
    } else {
      mobileActionSecondaryEl.style.display = 'none';
      mobileActionSecondaryEl.onclick = null;
    }
  }
}
function updateMobileActionBar() {
  if (!mobileActionBarEl) return;
  if (mobileActionSourceEl) {
    mobileActionSourceEl.classList.remove('mobile-action-source');
    mobileActionSourceEl = null;
  }
  if (!isCompactMobile()) {
    mobileActionBarEl.classList.remove('show');
    setMobileActionBarBindings(null, null);
    return;
  }
  const actions = getActiveActionContainer();
  if (!actions) {
    mobileActionBarEl.classList.remove('show');
    setMobileActionBarBindings(null, null);
    return;
  }
  const buttons = Array.from(actions.querySelectorAll('button'));
  const primary = buttons.find((b) => /save/i.test(String(b.textContent || '')));
  if (!primary) {
    mobileActionBarEl.classList.remove('show');
    setMobileActionBarBindings(null, null);
    return;
  }
  const secondary = buttons.find((b) => /test/i.test(String(b.textContent || '')));
  mobileActionSourceEl = actions;
  mobileActionSourceEl.classList.add('mobile-action-source');
  setMobileActionBarBindings(primary, secondary || null);
  mobileActionBarEl.classList.add('show');
}
function fleetDeviceStaleThresholdMs(r) {
  const staleAfterMs = Math.max(0, Number(r.stale_after_ms || 0));
  if (staleAfterMs > 0) return staleAfterMs;
  const intervalMs = Math.max(0, Number(r.expected_interval_ms || r.poll_interval_ms || 0));
  const base = intervalMs > 0 ? intervalMs * 3 : 300000;
  return Math.max(180000, base);
}
function fleetDeviceIsStale(r) {
  const seen = Number(r.last_seen_ms || 0);
  if (seen <= 0) return true;
  return Number(r.last_seen_age_ms || 0) > fleetDeviceStaleThresholdMs(r);
}
function ackChipClass(state) {
  const s = String(state || 'unknown').toLowerCase();
  if (s === 'ok') return 'ok';
  if (s === 'pending') return 'warn';
  if (s === 'timeout') return 'err';
  return 'neutral';
}
function freshnessChip(r) {
  if (fleetDeviceIsStale(r)) return `<span class="chip err">stale</span>`;
  if (Number(r.last_seen_ms || 0) === 0) return `<span class="chip neutral">unknown</span>`;
  return `<span class="chip ok">fresh</span>`;
}
function fleetDeviceAddrHex(r) {
  if (r && r.addr_hex) return String(r.addr_hex);
  const n = Number(r.address || 0);
  if (!Number.isFinite(n) || n <= 0) return '0x00';
  return toHexByte(n);
}
function relayChip(v) {
  const on = Number(v || 0) === 1;
  return `<span class="chip ${on ? 'ok' : 'neutral'}">${on ? 'relay on' : 'relay off'}</span>`;
}
function inputChip(v) {
  const closed = Number(v || 0) === 1;
  return `<span class="chip ${closed ? 'ok' : 'neutral'}">${closed ? 'input closed' : 'input open'}</span>`;
}
function fleetDeviceTempText(r) {
  if (!r || !r.temp_valid) return 'n/a';
  return `${Number(r.temp_c || 0).toFixed(1)} C`;
}
function fleetDeviceWebUiUrl(r) {
  const url = String((r && r.web_ui_url) || '').trim();
  if (!url.length) return '';
  if (!/^https?:\/\//i.test(url)) return '';
  return url;
}
function reasonLabel(v) {
  const r = String(v || '').toLowerCase();
  if (r === 'ack_timeout') return 'ack timeout';
  if (r === 'no_lora_link') return 'no LoRa link';
  if (r === 'input_open') return 'input open';
  if (r === 'boot') return 'boot';
  if (r === 'wait_ack') return 'waiting ack';
  if (r === 'ok') return 'normal';
  if (r === 'automation_on') return 'automation on';
  if (r === 'automation_off') return 'automation off';
  if (r === 'mqtt_on') return 'mqtt command on';
  if (r === 'mqtt_off') return 'mqtt command off';
  if (r === 'lora_on') return 'LoRa command on';
  if (r === 'lora_off') return 'LoRa command off';
  return (r || 'unknown').replace(/_/g, ' ');
}
function inputValue(id) {
  const el = document.getElementById(id);
  return el ? String(el.value || '') : '';
}
function normalizedInputValue(id) {
  return inputValue(id).trim();
}
function currentStaMatchesConnected() {
  if (!staIsConnected) return false;
  const ssid = normalizedInputValue('wifi_sta_ssid');
  if (!ssid.length) return false;
  return ssid === connectedStaSsid;
}
function updateStaTestButtonState() {
  const btn = document.getElementById('btnTestSta');
  if (!btn) return;
  const hasSsid = normalizedInputValue('wifi_sta_ssid').length > 0;
  const unchanged = currentStaMatchesConnected();
  const disabled = staTestInFlight || !hasSsid;
  btn.disabled = disabled;
  if (unchanged) {
    btn.title = 'Already connected with these STA credentials. Test will re-check current connection.';
  } else if (!hasSsid) {
    btn.title = 'Enter an SSID to test.';
  } else {
    btn.title = '';
  }
}
function stripPort(host) {
  return String(host || '').trim().toLowerCase().replace(/:\d+$/, '');
}
function normalizeLanHost(raw) {
  let h = String(raw || '').trim().toLowerCase();
  if (!h.length) return '';
  h = h.replace(/^https?:\/\//, '');
  h = h.replace(/\/.*$/, '');
  if (h.endsWith('.local')) return h;
  return `${h}.local`;
}
function startLanHostnameRedirect(hostname) {
  if (!LRS_ENABLE_MDNS) return;
  const targetHost = normalizeLanHost(hostname);
  if (!targetHost) return;
  const targetUrl = `http://${targetHost}/`;
  const el = document.getElementById('netTestResult');
  const total = 8;
  let left = total;
  if (window.__lanRedirectTimer) { clearInterval(window.__lanRedirectTimer); }
  const paint = () => {
    if (!el) return;
    el.className = 'result-line show ok';
    el.innerText = `Network saved. Switching to ${targetUrl} in ${left}s...`;
  };
  paint();
  window.__lanRedirectTimer = setInterval(() => {
    left--;
    if (left <= 0) {
      clearInterval(window.__lanRedirectTimer);
      window.__lanRedirectTimer = null;
      location.href = targetUrl;
      return;
    }
    paint();
  }, 1000);
}
function isLikelyStaSessionPath() {
  const host = stripPort(location.hostname);
  const staIp = stripPort(currentStaIp);
  const lanMdns = stripPort(currentLanMdns);
  if (host.length === 0) return false;
  if (staIp && host === staIp) return true;
  if (LRS_ENABLE_MDNS && lanMdns && host === lanMdns) return true;
  return false;
}
function toggleDrawer(force) {
  const open = (typeof force === 'boolean') ? force : !document.body.classList.contains('nav-open');
  document.body.classList.toggle('nav-open', open);
  const btn = document.getElementById('menuBtn');
  if (btn) {
    btn.innerText = open ? '✕' : '☰';
    btn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }
}
function showSettingsTab(tab) {
  const target = ['lora', 'network', 'mqtt', 'system'].includes(tab) ? tab : 'lora';
  activeSettingsTab = target;
  ['lora', 'network', 'mqtt', 'system'].forEach(p => {
    const pane = document.getElementById(`settings-pane-${p}`);
    const btn = document.getElementById(`settings-tab-${p}`);
    if (pane) pane.classList.toggle('active', p === target);
    if (btn) btn.classList.toggle('active', p === target);
  });
  if (target === 'system') { showSystemTab(activeSystemTab); }
  updateMobileActionBar();
}
function showSystemTab(tab) {
  const target = ['security', 'configuration', 'maintenance'].includes(tab) ? tab : 'security';
  activeSystemTab = target;
  ['security', 'configuration', 'maintenance'].forEach(p => {
    const pane = document.getElementById(`system-pane-${p}`);
    const btn = document.getElementById(`system-tab-${p}`);
    if (pane) pane.classList.toggle('active', p === target);
    if (btn) btn.classList.toggle('active', p === target);
  });
  updateMobileActionBar();
}
function showFleetTab(tab) {
  const target = (tab === 'manage') ? 'manage' : 'devices';
  activeFleetTab = target;
  ['devices', 'manage'].forEach(p => {
    const pane = document.getElementById(`fleet-pane-${p}`);
    const btn = document.getElementById(`fleet-tab-${p}`);
    if (pane) pane.classList.toggle('active', p === target);
    if (btn) btn.classList.toggle('active', p === target);
  });
  if (activePage === 'fleet' && target === 'devices') {
    if (fleetRefreshDebounceTimer) { clearTimeout(fleetRefreshDebounceTimer); fleetRefreshDebounceTimer = 0; }
    fleetRefreshDebounceTimer = setTimeout(() => { fleetRefreshDebounceTimer = 0; refreshFleet(); }, 250);
  }
  if (activePage === 'fleet' && target === 'manage') { showFleetManageTab(activeFleetManageTab); }
  syncPagePolling();
}
async function resolveFleetLandingTab() {
  if (activePage !== 'fleet' || !lastRoleIsTx) return;
  const token = ++fleetLandingDecisionToken;
  if (Array.isArray(fleetDevicesCache) && fleetDevicesCache.length > 0) {
    showFleetTab('devices');
    return;
  }
  showFleetTab('manage');
  const out = await apiJson('/api/fleet', { silent: true, timeoutMs: 2500 });
  if (activePage !== 'fleet' || !lastRoleIsTx || token !== fleetLandingDecisionToken) return;
  const role = String((out && out.role) || '').toLowerCase();
  if (role !== 'tx') return;
  updateFleetScanUi((out && out.scan) || {});
  const devices = Array.isArray(out && out.devices) ? out.devices : [];
  fleetDevicesCache = devices;
  showFleetTab(devices.length > 0 ? 'devices' : 'manage');
}
function showFleetManageTab(tab) {
  const target = (tab === 'lora') ? 'lora' : 'wifi';
  activeFleetManageTab = target;
  ['wifi', 'lora'].forEach(p => {
    const pane = document.getElementById(`fleet-manage-pane-${p}`);
    const btn = document.getElementById(`fleet-manage-tab-${p}`);
    if (pane) pane.classList.toggle('active', p === target);
    if (btn) btn.classList.toggle('active', p === target);
  });
  if (!(activePage === 'fleet' && activeFleetTab === 'manage')) return;
  if (target !== 'lora') {
    stopProvisioningPolling();
  } else {
    startProvisioningPolling();
  }
}
function defaultAutomationPredicate() {
  return { peer: 'self', field: 'input', op: '==', value: 0 };
}
function defaultAutomationAction() {
  return { type: 'set_relay', peer: 'self', value: 1 };
}
function defaultAutomationRule(idx) {
  return {
    id: `rule_${idx + 1}`,
    name: `Rule ${idx + 1}`,
    enabled: true,
    for_ms: 0,
    cooldown_ms: 60000,
    when: { all: [defaultAutomationPredicate()] },
    then: [defaultAutomationAction()]
  };
}
function defaultAutomationsDoc() {
  return { schema_version: 1, enabled: false, execution_mode: 'standalone', peer_display: 'addresses', action_target: 'self', rules: [] };
}
function automationNormalizeFieldDefaults(pred) {
  if (!pred) return;
  if (pred.field === 'temp_c') {
    pred.op = '>';
    pred.value = Number(pred.value);
    if (!Number.isFinite(pred.value)) pred.value = 0;
    return;
  }
  if (pred.field === 'reachable') {
    pred.op = '==';
    pred.value = (pred.value === true || pred.value === 1 || String(pred.value).toLowerCase() === 'true');
    return;
  }
  pred.op = '==';
  const n = Number(pred.value);
  pred.value = (n === 1) ? 1 : 0;
}
function normalizeAutomationsDoc(doc) {
  const out = (doc && typeof doc === 'object') ? doc : {};
  if (typeof out.schema_version !== 'number') out.schema_version = 1;
  if (typeof out.enabled !== 'boolean') out.enabled = false;
  if (out.execution_mode !== 'paired+rules') out.execution_mode = 'standalone';
  if (out.peer_display !== 'names') out.peer_display = 'addresses';
  if (typeof out.action_target !== 'string' && typeof out.action_target !== 'number') out.action_target = 'self';
  if (!Array.isArray(out.rules)) out.rules = [];
  out.rules = out.rules.slice(0, 8).map((r, i) => {
    const rule = (r && typeof r === 'object') ? r : {};
    if (!rule.id) rule.id = `rule_${i + 1}`;
    if (!rule.name) rule.name = `Rule ${i + 1}`;
    rule.enabled = (rule.enabled !== false);
    rule.for_ms = Math.max(0, Number(rule.for_ms || 0) | 0);
    rule.cooldown_ms = Math.max(0, Number(rule.cooldown_ms || 0) | 0);
    if (!rule.when || !Array.isArray(rule.when.all)) rule.when = { all: [defaultAutomationPredicate()] };
    rule.when.all = rule.when.all.slice(0, 4).map((p) => {
      const pred = (p && typeof p === 'object') ? p : defaultAutomationPredicate();
      pred.peer = (pred.peer === undefined || pred.peer === null || pred.peer === '') ? 'self' : pred.peer;
      pred.field = String(pred.field || 'input');
      if (!['temp_c', 'reachable', 'input', 'relay'].includes(pred.field)) pred.field = 'input';
      pred.op = String(pred.op || '==');
      if (pred.for_ms !== undefined && pred.for_ms !== null) pred.for_ms = Math.max(0, Number(pred.for_ms) || 0);
      automationNormalizeFieldDefaults(pred);
      return pred;
    });
    if (!rule.when.all.length) rule.when.all = [defaultAutomationPredicate()];
    let actions = Array.isArray(rule.then) ? rule.then : (rule.then ? [rule.then] : [defaultAutomationAction()]);
    actions = actions.slice(0, 4).map((a) => {
      const act = (a && typeof a === 'object') ? a : defaultAutomationAction();
      act.type = 'set_relay';
      act.peer = (act.peer === undefined || act.peer === null || act.peer === '') ? 'self' : act.peer;
      const v = Number(act.value);
      act.value = (v === 1) ? 1 : 0;
      return act;
    });
    if (!actions.length) actions = [defaultAutomationAction()];
    rule.then = actions;
    return rule;
  });
  return out;
}
function automationShowResult(msg, kind) {
  const el = document.getElementById('autoResult');
  if (!el) return;
  if (!msg) { el.className = 'result-line'; el.innerText = ''; return; }
  el.className = `result-line show${kind === 'ok' ? ' ok' : ''}${kind === 'err' ? ' err' : ''}`;
  el.innerText = msg;
}
function automationTopChanged() {
  if (!automationsDoc) automationsDoc = defaultAutomationsDoc();
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
function automationFormatValueForInput(pred) {
  if (!pred) return '';
  if (pred.field === 'reachable') return pred.value ? 'true' : 'false';
  return String(pred.value === undefined || pred.value === null ? '' : pred.value);
}
function automationFieldHint(field) {
  if (field === 'temp_c') return 'numeric (e.g. 35)';
  if (field === 'reachable') return 'true / false';
  return '0=open, 1=closed';
}
function automationIsToken(s) {
  const t = String(s || '').trim();
  return /^[A-Za-z0-9_-]{1,32}$/.test(t);
}
function automationIsAddressToken(v, allowSelf) {
  const t = String(v === undefined || v === null ? '' : v).trim();
  if (!t.length) return false;
  if (allowSelf && t === 'self') return true;
  const n = parseAddress(t);
  return Number.isInteger(n) && n >= 1 && n <= 254;
}
function automationFriendlyApiError(out) {
  if (!out) return 'Request failed (no response).';
  const code = String(out.error || 'request_failed');
  const detail = (out.detail !== undefined && out.detail !== null) ? String(out.detail) : '';
  const msgMap = {
    disabled: 'Automations are disabled in this firmware build.',
    payload_too_large: 'Automation rules JSON is too large (max 6 KB in v1).',
    empty_body: 'Automation rules payload was empty.',
    invalid_json: 'Automation JSON is invalid.',
    bad_root: 'Top-level JSON must be an object.',
    bad_schema: 'Unsupported automation schema schema_version.',
    bad_mode: 'Execution mode must be standalone or paired+rules.',
    bad_peer_display: 'Peer display must be addresses or names.',
    bad_action_target: 'Action target must be self or an address (1..254).',
    bad_rules: 'Missing or invalid rules array.',
    too_many_rules: 'Too many rules (max 8).',
    bad_rule: 'A rule has an invalid field, condition, or action.',
    open_failed: 'Could not open automation rules file.',
    open_tmp: 'Could not create temporary rules file.',
    write_failed: 'Could not write rules file.',
    rename_failed: 'Could not finalize rules file save.'
  };
  let msg = msgMap[code] || `Save failed (${code}).`;
  if (detail) msg += ` (${detail})`;
  return msg;
}
function automationValidateDoc(doc) {
  const errs = [];
  if (!doc || typeof doc !== 'object') { errs.push('Document must be a JSON object.'); return errs; }
  if (!Array.isArray(doc.rules)) { errs.push('Rules must be an array.'); return errs; }
  if (doc.rules.length > 8) errs.push('Max rules is 8 in v1.');
  if (doc.execution_mode && !['standalone', 'paired+rules'].includes(String(doc.execution_mode))) errs.push('Execution mode must be standalone or paired+rules.');
  if (doc.peer_display && !['addresses', 'names'].includes(String(doc.peer_display))) errs.push('Peer display must be addresses or names.');
  if (doc.action_target !== undefined && doc.action_target !== null && !automationIsAddressToken(doc.action_target, true)) {
    errs.push('Action target must be self or address 1..254.');
  }
  doc.rules.slice(0, 8).forEach((r, ri) => {
    if (!r || typeof r !== 'object') { errs.push(`Rule ${ri + 1}: must be an object.`); return; }
    if (!automationIsToken(r.id || '')) errs.push(`Rule ${ri + 1}: id must use only letters, numbers, _ or - (max 32).`);
    const all = r.when && Array.isArray(r.when.all) ? r.when.all : null;
    if (!all) { errs.push(`Rule ${ri + 1}: WHEN all[] is required.`); return; }
    if (all.length < 1 || all.length > 4) errs.push(`Rule ${ri + 1}: conditions must be 1..4.`);
    all.slice(0, 4).forEach((p, pi) => {
      if (!p || typeof p !== 'object') { errs.push(`Rule ${ri + 1} condition ${pi + 1}: invalid.`); return; }
      if (!automationIsAddressToken(p.peer, true)) errs.push(`Rule ${ri + 1} condition ${pi + 1}: peer must be self or address 1..254.`);
      if (!['temp_c', 'reachable', 'input', 'relay'].includes(String(p.field || ''))) errs.push(`Rule ${ri + 1} condition ${pi + 1}: unsupported field.`);
      if (p.value === undefined || p.value === null) errs.push(`Rule ${ri + 1} condition ${pi + 1}: value is required.`);
    });
    const acts = Array.isArray(r.then) ? r.then : (r.then ? [r.then] : []);
    if (acts.length < 1 || acts.length > 4) errs.push(`Rule ${ri + 1}: actions must be 1..4.`);
    acts.slice(0, 4).forEach((a, ai) => {
      if (!a || typeof a !== 'object') { errs.push(`Rule ${ri + 1} action ${ai + 1}: invalid.`); return; }
      if (String(a.type || '') !== 'set_relay') errs.push(`Rule ${ri + 1} action ${ai + 1}: action must be set_relay.`);
      if (!automationIsAddressToken(a.peer, true)) errs.push(`Rule ${ri + 1} action ${ai + 1}: target peer must be self or address 1..254.`);
      if (!(Number(a.value) === 0 || Number(a.value) === 1)) errs.push(`Rule ${ri + 1} action ${ai + 1}: relay state must be 0 or 1.`);
    });
  });
  return errs;
}
function renderAutomationsRules() {
  const host = document.getElementById('automationsRulesHost');
  if (!host) return;
  if (!automationsDoc) { host.innerHTML = 'Automations not loaded.'; return; }
  const rules = Array.isArray(automationsDoc.rules) ? automationsDoc.rules : [];
  if (!rules.length) {
    host.innerHTML = '<div class="small">No rules yet. Click <b>Add Rule</b> to create one.</div>';
    refreshAutomationsJsonPreview();
    return;
  }
  let html = '';
  rules.forEach((r, ri) => {
    html += `<div style="border:1px solid var(--border);border-radius:10px;padding:10px;margin-top:8px;background:rgba(255,255,255,.02)">`;
    html += `<div class="grid">`;
    html += `<div><label>Rule name</label><input value="${escapeHtml(r.name || '')}" oninput="automationSetRuleField(${ri},'name',this.value)"></div>`;
    html += `<div><label>Rule id</label><input value="${escapeHtml(r.id || '')}" oninput="automationSetRuleField(${ri},'id',this.value)"></div>`;
    html += `<div><div class="check-row"><input type="checkbox" ${r.enabled !== false ? 'checked' : ''} onchange="automationSetRuleEnabled(${ri},this.checked)"><label>Enabled</label></div></div><div></div>`;
    html += `<div><label>Start after condition is true for (seconds)</label><input type="number" min="0" value="${Math.floor(Number(r.for_ms || 0) / 1000)}" oninput="automationSetRuleMs(${ri},'for_ms',this.value)"></div>`;
    html += `<div><label>Do not trigger again for (seconds)</label><input type="number" min="0" value="${Math.floor(Number(r.cooldown_ms || 0) / 1000)}" oninput="automationSetRuleMs(${ri},'cooldown_ms',this.value)"><div class="small">Recommended default: 60s</div></div>`;
    html += `</div>`;
    html += `<div class="small" style="margin-top:6px">WHEN (ALL conditions) - first matching rule wins and processing stops.</div>`;
    (((r.when && r.when.all) || [])).forEach((p, pi) => {
      html += `<div style="border:1px solid var(--border);border-radius:8px;padding:8px;margin-top:6px">`;
      html += `<div class="grid">`;
      html += `<div><label>Peer</label><input value="${escapeHtml(String(p.peer === undefined || p.peer === null ? 'self' : p.peer))}" oninput="automationSetPredicateField(${ri},${pi},'peer',this.value)"></div>`;
      html += `<div><label>Field</label><select onchange="automationSetPredicateField(${ri},${pi},'field',this.value)">`;
      ['temp_c', 'reachable', 'input', 'relay'].forEach(f => { html += `<option value="${f}" ${p.field === f ? 'selected' : ''}>${f}</option>`; });
      html += `</select></div>`;
      html += `<div><label>Op</label><input value="${escapeHtml(String(p.op || '=='))}" oninput="automationSetPredicateField(${ri},${pi},'op',this.value)"></div>`;
      html += `<div><label>Value</label><input value="${escapeHtml(automationFormatValueForInput(p))}" oninput="automationSetPredicateField(${ri},${pi},'value',this.value)"><div class="small">${automationFieldHint(p.field)}</div></div>`;
      html += `</div>`;
      html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationRemovePredicate(${ri},${pi})">Remove Condition</button></div>`;
      html += `</div>`;
    });
    html += `<div class="actions" style="margin-top:6px"><button type="button" onclick="automationAddPredicate(${ri})">Add Condition (AND)</button></div>`;
    html += `<div class="small" style="margin-top:6px">THEN</div>`;
    (r.then || []).forEach((a, ai) => {
      html += `<div style="border:1px solid var(--border);border-radius:8px;padding:8px;margin-top:6px">`;
      html += `<div class="grid">`;
      html += `<div><label>Action</label><select disabled><option selected>set_relay</option></select></div>`;
      html += `<div><label>Target peer</label><input value="${escapeHtml(String(a.peer === undefined || a.peer === null ? 'self' : a.peer))}" oninput="automationSetActionField(${ri},${ai},'peer',this.value)"></div>`;
      html += `<div><label>Relay state</label><select onchange="automationSetActionField(${ri},${ai},'value',this.value)"><option value="0" ${Number(a.value) === 0 ? 'selected' : ''}>Open relay (0)</option><option value="1" ${Number(a.value) === 1 ? 'selected' : ''}>Close relay (1)</option></select></div>`;
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
function automationSetRuleField(ri, key, val) {
  if (!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
  automationsDoc.rules[ri][key] = String(val || '');
  refreshAutomationsJsonPreview();
}
function automationSetRuleEnabled(ri, val) {
  if (!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
  automationsDoc.rules[ri].enabled = !!val;
  refreshAutomationsJsonPreview();
}
function automationSetRuleMs(ri, key, valSec) {
  if (!automationsDoc || !automationsDoc.rules || !automationsDoc.rules[ri]) return;
  const sec = Math.max(0, Number(valSec) || 0);
  automationsDoc.rules[ri][key] = Math.round(sec * 1000);
  refreshAutomationsJsonPreview();
}
function automationSetPredicateField(ri, pi, key, val) {
  const pred = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when &&
    automationsDoc.rules[ri].when.all) ? automationsDoc.rules[ri].when.all[pi] : null;
  if (!pred) return;
  if (key === 'field') {
    pred.field = String(val || 'input');
    automationNormalizeFieldDefaults(pred);
    renderAutomationsRules();
    return;
  }
  if (key === 'value') {
    if (pred.field === 'temp_c') { pred.value = Number(val); if (!Number.isFinite(pred.value)) pred.value = 0; }
    else if (pred.field === 'reachable') { pred.value = ['1', 'true', 'yes', 'on'].includes(String(val).trim().toLowerCase()); }
    else { pred.value = (Number(val) === 1) ? 1 : 0; }
  } else {
    pred[key] = String(val || '');
  }
  refreshAutomationsJsonPreview();
}
function automationSetActionField(ri, ai, key, val) {
  const act = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].then)
    ? automationsDoc.rules[ri].then[ai] : null;
  if (!act) return;
  if (key === 'value') act.value = (Number(val) === 1) ? 1 : 0;
  else act[key] = String(val || '');
  refreshAutomationsJsonPreview();
}
function automationAddPredicate(ri) {
  const arr = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when)
    ? automationsDoc.rules[ri].when.all : null;
  if (!arr || arr.length >= 4) return;
  arr.push(defaultAutomationPredicate());
  renderAutomationsRules();
}
function automationRemovePredicate(ri, pi) {
  const arr = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri] && automationsDoc.rules[ri].when)
    ? automationsDoc.rules[ri].when.all : null;
  if (!arr) return;
  arr.splice(pi, 1);
  if (!arr.length) arr.push(defaultAutomationPredicate());
  renderAutomationsRules();
}
function automationAddAction(ri) {
  const arr = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri]) ? automationsDoc.rules[ri].then : null;
  if (!arr || arr.length >= 4) return;
  arr.push(defaultAutomationAction());
  renderAutomationsRules();
}
function automationRemoveAction(ri, ai) {
  const arr = (automationsDoc && automationsDoc.rules && automationsDoc.rules[ri]) ? automationsDoc.rules[ri].then : null;
  if (!arr) return;
  arr.splice(ai, 1);
  if (!arr.length) arr.push(defaultAutomationAction());
  renderAutomationsRules();
}
function automationDeleteRule(ri) {
  if (!automationsDoc || !automationsDoc.rules) return;
  automationsDoc.rules.splice(ri, 1);
  renderAutomationsRules();
}
function addAutomationRule() {
  if (!automationsDoc) automationsDoc = defaultAutomationsDoc();
  if (!Array.isArray(automationsDoc.rules)) automationsDoc.rules = [];
  if (automationsDoc.rules.length >= 8) {
    automationShowResult('Max rules is 8 in v1.', 'err');
    return;
  }
  automationsDoc.rules.push(defaultAutomationRule(automationsDoc.rules.length));
  renderAutomationsRules();
}
function refreshAutomationsJsonPreview() {
  if (!automationsDoc) return;
  const out = document.getElementById('auto_json_preview');
  if (!out) return;
  out.value = JSON.stringify(automationsDoc, null, 2);
}
function applyAutomationsDocToForm() {
  if (!automationsDoc) automationsDoc = defaultAutomationsDoc();
  const d = automationsDoc;
  const enabledEl = document.getElementById('auto_enabled');
  const modeEl = document.getElementById('auto_execution_mode');
  const peerDisplayEl = document.getElementById('auto_peer_display');
  const targetEl = document.getElementById('auto_action_target');
  if (enabledEl) enabledEl.checked = !!d.enabled;
  if (modeEl) modeEl.value = d.execution_mode === 'paired+rules' ? 'paired+rules' : 'standalone';
  if (peerDisplayEl) peerDisplayEl.value = d.peer_display === 'names' ? 'names' : 'addresses';
  if (targetEl) targetEl.value = String(d.action_target === undefined || d.action_target === null ? 'self' : d.action_target);
  renderAutomationsRules();
}
async function loadAutomationsPageData(force) {
  if (!UI_AUTOMATIONS_ENABLED) return;
  if (automationsPageLoadInFlight) return;
  if (automationsPageLoaded && !force) return;
  automationsPageLoadInFlight = true;
  automationShowResult(force ? 'Reloading automations...' : 'Loading automations...', '');
  try {
    const out = await apiJson('/api/automation-rules', { silent: true, timeoutMs: 7000 });
    if (!out) { automationShowResult('Automation rules unavailable.', 'err'); return; }
    if (out && out.ok === false) { automationShowResult(automationFriendlyApiError(out), 'err'); return; }
    automationsDoc = normalizeAutomationsDoc(out);
    applyAutomationsDocToForm();
    automationsPageLoaded = true;
    automationShowResult('Automations loaded.', 'ok');
  } catch (e) {
    automationShowResult(`Load failed: ${e.message || e}`, 'err');
  } finally {
    automationsPageLoadInFlight = false;
  }
}
function reloadAutomations() { automationsPageLoaded = false; return loadAutomationsPageData(true); }
async function saveAutomations() {
  if (!UI_AUTOMATIONS_ENABLED) { automationShowResult('Automations feature disabled in this firmware build.', 'err'); return; }
  if (!automationsDoc) automationsDoc = defaultAutomationsDoc();
  automationTopChanged();
  const validationErrors = automationValidateDoc(automationsDoc);
  if (validationErrors.length) {
    automationShowResult(validationErrors[0], 'err');
    return;
  }
  automationShowResult('Saving automations...', '');
  try {
    const body = JSON.stringify(automationsDoc);
    const out = await apiJson('/api/automation-rules', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body, timeoutMs: 8000, silent: true });
    if (out && out.ok) {
      automationsPageLoaded = true;
      automationShowResult(`Automation rules saved${out.saved_bytes ? ` (${out.saved_bytes} bytes)` : ''}.`, 'ok');
      return;
    }
    automationShowResult(automationFriendlyApiError(out), 'err');
  } catch (e) {
    automationShowResult(`Save failed: ${e.message || e}`, 'err');
  }
}
function applyAutomationsJsonFromPreview() {
  if (!UI_AUTOMATIONS_ENABLED) return;
  const ta = document.getElementById('auto_json_preview');
  if (!ta) return;
  try {
    const parsed = JSON.parse(ta.value || '{}');
    automationsDoc = normalizeAutomationsDoc(parsed);
    applyAutomationsDocToForm();
    automationShowResult('JSON applied to form.', 'ok');
  } catch (e) {
    automationShowResult(`Invalid JSON: ${e.message || e}`, 'err');
  }
}
async function copyAutomationJsonPreview() {
  if (!UI_AUTOMATIONS_ENABLED) return;
  const ta = document.getElementById('auto_json_preview');
  if (!ta) return;
  try {
    await writeClipboard(String(ta.value || ''));
    showToast('Automation JSON copied');
  } catch (e) {
    showToast(`Copy failed: ${e.message}`, true);
  }
}
function showPage(page) {
  const allowed = UI_AUTOMATIONS_ENABLED
    ? ['status', 'fleet', 'automations', 'sensors', 'settings']
    : ['status', 'fleet', 'sensors', 'settings'];
  activePage = allowed.includes(page) ? page : 'status';
  allowed.forEach(p => {
    const sec = document.getElementById(`page-${p}`);
    const nav = document.getElementById(`nav-${p}`);
    if (sec) sec.classList.toggle('active', p === activePage);
    if (nav) nav.classList.toggle('active', p === activePage);
  });
  if (activePage === 'settings') { showSettingsTab(activeSettingsTab); loadSettingsPageData(false).catch(() => { }); scanWifi().catch(() => { }); }
  if (activePage === 'automations') { loadAutomationsPageData(false).catch(() => { }); }
  if (activePage === 'status') {
    statusStaticCache = null;
    statusStaticLoadInFlight = false;
    statusLiveHasLiveData = false;
    statusDegradedLiteMode = false;
    statusLiveSseLastMessageMs = 0;
    ensureStatusStatic(true).catch(() => { });
    refreshStatusLiveNotice();
  }
  if (activePage === 'fleet') { resolveFleetLandingTab().catch(() => { showFleetTab('manage'); }); }
  applyAutomationsFeatureVisibility();
  toggleDrawer(false);
  syncPagePolling();
  updateMobileActionBar();
}
function applyFleetTabVisibility() {
  const fleetTabBtn = document.getElementById('nav-fleet');
  if (fleetTabBtn) { fleetTabBtn.style.display = lastRoleIsTx ? '' : 'none'; }
  if (!lastRoleIsTx && activePage === 'fleet') { showPage('status'); }
}
function showToast(msg, isError = false) {
  const t = document.getElementById('toast');
  if (!t) return;
  t.className = `toast show${isError ? ' err' : ''}`;
  t.innerText = msg;
  clearTimeout(window.__toastTimer);
  window.__toastTimer = setTimeout(() => { t.className = 'toast'; }, 2200);
}
function applyTheme(theme) {
  currentTheme = (theme === 'light') ? 'light' : 'dark';
  document.body.classList.toggle('light', currentTheme === 'light');
  const icons = document.querySelectorAll('.theme-toggle');
  icons.forEach((btn) => { btn.innerText = currentTheme === 'dark' ? '☀' : '🌙'; });
  try { localStorage.setItem('lrs_theme', currentTheme); } catch (e) { }
}
function toggleTheme() { applyTheme(currentTheme === 'dark' ? 'light' : 'dark'); }
function togglePasswordField(id, btn) {
  const el = document.getElementById(id);
  if (!el) return;
  const show = el.type === 'password';
  el.type = show ? 'text' : 'password';
  if (btn) {
    btn.innerText = show ? '🙈' : '👁';
    btn.title = show ? 'Hide password' : 'Show password';
    btn.setAttribute('aria-label', btn.title);
  }
}
let statusDetailsLoaded = false;
async function ensureStatusStatic(silent) {
  if (location.pathname !== '/') return statusStaticCache;
  if (statusStaticCache) return statusStaticCache;
  if (statusStaticLoadInFlight) return null;
  statusStaticLoadInFlight = true;

  // In M1, we fetch lite by default on page load.
  const lite = await apiJson('/api/status-lite', { silent: true });
  if (lite && lite.ok !== false) {
    applyStatusLiteDegraded(lite, null);
  }
  statusStaticLoadInFlight = false;
  return statusStaticCache;
}
async function loadStatusDetails() {
  if (statusDetailsLoaded) return;
  const btn = document.getElementById('btnLoadStatusDetails');
  if (btn) btn.innerText = 'Loading...';
  const st = await apiJson('/api/status-static', { silent: false });
  if (st && st.ok !== false) {
    statusStaticCache = st;
    statusDegradedLiteMode = false;
    const footerFw = document.getElementById('footerFw');
    if (footerFw) {
      footerFw.innerText = `FW: ${String(st.fw_display || st.fw_version || '-')}`;
    }
    applyStatusPageState(statusStaticCache);
    statusDetailsLoaded = true;
  }
  if (btn) btn.style.display = 'none';
  document.getElementById('statusDetailsPane').classList.add('active');
}
function toggleStatusDetails() {
  loadStatusDetails();
}

function buildStatusFallbackFromLite(lite) {
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
function applyStatusLiteDegraded(lite, noticeText) {
  if (!lite || lite.ok === false) return false;
  const fallback = buildStatusFallbackFromLite(lite);
  statusStaticCache = Object.assign({}, statusStaticCache || {}, fallback);
  statusDegradedLiteMode = true;
  applyStatusPageState(statusStaticCache);
  if (noticeText) { setStatusLiveNotice(noticeText); }
  return true;
}
function setStatusLiveNotice(text) {
  const el = document.getElementById('statusLiveState');
  if (!el) return;
  const t = String(text || '');
  let dot = '⚪';
  if (t.includes('connected')) {
    dot = '🟢';
  } else if (t.includes('disconnected')) {
    dot = '🔴';
  } else if (t.includes('paused') || t.includes('waiting') || t.includes('reconnect') || t.includes('limited')) {
    dot = '🟡';
  }
  el.innerText = dot;
  el.title = t;
  el.setAttribute('aria-label', t || 'status updates');
}
function refreshStatusLiveNotice() {
  if (location.pathname !== '/' || activePage !== 'status') {
    return;
  }
  if (document.hidden) {
    setStatusLiveNotice('Status updates paused while tab is hidden');
    return;
  }
  if ((suspendGlobalPollsUntilMs > 0 && Date.now() < suspendGlobalPollsUntilMs)) {
    setStatusLiveNotice('Status updates paused while device is busy');
    return;
  }
  if (statusDegradedLiteMode && !statusLiveSseConnected) {
    setStatusLiveNotice('Low-memory mode: limited status');
    return;
  }
  if (statusLiveSseConnected) {
    const age = statusLiveSseLastMessageMs > 0 ? (Date.now() - statusLiveSseLastMessageMs) : 0;
    if (age > 12000) {
      setStatusLiveNotice('Waiting for device updates...');
    } else {
      setStatusLiveNotice('Live updates: connected');
    }
    return;
  }
  if (statusLiveHasLiveData) {
    setStatusLiveNotice('Live updates disconnected; waiting to reconnect...');
  } else {
    setStatusLiveNotice('Waiting for device updates...');
  }
}
function stopStatusLiveUiTicker() {
  if (statusLiveUiTicker) { clearTimeout(statusLiveUiTicker); statusLiveUiTicker = 0; }
}
function startStatusLiveUiTicker() {
  stopStatusLiveUiTicker();
  if (location.pathname !== '/' || activePage !== 'status') return;
  const loop = () => {
    if (location.pathname !== '/' || activePage !== 'status') { statusLiveUiTicker = 0; return; }
    refreshStatusLiveNotice();
    statusLiveUiTicker = setTimeout(loop, 1000);
  };
  loop();
}
function closeStatusLiveSse() {
  if (statusLiveSseReconnectTimer) { clearTimeout(statusLiveSseReconnectTimer); statusLiveSseReconnectTimer = 0; }
  if (statusLiveEventSource) {
    try { statusLiveEventSource.close(); } catch (e) { }
    statusLiveEventSource = null;
  }
  statusLiveSseConnected = false;
  statusLiveSseLastMessageMs = 0;
  refreshStatusLiveNotice();
}
function shouldUseStatusLiveSse() {
  return true;
}
function scheduleStatusLiveSseReconnect() {
  if (statusLiveSseReconnectTimer || !shouldUseStatusLiveSse()) return;
  const delay = Math.max(1000, Math.min(10000, Number(statusLiveSseBackoffMs || 1000)));
  statusLiveSseReconnectTimer = setTimeout(() => {
    statusLiveSseReconnectTimer = 0;
    if (shouldUseStatusLiveSse()) { startStatusLiveSse(); }
  }, delay);
  statusLiveSseBackoffMs = Math.min(10000, delay * 2);
}
function handleStatusLivePayload(live) {
  if (!live || typeof live !== 'object') return;
  statusFailCount = 0;
  statusDegradedLiteMode = false;
  statusLiveHasLiveData = true;
  statusLiveSseLastMessageMs = Date.now();
  if (!statusStaticCache && !statusStaticLoadInFlight) {
    ensureStatusStatic(true).catch(() => { });
  }
  const st = Object.assign({}, statusStaticCache || {}, live || {});
  applyStatusPageState(st);
  refreshStatusLiveNotice();
}
function startStatusLiveSse() {
  if (!shouldUseStatusLiveSse()) return;
  if (statusLiveEventSource) return;
  const pageParam = activePage === 'status' ? 'status' : (activePage === 'fleet' ? (isProvisioningUiBusy() ? 'provisioning' : 'fleet') : 'none');
  try {
    const es = new EventSource('/api/status-live/events?page=' + pageParam);
    statusLiveEventSource = es;
    es.onopen = () => {
      statusLiveSseConnected = true;
      statusLiveSseBackoffMs = 1000;
      statusLiveSseLastMessageMs = Date.now();
      refreshStatusLiveNotice();
    };
    const onStatusEvent = (ev) => {
      try {
        const live = JSON.parse(ev.data);
        handleStatusLivePayload(live);
      } catch (e) { }
    };
    const onFleetEvent = (ev) => {
      if (!(activePage === 'fleet' && activeFleetTab === 'devices')) return;
      try {
        const fleet = JSON.parse(ev.data);
        const role = String(fleet.role || '').toLowerCase();
        updateFleetScanUi(fleet.scan || {});
        if (role !== 'tx') return;
        const devices = Array.isArray(fleet.devices) ? fleet.devices : [];
        fleetDevicesCache = devices;
        const summary = document.getElementById('fleetSummary');
        if (summary) summary.innerText = `Discovered devices: ${devices.length} | Global schedule default: ${Math.max(60, Math.round(Number(fleet.tx_default_poll_interval_ms || 60000) / 1000))}s | Global polling: ${fleet.tx_polling_enabled ? 'enabled' : 'disabled'}`;
        renderFleetTable(devices);
      } catch (e) { }
    };
    const onProvisioningEvent = (ev) => {
      if (!(activePage === 'fleet' && activeFleetTab === 'manage' && activeFleetManageTab === 'lora')) return;
      try {
        const prov = JSON.parse(ev.data);
        renderProvisioningStatus(prov);
        const sess = (prov && prov.session) || {};
        const st = String(sess.state || 'idle');
        if ((!sess.active) || st === 'complete' || st === 'error' || st === 'idle') {
          provPollGraceUntilMs = 0;
        }
      } catch (e) { }
    };
    es.addEventListener('status', onStatusEvent);
    es.addEventListener('fleet', onFleetEvent);
    es.addEventListener('provisioning', onProvisioningEvent);
    es.onmessage = onStatusEvent;
    es.onerror = () => {
      if (statusLiveEventSource !== es) return;
      try { es.close(); } catch (e) { }
      statusLiveEventSource = null;
      statusLiveSseConnected = false;
      refreshStatusLiveNotice();
      scheduleStatusLiveSseReconnect();
    };
  } catch (e) {
    statusLiveSseConnected = false;
    refreshStatusLiveNotice();
    scheduleStatusLiveSseReconnect();
  }
}
function syncStatusLiveSse(forceReconnect = false) {
  if (forceReconnect && statusLiveEventSource) {
    try { statusLiveEventSource.close(); } catch (e) { }
    statusLiveEventSource = null;
    statusLiveSseConnected = false;
  }
  if (shouldUseStatusLiveSse()) {
    startStatusLiveSse();
    startStatusLiveUiTicker();
  } else {
    closeStatusLiveSse();
    if (activePage !== 'status') { stopStatusLiveUiTicker(); }
  }
  refreshStatusLiveNotice();
}
function applyStatusPageState(st) {
  console.log("applyStatusPageState fired", st);
  if (!st) return;
  applyHeaderStatus(st);
  const relayOn = Number(st.relay_state) === 1;
  const hasLora = Number(st.lora_last_packet_ms || 0) > 0;
  const hasLoraTx = Number(st.lora_last_tx_ms || 0) > 0;
  const loraAgoMs = Number(st.uptime_ms || 0) - Number(st.lora_last_packet_ms || 0);
  const loraTxAgoMs = Number(st.uptime_ms || 0) - Number(st.lora_last_tx_ms || 0);
  const roleText = String(st.role || '').toLowerCase();
  if (roleText === 'tx' || roleText === 'rx') {
    lastRoleIsTx = roleText === 'tx';
  }
  applyFleetTabVisibility();
  const roleIsTx = lastRoleIsTx;
  const modeRaw = String(st.mode || 'paired').toLowerCase();
  const roleRaw = String(st.role_name || '').toLowerCase();
  const modeDisplay = modeRaw === 'mesh' ? 'Mesh' : (modeRaw === 'standalone' ? 'Standalone' : 'Paired');
  let roleDisplayName = roleRaw;
  if (!roleDisplayName) {
    roleDisplayName = roleIsTx ? 'transmitter' : 'receiver';
  }
  if (roleDisplayName === 'transmitter') roleDisplayName = 'Transmitter';
  else if (roleDisplayName === 'receiver') roleDisplayName = 'Receiver';
  else if (roleDisplayName === 'coordinator') roleDisplayName = 'Coordinator';
  else if (roleDisplayName === 'node') roleDisplayName = 'Node';
  else if (roleDisplayName === 'none') roleDisplayName = 'None';
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
  if (linkStateRaw === 'ok' || linkStateRaw === 'linked' || linkStateRaw === 'connected' || linkStateRaw === 'active') {
    linkEmoji = '🟢';
  } else if (linkStateRaw === 'timeout' || linkStateRaw === 'lost' || linkStateRaw === 'down' || linkStateRaw === 'error' || linkStateRaw === 'failed') {
    linkEmoji = '🔴';
  } else if (linkStateRaw === 'syncing' || linkStateRaw === 'searching' || linkStateRaw === 'degraded') {
    linkEmoji = '🟡';
  }
  const linkLine = `${linkEmoji} ${String(st.link_state || 'unknown')} • RSSI ${loraRssiText}`;
  const loraActivityLine = `tx ${loraLastTxText} • rx ${loraLastText}`;
  const relayReasonRaw = String(st.relay_reason || '').toLowerCase();
  let relaySource = '';
  if (relayReasonRaw.startsWith('mqtt_')) relaySource = 'mqtt command';
  else if (relayReasonRaw.startsWith('automation_')) relaySource = 'automation';
  else if (relayReasonRaw.startsWith('lora_')) relaySource = 'LoRa command';
  else if (relayReasonRaw.startsWith('input_')) relaySource = 'input';
  staIsConnected = !!st.sta_connected;
  currentStaIp = String(st.sta_ip || '');
  currentLanMdns = LRS_ENABLE_MDNS ? String(st.mdns_lan || (st.lan_hostname ? `${st.lan_hostname}.local` : '')) : '';
  currentApIp = String(st.ap_ip || '');
  currentApMdns = LRS_ENABLE_MDNS ? String(st.mdns_ap || 'lrs.local') : '';
  if (staIsConnected) {
    const connectedSsid = String(st.sta_ssid || '');
    if (connectedSsid.length) {
      connectedStaSsid = connectedSsid;
    }
  } else {
    connectedStaSsid = '';
  }
  updateStaTestButtonState();
  const lanHost = LRS_ENABLE_MDNS ? String(st.mdns_lan || (st.lan_hostname ? `${st.lan_hostname}.local` : '')).replace(/\/+$/, '') : '';
  const apHost = LRS_ENABLE_MDNS ? String(st.mdns_ap || 'lrs.local').replace(/\/+$/, '') : '';
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
  const rb = document.getElementById('relayBadge');
  if (rb) {
    rb.className = `relay-badge ${relayOn ? 'on' : 'off'}`;
    rb.innerText = relayOn ? 'RELAY ON' : 'RELAY OFF';
  }
  const rm = document.getElementById('relayMeta');
  if (rm) {
    const relayMeta = relaySource ? `Link: ${linkEmoji} ${st.link_state || 'unknown'} • via ${relaySource}` : `Link: ${linkEmoji} ${st.link_state || 'unknown'}`;
    rm.innerText = relayMeta;
  }
  const liteTable = document.getElementById('statusLiteTable');
  if (liteTable) {
    liteTable.className = 'status-table';
    liteTable.innerHTML =
      `<div class="k">Role</div><div class="v">${escapeHtml(roleDisplay)}</div>
     <div class="k">Link</div><div class="v">${escapeHtml(linkLine)}</div>
     <div class="k">Activity</div><div class="v">${escapeHtml(loraActivityLine)}</div>
     <div class="k">Relay reason</div><div class="v">${escapeHtml(reasonLabel(st.relay_reason))}</div>
     <div class="k">WiFi</div><div class="v"><span class="sta-line">${escapeHtml(st.sta_ssid || st.sta_target_ssid || 'sta')}<span class="sta-dot ${st.sta_connected ? 'on' : 'off'}" title="${st.sta_connected ? 'connected' : 'not connected'}"></span></span><div class="small">${escapeHtml(st.sta_connected ? `${st.sta_rssi} dBm` : 'disconnected')}</div></div>`;
  }
  const table = document.getElementById('statusTable');
  const deployKey = String(st.deployment_key || '');
  const footerFw = document.getElementById('footerFw');
  if (footerFw) {
    footerFw.innerText = `FW: ${String(st.fw_display || st.fw_version || '-')}`;
  }
  if (table && statusDetailsLoaded) {
    table.className = 'status-table';
    table.innerHTML =
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
  const tempTiles = document.querySelectorAll('#sensorTempTile');
  tempTiles.forEach((t) => {
    if (st.sensor_temp_detected && st.sensor_temp_valid) {
      t.innerText = `Temperature: ${Number(st.sensor_temp_c).toFixed(1)} C`;
    } else if (st.sensor_temp_enabled) {
      t.innerText = `Temperature: ${st.sensor_temp_error || 'n/a'}`;
    } else {
      t.innerText = 'Temperature: disabled';
    }
  });
  const remoteTempTiles = document.querySelectorAll('#sensorRemoteTempTile');
  remoteTempTiles.forEach((rt) => {
    if (st.lora_remote_temp_valid) {
      rt.innerText = `Remote LoRa temp: ${Number(st.lora_remote_temp_c).toFixed(1)} C`;
    } else {
      rt.innerText = 'Remote LoRa temp: n/a';
    }
  });
  const inputTiles = document.querySelectorAll('#sensorInputTile');
  inputTiles.forEach((inTile) => {
    const closed = Number(st.local_input_state) === 1;
    inTile.innerHTML = `Dry contact input: <span class="sensor-state ${closed ? 'closed' : 'open'}">${closed ? 'CLOSED' : 'OPEN'}</span>`;
  });
  const sdState = document.getElementById('sensorDiagState');
  const sdTemp = document.getElementById('sensorDiagTemp');
  const sdAddr = document.getElementById('sensorDiagAddr');
  const sdLast = document.getElementById('sensorDiagLast');
  if (sdState && sdTemp && sdAddr && sdLast) {
    const enabled = !!st.sensor_temp_enabled;
    const detected = !!st.sensor_temp_detected;
    const valid = !!st.sensor_temp_valid;
    if (!enabled) {
      sdState.innerText = 'DS18B20: disabled';
      sdTemp.innerText = 'Temperature: n/a';
      sdAddr.innerText = 'Address: n/a';
      sdLast.innerText = 'Last read: n/a';
    } else {
      sdState.innerText = detected ? 'DS18B20: detected' : `DS18B20: not detected (${st.sensor_temp_error || 'unknown'})`;
      sdTemp.innerText = valid ? `Temperature: ${Number(st.sensor_temp_c).toFixed(1)} C` : `Temperature: ${st.sensor_temp_error || 'n/a'}`;
      sdAddr.innerText = `Address: ${st.sensor_temp_addr || 'n/a'}`;
      const ageMs = Number(st.uptime_ms || 0) - Number(st.sensor_temp_last_read_ms || 0);
      sdLast.innerText = Number(st.sensor_temp_last_read_ms || 0) > 0 ? `Last read: ${humanAgeMs(ageMs)}` : 'Last read: n/a';
    }
  }
}
function applyHeaderStatus(st) {
  if (!st) return;
  const footerMem = document.getElementById('footerMem');
  const modeRaw = String(st.mode || 'paired').toLowerCase();
  const titleEl = document.getElementById('consoleTitle');
  const hLan = st.lan_hostname ? String(st.lan_hostname) : '';
  const hMdns = st.mdns_lan ? String(st.mdns_lan) : '';
  const hostLabel = String(hLan || hMdns || '').replace(/\.local$/i, '').trim();
  const chipIdStr = st.chip_id ? String(st.chip_id).trim() : '';
  const headerTitle = hostLabel || (chipIdStr ? `lrs-${chipIdStr}` : 'lrs-xxx');
  if (titleEl) { titleEl.innerText = headerTitle; }
  document.title = headerTitle || 'LRS Console';
  const serialStr = st.factory_serial ? String(st.factory_serial).trim() : '';
  const identity = serialStr || (chipIdStr ? `lrs-${chipIdStr}` : '-');
  const statusHeadText = document.getElementById('statusHeadDeviceText');
  const statusHeadCopy = document.getElementById('statusHeadDeviceCopy');
  if (statusHeadText) { statusHeadText.innerText = `Device: ${identity}`; }
  if (statusHeadCopy) {
    statusHeadCopy.dataset.copy = identity;
    statusHeadCopy.dataset.label = 'Device identity';
  }
  const roleText = String(st.role || '').toLowerCase();
  if (roleText === 'tx' || roleText === 'rx') {
    lastRoleIsTx = roleText === 'tx';
    applyFleetTabVisibility();
  }
  setWifiBadge(st.sta_connected ? rssiToLevel(st.sta_rssi) : 0, 'WiFi');
  const hasLora = Number(st.lora_last_packet_ms || 0) > 0;
  setLoraBadge(hasLora ? rssiToLevel(st.lora_last_rssi) : 0, 'LoRa');
  const relayOn = Number(st.relay_state) === 1;
  const rh = document.getElementById('relayHeader');
  if (rh) {
    rh.className = `relay-head ${relayOn ? 'on' : 'off'}`;
    rh.innerText = relayOn ? '🟢' : '⚪';
    rh.title = relayOn ? 'Relay on' : 'Relay off';
    rh.setAttribute('aria-label', relayOn ? 'Relay on' : 'Relay off');
  }
  if (footerMem) {
    const heapBytes = Number(st.heap_free_bytes || 0);
    const maxBlockBytes = Number(st.max_free_block_bytes || 0);
    const heapFrag = Number(st.heap_frag_percent || 0);
    const heapK = heapBytes > 0 ? (heapBytes / 1024) : 0;
    const maxK = maxBlockBytes > 0 ? (maxBlockBytes / 1024) : 0;
    const heapTxt = heapBytes > 0 ? (heapK >= 10 ? String(Math.round(heapK)) : heapK.toFixed(1)) : '-';
    const maxTxt = maxBlockBytes > 0 ? (maxK >= 10 ? String(Math.round(maxK)) : maxK.toFixed(1)) : '-';
    footerMem.innerText = (heapBytes > 0 && maxBlockBytes > 0) ? `Mem ${heapTxt}/${maxTxt}` : 'Mem -/-';
    if (heapBytes > 0 || maxBlockBytes > 0) {
      footerMem.title = `Free heap: ${heapBytes} B | Max block: ${maxBlockBytes} B | Frag: ${heapFrag}%`;
    } else {
      footerMem.title = 'Memory metrics unavailable';
    }
  }
}
async function refreshHeaderStatus() {
  if (location.pathname !== '/') return;
  if (activePage === 'status') return;
  if ((suspendGlobalPollsUntilMs > 0 && Date.now() < suspendGlobalPollsUntilMs)) return;
  if (headerStatusRefreshInFlight) return;
  headerStatusRefreshInFlight = true;
  const st = await apiJson('/api/status-lite', { silent: true });
  if (st && st.ok !== false) { applyHeaderStatus(st); }
  headerStatusRefreshInFlight = false;
}
function stopAllUiPollingForAuthExpiry() {
  suspendGlobalPollsUntilMs = Date.now() + 60000;
  try { stopProvisioningPolling(); } catch (e) { }
  try { stopPagePolling(); } catch (e) { }
  try { stopHeaderPolling(); } catch (e) { }
  try { stopStatusLiveUiTicker(); } catch (e) { }
  try { closeStatusLiveSse(); } catch (e) { }
  try { provStatusInFlight = false; } catch (e) { }
  try { headerStatusRefreshInFlight = false; } catch (e) { }
  try { fleetRefreshInFlight = false; } catch (e) { }
}
async function apiJson(url, options) {
  let t = null;
  try {
    const merged = Object.assign({ cache: 'no-store', silent: false, timeoutMs: 8000 }, options || {});
    const controller = new AbortController();
    const timeoutMs = Number(merged.timeoutMs || 8000);
    const allowHttpError = !!merged.allowHttpError;
    t = setTimeout(() => controller.abort(), timeoutMs);
    delete merged.timeoutMs;
    delete merged.allowHttpError;
    merged.signal = controller.signal;
    const res = await fetch(url, merged);
    if (res.status === 401) {
      stopAllUiPollingForAuthExpiry();
      location.href = '/login?expired=1';
      return null;
    }
    if (!res.ok) {
      if (allowHttpError) {
        const out = await res.json();
        if (out && typeof out === 'object' && out._http_status == null) { out._http_status = res.status; }
        return out;
      }
      throw new Error(`HTTP ${res.status}`);
    }
    return await res.json();
  } catch (e) {
    if (!options || !options.silent) {
      const st = document.getElementById('statusTable');
      if (st) { st.innerText = `API error: ${e.message}`; }
    }
    return null;
  } finally {
    if (t) { clearTimeout(t); }
  }
}
async function logout() {
  try { await fetch('/api/logout', { method: 'POST' }); } catch (e) { }
  location.href = '/login?logged_out=1';
}
async function load() {
  applyAutomationsFeatureVisibility();
  if (activePage === 'status') {
    await ensureStatusStatic(true);
    // Only sync SSE if details have been loaded to prevent heavy background work
    if (statusDetailsLoaded) {
      syncStatusLiveSse();
    } else {
      refreshStatusLiveNotice();
    }
  }
}

async function loadSettingsPageData(force) {
  if (settingsPageLoadInFlight) return;
  if (settingsPageLoaded && !force) return;
  settingsPageLoadInFlight = true;
  try {
    const s = await apiJson('/api/settings', { silent: true, timeoutMs: 6000 });
    if (!s) return;
    lastRoleIsTx = !!s.role_tx;
    applyFleetTabVisibility();
    Object.keys(s).forEach(k => {
      const el = document.getElementById(k);
      if (!el) return;
      if (el.type === 'checkbox') { el.checked = !!s[k]; return; }
      el.value = String(s[k]);
    });
    const fleetEl = document.getElementById('fleet_passphrase');
    if (fleetEl) {
      fleetEl.value = '';
      fleetEl.placeholder = s.fleet_passphrase_set ? 'Stored (hidden). Enter new value to change.' : '';
    }
    const staPassEl = document.getElementById('wifi_sta_password');
    if (staPassEl) {
      staPassEl.value = '';
      staPassEl.placeholder = s.wifi_sta_password_set ? 'Stored (hidden). Enter new value to change.' : '';
    }
    const mqttPassEl = document.getElementById('mqtt_password');
    if (mqttPassEl) {
      mqttPassEl.value = '';
      mqttPassEl.placeholder = s.mqtt_password_set ? 'Stored (hidden). Enter new value to change.' : '';
    }
    const adminPassEl = document.getElementById('admin_password');
    if (adminPassEl) {
      adminPassEl.value = '';
      adminPassEl.placeholder = s.admin_password_set ? 'Stored (hidden). Enter new value to change.' : '';
    }
    updateDeploymentKeyStrength();
    updateStaTestButtonState();
    const modeSelect = document.getElementById('mode_select');
    if (modeSelect) {
      modeSelect.value = String(s.mode || 'paired');
    }
    document.getElementById('role_tx').value = String(!!s.role_tx);
    document.getElementById('role_tx_true').checked = !!s.role_tx;
    document.getElementById('role_tx_false').checked = !s.role_tx;
    document.getElementById('ap_always_on').checked = !!s.ap_always_on;
    document.getElementById('mqtt_client_enabled').checked = !!s.mqtt_client_enabled;
    document.getElementById('mqtt_control_enabled').checked = !!s.mqtt_control_enabled;
    document.getElementById('sensor_temp_enabled').checked = !!s.sensor_temp_enabled;
    document.getElementById('lora_frequency_mhz').value = (Number(s.lora_frequency_hz) / 1000000).toFixed(3);
    refreshFreqPreset();
    document.getElementById('heartbeat_s').value = Math.max(1, Math.round(Number(s.heartbeat_ms) / 1000));
    document.getElementById('ack_timeout_s').value = Math.max(1, Math.round(Number(s.ack_timeout_ms) / 1000));
    document.getElementById('mqtt_remote_retry_timeout_s').value = Math.max(1, Math.round(Number(s.mqtt_remote_retry_timeout_ms || 300000) / 1000));
    document.getElementById('tx_mqtt_remote_polling_enabled').checked = !!s.tx_mqtt_remote_polling_enabled;
    document.getElementById('tx_mqtt_remote_default_poll_interval_s').value = Math.max(60, Math.round(Number(s.tx_mqtt_remote_default_poll_interval_ms || 60000) / 1000));
    document.getElementById('rx_push_on_change_enabled').checked = !!s.rx_push_on_change_enabled;
    document.getElementById('rx_push_min_interval_s').value = Math.max(60, Math.round(Number(s.rx_push_min_interval_ms || 60000) / 1000));
    document.getElementById('rx_failsafe_mode').value = String(s.rx_failsafe_mode || 'hold_last');
    document.getElementById('rx_failsafe_timeout_s').value = Math.max(5, Math.round(Number(s.rx_failsafe_timeout_ms || 180000) / 1000));
    document.getElementById('local_address').value = String(s.local_address);
    document.getElementById('remote_address').value = String(s.remote_address);
    refreshRoleLabels();
    refreshAddressHints();
    refreshHostnamePreview();
    const f = await apiJson('/api/factory', { silent: true, timeoutMs: 6000 });
    if (f) { document.getElementById('factory').innerText = JSON.stringify(f, null, 2); }
    settingsPageLoaded = true;
  } finally {
    settingsPageLoadInFlight = false;
  }
}
async function scanWifi() {
  if (wifiScanInFlight) return;
  const btn = document.getElementById('wifiScanBtn');
  const host = document.getElementById('wifi_scan_list');
  if (!host) return;
  wifiScanInFlight = true;
  const scanPressureWindowMs = 22000;
  suspendGlobalPollsUntilMs = Math.max(suspendGlobalPollsUntilMs, Date.now() + scanPressureWindowMs);
  if (btn) { btn.disabled = true; btn.innerText = 'Scanning...'; }
  host.innerHTML = 'Scanning...';
  try {
    const start = Date.now();
    const timeoutMs = 20000;
    let transientFailures = 0;
    let out = null;
    while ((Date.now() - start) < timeoutMs) {
      out = await apiJson('/api/wifi/scan', { silent: true, timeoutMs: 5000 });
      if (!out) {
        transientFailures++;
        host.innerHTML = transientFailures > 2 ? 'Scanning... (retrying link)' : 'Scanning...';
        await sleep(transientFailures > 2 ? 1200 : 800);
        continue;
      }
      transientFailures = 0;
      if (String(out.status || '') === 'ready') { break; }
      await sleep(900);
    }
    if (!out || String(out.status || '') !== 'ready') {
      host.innerHTML = 'Scan timed out';
      return;
    }
    if (!out.networks || !out.networks.length) {
      host.innerHTML = 'No SSIDs found';
      return;
    }
    out.networks.sort((a, b) => Number(b.rssi) - Number(a.rssi));
    host.innerHTML = '<table class="wifi-table"><thead><tr><th>SSID</th><th>Signal</th><th></th></tr></thead><tbody></tbody></table>';
    const tbody = host.querySelector('tbody');
    out.networks.forEach(n => {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${escapeHtml(n.ssid)}</td><td>${sigIconHtml(n.rssi, 'scan')}${escapeHtml(n.rssi)} dBm</td><td><button type="button" data-ssid="${escapeHtml(n.ssid)}">Use</button></td>`;
      tbody.appendChild(tr);
    });
    host.querySelectorAll('button[data-ssid]').forEach(useBtn => {
      useBtn.addEventListener('click', () => {
        document.getElementById('wifi_sta_ssid').value = useBtn.getAttribute('data-ssid');
        updateStaTestButtonState();
      });
    });
  } catch (e) {
    host.innerHTML = 'Scan failed';
  } finally {
    wifiScanInFlight = false;
    if (btn) { btn.disabled = false; btn.innerText = 'Rescan SSIDs'; }
  }
}
async function save() {
  await saveAll();
}
async function postSettings(body, options) {
  const opts = Object.assign({ skipReload: false }, options || {});
  const btns = [...document.querySelectorAll('button')];
  btns.forEach(b => b.disabled = true);
  showToast('Saving...');
  let t = null;
  try {
    const controller = new AbortController();
    t = setTimeout(() => controller.abort(), 10000);
    const res = await fetch('/api/settings', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: controller.signal });
    if (res.status === 401) { location.href = '/login?expired=1'; return false; }
    const text = await res.text();
    if (!res.ok) {
      showToast(`Save failed: ${text}`, true);
      return false;
    }
    showToast('Saved');
    if (!opts.skipReload) {
      setTimeout(() => { load().catch(() => { }); }, 150);
    }
    return true;
  } catch (e) {
    showToast(`Save failed: ${e.message}`, true);
    return false;
  } finally {
    if (t) { clearTimeout(t); }
    btns.forEach(b => b.disabled = false);
  }
}
function collectLoraBody() {
  const mode = String((document.getElementById('mode_select') || {}).value || 'paired');
  const local = parseAddress(document.getElementById('local_address').value);
  const remote = parseAddress(document.getElementById('remote_address').value);
  if (!Number.isInteger(local) || local < 1 || local > 254) { alert('Local address must be 1..254 (decimal or 0xHEX).'); return; }
  if (!Number.isInteger(remote) || remote < 1 || remote > 254) { alert('Remote address must be 1..254 (decimal or 0xHEX).'); return; }
  const mhz = Number(document.getElementById('lora_frequency_mhz').value);
  if (!Number.isFinite(mhz) || mhz < FREQ_MIN_MHZ || mhz > FREQ_MAX_MHZ) { alert(`Frequency must be between ${FREQ_MIN_MHZ} and ${FREQ_MAX_MHZ} MHz.`); return; }
  const hbSec = Math.floor(Number(document.getElementById('heartbeat_s').value));
  const ackSec = Math.floor(Number(document.getElementById('ack_timeout_s').value));
  const mqttRetrySec = Math.floor(Number(document.getElementById('mqtt_remote_retry_timeout_s').value));
  const txPollDefaultSec = Math.floor(Number(document.getElementById('tx_mqtt_remote_default_poll_interval_s').value));
  const rxPushMinSec = Math.floor(Number(document.getElementById('rx_push_min_interval_s').value));
  const rxFailsafeMode = String(document.getElementById('rx_failsafe_mode').value || 'hold_last').trim().toLowerCase();
  const rxFailsafeTimeoutSec = Math.floor(Number(document.getElementById('rx_failsafe_timeout_s').value));
  if (mode === 'paired' && (!Number.isFinite(hbSec) || hbSec < 60 || hbSec > 3600)) { alert('Heartbeat must be between 60 and 3600 seconds.'); return; }
  if (!Number.isFinite(ackSec) || ackSec < 5 || ackSec > 600) { alert('ACK timeout must be between 5 and 600 seconds.'); return; }
  if (!Number.isFinite(mqttRetrySec) || mqttRetrySec < 5 || mqttRetrySec > 3600) { alert('MQTT remote retry timeout must be between 5 and 3600 seconds.'); return; }
  if (!Number.isFinite(txPollDefaultSec) || txPollDefaultSec < 60 || txPollDefaultSec > 3600) { alert('Default poll interval must be between 60 and 3600 seconds.'); return; }
  if (!Number.isFinite(rxPushMinSec) || rxPushMinSec < 60 || rxPushMinSec > 3600) { alert('RX push minimum interval must be between 60 and 3600 seconds.'); return; }
  if (rxFailsafeMode !== 'hold_last' && rxFailsafeMode !== 'force_off' && rxFailsafeMode !== 'force_on') { alert('RX failsafe mode must be hold_last, force_off, or force_on.'); return; }
  if (!Number.isFinite(rxFailsafeTimeoutSec) || rxFailsafeTimeoutSec < 5 || rxFailsafeTimeoutSec > 3600) { alert('RX failsafe timeout must be between 5 and 3600 seconds.'); return; }
  const ids = ['lora_tx_power', 'lora_spreading_factor', 'lora_bandwidth_hz', 'lora_coding_rate'];
  const body = {}; ids.forEach(id => body[id] = document.getElementById(id).value);
  const fleetPassphrase = String(document.getElementById('fleet_passphrase').value || '').trim();
  if (fleetPassphrase.length) {
    if (fleetPassphrase.length < MIN_DEPLOYMENT_KEY_LEN) {
      alert(`Deployment Key must be at least ${MIN_DEPLOYMENT_KEY_LEN} characters.`);
      return;
    }
    if (isDefaultDeploymentKey(fleetPassphrase)) {
      alert('Deployment Key cannot be the default value. Please set a unique installation key.');
      return;
    }
    body.fleet_passphrase = fleetPassphrase;
  }
  const roleName = String((document.getElementById('role_name') || {}).value || 'transmitter');
  body.mode = mode;
  body.role = roleName;
  body.role_tx = document.getElementById('role_tx_true').checked;
  body.local_address = local;
  body.remote_address = remote;
  body.input_control_paired_lora_enabled = document.getElementById('input_control_paired_lora_enabled').checked;
  body.tx_mqtt_remote_polling_enabled = document.getElementById('tx_mqtt_remote_polling_enabled').checked;
  body.rx_push_on_change_enabled = document.getElementById('rx_push_on_change_enabled').checked;
  body.lora_frequency_hz = Math.round(mhz * 1000000);
  body.heartbeat_ms = (mode === 'paired') ? (hbSec * 1000) : 60000;
  body.ack_timeout_ms = ackSec * 1000;
  body.mqtt_remote_retry_timeout_ms = mqttRetrySec * 1000;
  body.tx_mqtt_remote_default_poll_interval_ms = txPollDefaultSec * 1000;
  body.rx_push_min_interval_ms = rxPushMinSec * 1000;
  body.rx_failsafe_mode = rxFailsafeMode;
  body.rx_failsafe_timeout_ms = rxFailsafeTimeoutSec * 1000;
  return body;
}
function collectNetworkBody() {
  const ids = ['wifi_sta_ssid', 'lan_hostname'];
  const body = {}; ids.forEach(id => body[id] = document.getElementById(id).value);
  const staPass = String(document.getElementById('wifi_sta_password').value || '');
  if (staPass.length) { body.wifi_sta_password = staPass; }
  body.ap_always_on = document.getElementById('ap_always_on').checked;
  return body;
}
function collectSystemBody() {
  const body = {};
  const adminPass = String(document.getElementById('admin_password').value || '');
  if (adminPass.length) { body.admin_password = adminPass; }
  return body;
}
function collectMqttBody() {
  const ids = ['mqtt_host', 'mqtt_user', 'mqtt_topic_root', 'mqtt_controller_addresses'];
  const body = {}; ids.forEach(id => body[id] = document.getElementById(id).value);
  const mqttPass = String(document.getElementById('mqtt_password').value || '');
  if (mqttPass.length) { body.mqtt_password = mqttPass; }
  const portRaw = String(document.getElementById('mqtt_port').value || '').trim();
  const port = Number(portRaw);
  if (portRaw.length === 0 || !Number.isFinite(port) || port < 1 || port > 65535) {
    alert('MQTT port must be in range 1..65535.');
    return null;
  }
  body.mqtt_port = Math.floor(port);
  body.mqtt_client_enabled = document.getElementById('mqtt_client_enabled').checked;
  body.mqtt_control_enabled = document.getElementById('mqtt_control_enabled').checked;
  if (body.mqtt_control_enabled && !body.mqtt_client_enabled) {
    alert('MQTT control enabled requires MQTT client enabled.');
    return null;
  }
  return body;
}
function collectSensorsBody() {
  const ids = [];
  const body = {}; ids.forEach(id => body[id] = document.getElementById(id).value);
  body.sensor_temp_enabled = document.getElementById('sensor_temp_enabled').checked;
  return body;
}
async function saveLora() {
  const body = collectLoraBody();
  if (!body) return;
  await postSettings(body);
}
async function saveNetwork() {
  const body = collectNetworkBody();
  let shouldRedirect = false;
  let newHost = '';
  if (LRS_ENABLE_MDNS) {
    const oldHost = normalizeLanHost(currentLanMdns);
    newHost = normalizeLanHost(body.lan_hostname);
    const hostChanged = oldHost.length > 0 && newHost.length > 0 && oldHost !== newHost;
    shouldRedirect = hostChanged && isLikelyStaSessionPath();
  }
  const ok = await postSettings(body, shouldRedirect ? { skipReload: true } : undefined);
  if (!ok) return;
  if (shouldRedirect) {
    startLanHostnameRedirect(newHost);
  }
}
async function saveSystem() {
  await postSettings(collectSystemBody());
}
async function saveMqtt() {
  const body = collectMqttBody();
  if (!body) return;
  await postSettings(body);
}
async function saveSensors() {
  await postSettings(collectSensorsBody());
}
async function saveAll() {
  const lora = collectLoraBody();
  if (!lora) return;
  const mqtt = collectMqttBody();
  if (!mqtt) return;
  await postSettings(Object.assign({}, lora, collectNetworkBody(), mqtt, collectSensorsBody(), collectSystemBody()));
}
async function reboot() { await fetch('/api/reboot', { method: 'POST' }); }
async function factoryResetLocal() {
  const el = document.getElementById('factoryResetResult');
  const pwEl = document.getElementById('factory_reset_password');
  const confirmEl = document.getElementById('factory_reset_confirm_word');
  const keepWifiEl = document.getElementById('factory_reset_keep_wifi_local');
  if (!el || !pwEl || !confirmEl || !keepWifiEl) return;
  const password = String(pwEl.value || '');
  const confirmWord = String(confirmEl.value || '').trim().toUpperCase();
  let keepFleet = false;
  if (confirmWord === 'RESET') keepFleet = true;
  else if (confirmWord === 'REMOVE') keepFleet = false;
  const keepWifi = !!keepWifiEl.checked;
  if (!password.length) {
    el.className = 'result-line show err';
    el.innerText = 'Enter admin password to factory reset.';
    return;
  }
  if (confirmWord !== 'RESET' && confirmWord !== 'REMOVE') {
    el.className = 'result-line show err';
    el.innerText = 'Type RESET (keep fleet key) or REMOVE (clear fleet key).';
    return;
  }
  const keepBits = [];
  if (keepFleet) keepBits.push('shared fleet key');
  if (keepWifi) keepBits.push('WiFi credentials');
  el.className = 'result-line show';
  el.innerText = `Factory reset requested${keepBits.length ? ` (keep ${keepBits.join(' + ')})` : ''}...`;
  const out = await apiJson('/api/system/factory-reset', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ admin_password: password, keep_shared_fleet_key: keepFleet, keep_wifi_credentials: keepWifi }), silent: true });
  if (out && out.ok) {
    el.className = 'result-line show ok';
    el.innerText = 'Factory reset started. Device is rebooting...';
    pwEl.value = '';
    confirmEl.value = '';
    return;
  }
  const err = (out && out.error) ? out.error : 'request_failed';
  el.className = 'result-line show err';
  el.innerText = `Factory reset failed: ${err}`;
}
async function testSta() {
  const btn = document.getElementById('btnTestSta');
  const el = document.getElementById('netTestResult');
  if (!el) return;
  if (btn && btn.disabled) return;
  const requestedSsid = normalizedInputValue('wifi_sta_ssid');
  if (staIsConnected && requestedSsid.length && requestedSsid !== connectedStaSsid && isLikelyStaSessionPath()) {
    const apHint = currentApIp ? `http://${currentApIp}` : (LRS_ENABLE_MDNS && currentApMdns ? `http://${currentApMdns}` : 'the Soft AP URL');
    const msg = `Cannot test a different SSID from current LAN session (it drops this connection). Join device Soft AP and retry via ${apHint}.`;
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast('Use Soft AP for cross-SSID test', true);
    return;
  }
  staTestInFlight = true;
  updateStaTestButtonState();
  if (btn) { btn.innerText = 'Testing...'; }
  el.className = 'result-line show';
  el.innerText = 'Testing STA connection...';
  showToast('Testing STA connection...');
  const out = await apiJson('/api/network/test', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(collectNetworkBody()) });
  staTestInFlight = false;
  if (btn) { btn.innerText = 'Test'; }
  updateStaTestButtonState();
  if (!out) {
    const st = await apiJson('/api/status-live', { silent: true, timeoutMs: 5000 });
    if (st && st.sta_connected && String(st.sta_ssid || '').trim() === requestedSsid) {
      const msg = `STA connect OK${st.sta_rssi ? ` (${st.sta_rssi} dBm)` : ''}`;
      el.className = 'result-line show ok';
      el.innerText = msg;
      showToast(msg);
      return;
    }
    el.className = 'result-line show err';
    el.innerText = 'STA test failed';
    showToast('STA test failed', true);
    return;
  }
  if (out.ok) {
    const msg = `STA connect OK${out.rssi ? ` (${out.rssi} dBm)` : ''}`;
    el.className = 'result-line show ok';
    el.innerText = msg;
    showToast(msg);
  } else {
    const msg = `STA connect failed: ${out.status_text} [${out.status_code}]`;
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast(msg, true);
  }
}
let provStatusPollTimer = 0;
let provLastRowsHtml = '';
let provPollSeenActive = false;
let provPollGraceUntilMs = 0;
let suspendGlobalPollsUntilMs = 0;
let provStickySessionNonce = 0;
let provStickyRowsByChip = {};
let provStickyOrder = [];
function clearProvisioningStickyRows() {
  provStickySessionNonce = 0;
  provStickyRowsByChip = {};
  provStickyOrder = [];
  provLastRowsHtml = '';
}
function mergeProvisioningStickyRows(devices, sessionNonce) {
  const nonce = Number(sessionNonce || 0);
  if (nonce > 0 && provStickySessionNonce !== nonce) {
    provStickySessionNonce = nonce;
    provStickyRowsByChip = {};
    provStickyOrder = [];
    provLastRowsHtml = '';
  }
  const list = Array.isArray(devices) ? devices : [];
  for (const d of list) {
    const key = String((d && (d.chip_id_hex || d.chip_id)) || '');
    if (!key) continue;
    if (!Object.prototype.hasOwnProperty.call(provStickyRowsByChip, key)) {
      provStickyOrder.push(key);
    }
    provStickyRowsByChip[key] = d;
  }
  if (provStickyOrder.length > 16) {
    const drop = provStickyOrder.splice(0, provStickyOrder.length - 16);
    for (const key of drop) delete provStickyRowsByChip[key];
  }
  const out = [];
  for (const key of provStickyOrder) {
    if (Object.prototype.hasOwnProperty.call(provStickyRowsByChip, key)) out.push(provStickyRowsByChip[key]);
  }
  return out;
}
function stopProvisioningPolling() {
  if (provStatusPollTimer) { clearTimeout(provStatusPollTimer); provStatusPollTimer = 0; }
}
function ensureProvisioningUiScaffold() {
  const table = document.querySelector('#fleet-manage-pane-lora table.table');
  if (table) {
    table.classList.add('prov-table');
    const th = table.querySelectorAll('thead th');
    if (th.length >= 6) {
      th[0].innerText = 'Status';
      th[0].classList.add('prov-col-status');
      th[1].classList.add('mono', 'prov-col-chip');
      th[2].innerText = 'Cur Addr';
      th[2].classList.add('num', 'prov-col-cur');
      th[3].innerText = 'New Addr';
      th[3].classList.add('num', 'prov-col-new');
      th[4].innerText = 'FW Ver';
      th[4].classList.add('mono', 'prov-col-fw');
      th[5].classList.add('num', 'prov-col-rssi');
    }
    const wrap = table.parentElement;
    if (wrap) wrap.classList.add('prov-table-wrap');
  }
  const summary = document.getElementById('provWizardSummary');
  if (summary && !document.getElementById('provSessionLine')) {
    const sessionLine = document.createElement('div');
    sessionLine.id = 'provSessionLine';
    sessionLine.className = 'small prov-session-line';
    summary.parentNode.insertBefore(sessionLine, summary);
  }
  const provisionBtn = document.getElementById('provProvisionAllBtn');
  const actionRow = provisionBtn && provisionBtn.parentElement;
  if (actionRow && !document.getElementById('provProvisionAllReason')) {
    const reason = document.createElement('div');
    reason.id = 'provProvisionAllReason';
    reason.className = 'small prov-provision-reason';
    actionRow.insertAdjacentElement('afterend', reason);
  }
}
function provisioningSessionStateLabel(s) {
  const key = String(s || 'idle');
  if (key === 'discovering') return 'Discovering';
  if (key === 'ready') return 'Ready';
  if (key === 'provisioning') return 'Provisioning';
  if (key === 'complete') return 'Complete';
  if (key === 'error') return 'Error';
  return 'Idle';
}
function formatElapsedCompact(ms) {
  const totalSec = Math.max(0, Math.floor(Number(ms || 0) / 1000));
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}
function provisioningDisabledReason(st, discovered, sessActive) {
  if (st === 'ready' && discovered > 0) return 'Ready to provision discovered devices.';
  if (st === 'discovering') return 'Provision All unlocks when discovery finishes.';
  if (st === 'provisioning') return 'Provisioning is already in progress.';
  if (st === 'complete') return 'Session complete. Run discovery again for another batch.';
  if (st === 'error') return 'Session failed. Start discovery again.';
  if (st === 'ready' && discovered === 0) return 'No discovered devices in this session.';
  if (sessActive) return 'Provision All is not available in the current phase.';
  return 'Start discovery to enable Provision All.';
}
function provisioningDeviceStatusDisplay(d) {
  const state = String((d && d.state) || 'unknown');
  if (state === 'discovered') return '🔵 discovered';
  if (state === 'assigned') return '🧭 assigned';
  if (state === 'keying') return '🔐 keying';
  if (state === 'await_verify') return '🟠 await verify';
  if (state === 'verified') return '🟢 verified';
  if (state === 'applied_unconfirmed') return '🟠 applied (unconfirmed)';
  if (state === 'skipped') return '⚪ skipped';
  if (state === 'failed') {
    // If an address was assigned, this is often a verify-timeout/missed final ack on LoRa.
    // The target may already have applied provisioning successfully.
    if (Number(d && d.assigned_address || 0) > 0) return '🟠 unverified';
    return '🔴 failed';
  }
  return `❔ ${state}`;
}
function renderProvisioningStatus(out) {
  ensureProvisioningUiScaffold();
  const result = document.getElementById('provWizardResult');
  const sessionLine = document.getElementById('provSessionLine');
  const summary = document.getElementById('provWizardSummary');
  const rows = document.getElementById('provWizardRows');
  const provisionBtn = document.getElementById('provProvisionAllBtn');
  const provisionReason = document.getElementById('provProvisionAllReason');
  if (!summary || !rows) return;
  const sess = (out && out.session) || {};
  const incomingDevices = Array.isArray(out && out.devices) ? out.devices : [];
  provUiSessionActive = !!sess.active;
  provUiSessionState = String(sess.state || 'idle');
  const st = String(sess.state || 'idle');
  const stickyEnabled = (st === 'discovering' || st === 'ready' || st === 'provisioning');
  const devices = stickyEnabled
    ? mergeProvisioningStickyRows(incomingDevices, sess.session_nonce)
    : incomingDevices;
  const discoveredRaw = Number(sess.discovered_count || 0);
  const discoveredEffective = Math.max(discoveredRaw, devices.length);
  const compactMode = !!(sess && (sess.compact || sess.devices_truncated));
  const compactReason = String((sess && sess.compact_reason) || '');
  if (provisionBtn) {
    const canProvision = (st === 'ready' && discoveredEffective > 0);
    provisionBtn.disabled = !canProvision;
    if (provisionReason) {
      provisionReason.innerText = provisioningDisabledReason(st, discoveredEffective, !!sess.active);
      provisionReason.classList.toggle('ok', canProvision);
    }
  }
  const now = Number(sess.now_ms || 0);
  const started = Number(sess.started_ms || 0);
  const elapsedMs = (now > 0 && started > 0 && now >= started) ? (now - started) : 0;
  const elapsedTxt = formatElapsedCompact(elapsedMs);
  const estimated = Math.max(1, Number(sess.estimated_count || 0) || Number(sess.discovered_count || 0) || devices.length || 1);
  const verified = Number(sess.verified_count || 0);
  const failed = Number(sess.failed_count || 0);
  const discovered = discoveredEffective;
  const found = discovered;
  const provisioned = Math.min(found, verified + failed);
  if (sessionLine) {
    if (!sess.active) {
      sessionLine.innerText = 'No provisioning session active.';
    } else if (st === 'discovering') {
      sessionLine.innerText = `Discovering... Found ${found} (estimated ${estimated}) · elapsed ${elapsedTxt}`;
    } else if (st === 'ready') {
      sessionLine.innerText = `Found ${found} (estimated ${estimated}) · Verified ${verified} / Found ${found} · elapsed ${elapsedTxt}`;
    } else if (st === 'provisioning') {
      sessionLine.innerText = `Provisioned ${provisioned} / Found ${found} · Verified ${verified} / Found ${found} · elapsed ${elapsedTxt}`;
    } else if (st === 'complete') {
      sessionLine.innerText = `Complete · Verified ${verified} / Found ${found} · Failed ${failed} · elapsed ${elapsedTxt}`;
    } else if (st === 'error') {
      sessionLine.innerText = `Error · Verified ${verified} / Found ${found} · Failed ${failed} · elapsed ${elapsedTxt}`;
    } else {
      sessionLine.innerText = `${provisioningSessionStateLabel(st)} · elapsed ${elapsedTxt}`;
    }
  }
  let countdownTxt = '';
  if (sess.active && Number(sess.phase_deadline_ms || 0) > 0 && now > 0) {
    const rem = Math.max(0, Math.ceil((Number(sess.phase_deadline_ms) - now) / 1000));
    if (rem > 0 && (sess.state === 'discovering' || sess.state === 'provisioning')) countdownTxt = ` · next phase in ~${rem}s`;
  }
  summary.innerText = sess.active
    ? `State: ${provisioningSessionStateLabel(sess.state)} · found ${discoveredEffective} · conflicts ${Number(sess.conflict_count || 0)}${countdownTxt}`
    : 'No provisioning session active.';
  if (result && sess.active) {
    result.className = 'result-line show';
    if (sess.state === 'complete') result.className = 'result-line show ok';
    if (sess.state === 'error') result.className = 'result-line show err';
    result.innerText = `Provisioning ${provisioningSessionStateLabel(sess.state)}`;
  }
  if (!devices.length) {
    let emptyText = 'No devices discovered yet.';
    if (compactMode && (compactReason === 'low_heap' || compactReason === 'truncated_rows')) {
      emptyText = 'Low-memory mode: showing counts only (keeping rows when available).';
    } else if (sess.active && (st === 'discovering' || st === 'ready' || st === 'provisioning')) {
      emptyText = 'Awaiting device replies...';
    }
    rows.innerHTML = `<tr><td colspan="6" class="small prov-empty-row">${emptyText}</td></tr>`;
    return;
  }
  rows.innerHTML = devices.map((d) => {
    const cur = Number(d.current_address || 0);
    const nxt = Number(d.assigned_address || 0);
    const fw = d.fw_version || `${d.fw_major || 0}.${d.fw_minor || 0}.${d.fw_patch || 0}`;
    const conflict = d.address_conflict ? '<span class="prov-conflict-tag">conflict</span>' : '';
    const stateClass = String((d && d.state) || 'unknown').replace(/[^a-z_]/g, '');
    return `<tr class="prov-row state-${stateClass}"><td class="prov-status-cell">${escapeHtml(provisioningDeviceStatusDisplay(d))}${conflict}</td><td class="mono">${escapeHtml(String(d.chip_id_hex || d.chip_id || ''))}</td><td class="num">${cur || '-'}</td><td class="num">${nxt || '-'}</td><td class="mono">${escapeHtml(String(fw))}</td><td class="num">${Number(d.rssi || 0)}</td></tr>`;
  }).join('');
  provLastRowsHtml = rows.innerHTML;
}
async function refreshProvisioningStatus(silent = true) {
  if (!(activePage === 'fleet' && activeFleetTab === 'manage' && activeFleetManageTab === 'lora')) return null;
  if (provStatusInFlight) return null;
  provStatusInFlight = true;
  provLastStatusRefreshMs = Date.now();
  const out = await apiJson('/api/provisioning/status', { silent });
  if (out && out.ok) { renderProvisioningStatus(out); provStatusInFlight = false; return out; }
  provStatusInFlight = false;
  return null;
}
function startProvisioningPolling(opts) {
  stopProvisioningPolling();
  const graceMs = Math.max(0, Number((opts && opts.graceMs) || 0));
  provPollSeenActive = false;
  provPollGraceUntilMs = Date.now() + graceMs;
  let idleAfterGraceCount = 0;
  const scheduleNext = (ms) => {
    if (provStatusPollTimer) { clearTimeout(provStatusPollTimer); provStatusPollTimer = 0; }
    provStatusPollTimer = setTimeout(loop, Math.max(250, Number(ms) || 1000));
  };
  const loop = async () => {
    if (!(activePage === 'fleet' && activeFleetTab === 'manage' && activeFleetManageTab === 'lora')) {
      provStatusPollTimer = 0;
      return;
    }
    const now = Date.now();
    if (document.hidden) {
      scheduleNext(4000);
      return;
    }
    if (suspendGlobalPollsUntilMs > now) {
      scheduleNext(Math.min(2500, Math.max(500, suspendGlobalPollsUntilMs - now)));
      return;
    }
    const out = await refreshProvisioningStatus(true);
    const sess = (out && out.session) || {};
    const st = String(sess.state || 'idle');
    const activeLike = !!sess.active || st === 'discovering' || st === 'provisioning' || st === 'ready';
    if (activeLike) {
      provPollSeenActive = true;
      idleAfterGraceCount = 0;
      scheduleNext(1200);
      return;
    }
    const withinGrace = Date.now() < provPollGraceUntilMs;
    if (withinGrace) {
      scheduleNext(1200);
      return;
    }
    if (!provPollSeenActive && out == null) {
      idleAfterGraceCount++;
      if (idleAfterGraceCount < 4) {
        scheduleNext(1800);
        return;
      }
    }
    provStatusPollTimer = 0;
  };
  scheduleNext(300);
}
function syncPagePolling() {
  syncStatusLiveSse(true);
}
async function startFleetProvisioningDiscovery() {
  const result = document.getElementById('provWizardResult');
  const estEl = document.getElementById('prov_estimated_count');
  const est = Math.max(1, Math.min(8, Number(estEl && estEl.value || 2) || 2));
  clearProvisioningStickyRows();
  suspendGlobalPollsUntilMs = Date.now() + 5000;
  if (result) { result.className = 'result-line show'; result.innerText = 'Starting discovery...'; }
  const out = await apiJson('/api/provisioning/start', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ estimated_count: est }), silent: true, allowHttpError: true });
  if (out && out.ok) {
    if (out.session) { renderProvisioningStatus(out); }
    startProvisioningPolling({ graceMs: 5000 });
    if (result) { result.className = 'result-line show ok'; result.innerText = 'Discovery started. Watching live updates...'; }
    return;
  }
  if (result) { result.className = 'result-line show err'; result.innerText = `Discovery start failed: ${(out && out.error) || 'request_failed'}`; }
}
async function provisionFleetAll() {
  const result = document.getElementById('provWizardResult');
  suspendGlobalPollsUntilMs = Date.now() + 5000;
  if (result) { result.className = 'result-line show'; result.innerText = 'Provisioning discovered devices...'; }
  const out = await apiJson('/api/provisioning/provision-all', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}', silent: true, allowHttpError: true });
  if (out && out.ok) {
    clearProvisioningStickyRows();
    if (out.session) { renderProvisioningStatus(out); }
    startProvisioningPolling({ graceMs: 5000 });
    if (result) { result.className = 'result-line show ok'; result.innerText = 'Provisioning started. Waiting for verify replies...'; }
    return;
  }
  if (result) { result.className = 'result-line show err'; result.innerText = `Provisioning failed to start: ${(out && out.error) || 'request_failed'}`; }
}
async function cancelFleetProvisioning() {
  stopProvisioningPolling();
  clearProvisioningStickyRows();
  await apiJson('/api/provisioning/cancel', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}', silent: true, allowHttpError: true });
  const out = await refreshProvisioningStatus(true);
  const result = document.getElementById('provWizardResult');
  if (result) { result.className = 'result-line show'; result.innerText = out && out.session && out.session.active ? 'Provisioning session updated.' : 'Provisioning session cancelled.'; }
}
async function provisionFleetWifi() {
  const el = document.getElementById('wifiProvisionResult');
  if (!el) return;
  const body = {};
  const requestedSsid = normalizedInputValue('wifi_sta_ssid');
  const requestedPass = inputValue('wifi_sta_password');
  if (requestedSsid.length) body.wifi_sta_ssid = requestedSsid;
  if (requestedPass.length) body.wifi_sta_password = requestedPass;
  el.className = 'result-line show';
  el.innerText = 'Sending WiFi credentials over LoRa...';
  const out = await apiJson('/api/network/provision-fleet', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
    silent: true,
    allowHttpError: true,
    timeoutMs: 20000
  });
  if (out && out.ok) {
    const msg = `LoRa WiFi provisioning sent (${out.packets || '?'} packets broadcast)`;
    el.className = 'result-line show ok';
    el.innerText = msg;
    if (wifiProvisionResultTimer) { clearTimeout(wifiProvisionResultTimer); wifiProvisionResultTimer = 0; }
    wifiProvisionResultTimer = setTimeout(() => {
      const cur = document.getElementById('wifiProvisionResult');
      if (!cur) return;
      cur.className = 'result-line';
      cur.innerText = '';
      wifiProvisionResultTimer = 0;
    }, 10000);
    showToast(msg);
    return;
  }
  if (out && out.error === 'cooldown_active') {
    const sec = Math.max(1, Number(out.retry_after_s || Math.ceil(Number(out.retry_after_ms || 0) / 1000) || 1));
    const msg = `Wait ${sec}s before sending WiFi provisioning again.`;
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast(msg, true);
    return;
  }
  if (out && out.error === 'ssid_required') {
    const msg = 'Enter STA SSID in Settings > Network, then retry.';
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast(msg, true);
    return;
  }
  if (out && out.error === 'send_failed') {
    const msg = 'Fleet WiFi send failed (radio busy). Retry in a few seconds.';
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast(msg, true);
    return;
  }
  const err = (out && out.error) ? out.error : 'request_failed';
  const msg = `Fleet WiFi provisioning failed: ${err}`;
  el.className = 'result-line show err';
  el.innerText = msg;
  showToast(msg, true);
}
async function testMqtt() {
  const body = collectMqttBody();
  if (!body) return;
  const out = await apiJson('/api/mqtt/test', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const el = document.getElementById('mqttTestResult');
  if (!el) return;
  const target = (out && out.host) ? `${out.host}:${out.port}` : `${body.mqtt_host}:${body.mqtt_port}`;
  if (!out) {
    el.className = 'result-line show err';
    el.innerText = 'Broker test failed';
    showToast('Broker test failed', true);
    return;
  }
  if (out.ok) {
    const msg = `Broker connection OK (${target})`;
    el.className = 'result-line show ok';
    el.innerText = msg;
    showToast(msg);
  } else {
    const msg = `Broker connection failed (${target}, state ${out.state})`;
    el.className = 'result-line show err';
    el.innerText = msg;
    showToast(msg, true);
  }
}
async function fleetDeviceAction(action, addr, intervalS, enabled, extraBody) {
  const pathByAction = {
    poll_now: `/api/fleet/${addr}/actions/poll-now`,
    forget: `/api/fleet/${addr}/actions/forget`,
    set_interval: `/api/fleet/${addr}/actions/poll-interval`,
    set_schedule: `/api/fleet/${addr}/actions/schedule`,
    factory_reset: `/api/fleet/${addr}/actions/factory-reset`,
  };
  const path = pathByAction[action];
  if (!path) {
    showToast(`Fleet device action failed: unknown_action`, true);
    return false;
  }
  const body = {};
  if (intervalS !== undefined && intervalS !== null) { body.interval_s = intervalS; }
  if (enabled !== undefined) { body.enabled = !!enabled; }
  if (extraBody && typeof extraBody === 'object') { Object.assign(body, extraBody); }
  const out = await apiJson(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), silent: true });
  if (!out || !out.ok) {
    const err = (out && out.error) ? out.error : 'request_failed';
    showToast(`Fleet device action failed: ${err}`, true);
    return false;
  }
  return true;
}
async function fleetDeviceFactoryReset(addr) {
  const keepEl = document.getElementById('fleet-detail-keep-fleet');
  const keepFleet = keepEl ? !!keepEl.checked : true;
  if (!keepFleet) {
    alert('Warning: this will remove the device from the control of this LoRa fleet and it will require manual provisioning again.');
    const confirmWord = prompt("Type REMOVE to confirm fleet device reset without shared fleet key:");
    if (String(confirmWord || '').trim() !== 'REMOVE') {
      showToast('Fleet device factory reset cancelled', true);
      return;
    }
  }
  if (!confirm(`Factory reset fleet device ${toHexByte(addr)}${keepFleet ? ' (keep shared fleet key)' : ''}?`)) return;
  const ok = await fleetDeviceAction('factory_reset', addr, undefined, undefined, { keep_shared_fleet_key: keepFleet });
  if (!ok) return;
  showToast(`Factory reset sent to ${toHexByte(addr)}`);
  await refreshFleet();
}

function shouldRunFleetDevicesLive() {
  return activePage === 'fleet' && lastRoleIsTx && activeFleetTab === 'devices';
}
function isFleetManageActive() {
  return activePage === 'fleet' && activeFleetTab === 'manage';
}
function isProvisioningUiBusy() {
  const st = String(provUiSessionState || 'idle');
  return !!provUiSessionActive || st === 'discovering' || st === 'provisioning';
}
function sessionPollDelayMs() {
  if (document.hidden) return 1800000;
  return 1800000;
}
async function fleetDevicePollNow(addr) {
  const ok = await fleetDeviceAction('poll_now', addr);
  if (!ok) return;
  showToast(`Poll requested for ${toHexByte(addr)}`);
  await refreshFleet();
}
async function fleetDeviceForget(addr) {
  if (!confirm(`Forget ${toHexByte(addr)}?`)) return;
  const ok = await fleetDeviceAction('forget', addr);
  if (!ok) return;
  showToast(`Forgot ${toHexByte(addr)}`);
  await refreshFleet();
}
function fleetDeviceIntervalFromInput() {
  const el = document.getElementById('fleet-detail-interval');
  const sec = Math.floor(Number(el ? el.value : NaN));
  if (!Number.isFinite(sec) || sec < 60 || sec > 3600) {
    showToast('Interval must be 60..3600 seconds', true);
    return null;
  }
  return sec;
}
async function fleetDeviceSetInterval(addr) {
  const sec = fleetDeviceIntervalFromInput();
  if (sec === null) return;
  const ok = await fleetDeviceAction('set_interval', addr, sec);
  if (!ok) return;
  showToast(`Interval updated for ${toHexByte(addr)}`);
  await refreshFleet();
}
async function fleetDeviceSetSchedule(addr, enabled) {
  let sec = undefined;
  if (enabled) {
    const s = fleetDeviceIntervalFromInput();
    if (s === null) return;
    sec = s;
  }
  const ok = await fleetDeviceAction('set_schedule', addr, sec, enabled);
  if (!ok) return;
  showToast(`${enabled ? 'Enabled' : 'Disabled'} schedule for ${toHexByte(addr)}`);
  await refreshFleet();
}
function setFleetDeviceDetailTab(tab) {
  fleetDeviceDetailTab = (tab === 'manage') ? 'manage' : 'state';
  renderFleetDeviceDetail();
}
function selectFleetDevice(addr) {
  selectedFleetDeviceAddr = Number(addr || 0);
  renderFleetDeviceDetail();
}
function renderFleetDeviceDetail() {
  const host = document.getElementById('fleetDetailHost');
  if (!host) return;
  const selected = fleetDevicesCache.find((r) => Number(r.address || 0) === Number(selectedFleetDeviceAddr || 0));
  if (!selected) {
    host.innerHTML = 'Select a device to view details.';
    return;
  }
  const addr = Number(selected.address || 0);
  const addrHex = fleetDeviceAddrHex(selected);
  const seenAge = (Number(selected.last_seen_ms || 0) > 0) ? humanAgeMsShort(selected.last_seen_age_ms || 0) : 'never';
  const pollAge = (Number(selected.last_poll_tx_ms || 0) > 0) ? humanAgeMsShort(selected.last_poll_age_ms || 0) : 'never';
  const intervalS = Math.max(0, Math.round(Number(selected.poll_interval_ms || 0) / 1000));
  const webUiUrl = fleetDeviceWebUiUrl(selected);
  const webUiText = webUiUrl.length
    ? `<a href="${escapeHtml(webUiUrl)}" target="_blank" rel="noopener">Open device UI</a>`
    : '<span class="small">not known</span>';
  const stateView = `
 <div class="fleet-detail-grid">
   <div class="k">Device</div><div class="v">${escapeHtml(addrHex)}</div>
   <div class="k">Freshness</div><div class="v">${freshnessChip(selected)} ${escapeHtml(seenAge)}</div>
   <div class="k">Relay</div><div class="v">${relayChip(selected.relay_state)}</div>
   <div class="k">Input</div><div class="v">${inputChip(selected.input_state)}</div>
   <div class="k">Temperature</div><div class="v">${escapeHtml(fleetDeviceTempText(selected))}</div>
   <div class="k">Uplink RSSI</div><div class="v">${escapeHtml(String(selected.uplink_rssi || -127))} dBm</div>
   <div class="k">Downlink RSSI</div><div class="v">${selected.downlink_rssi_valid ? `${escapeHtml(String(selected.downlink_rssi))} dBm` : 'n/a'}</div>
   <div class="k">Poll state</div><div class="v"><span class="chip ${selected.poll_pending ? 'warn' : 'neutral'}">${selected.poll_pending ? 'pending' : 'idle'}</span> (last tx ${escapeHtml(pollAge)})</div>
   <div class="k">Ack</div><div class="v"><span class="chip ${ackChipClass(selected.ack_state)}">${escapeHtml(String(selected.ack_state || 'unknown'))}</span></div>
   <div class="k">Web UI</div><div class="v">${webUiText}</div>
 </div>`;
  const manageView = `
 <div class="fleet-detail-grid">
   <div class="k">Interval</div><div class="v"><div class="inline-row"><input id="fleet-detail-interval" type="number" min="60" max="3600" value="${intervalS > 0 ? intervalS : 60}" style="max-width:120px" /><span class="small">${intervalS > 0 ? `${intervalS}s active` : 'disabled'}</span></div></div>
   <div class="k">Factory reset</div><div class="v"><div class="check-row" style="margin-top:0"><input id="fleet-detail-keep-fleet" type="checkbox" checked /><label for="fleet-detail-keep-fleet">Keep shared fleet key</label></div><div class="small">Untick only if you want to remove this device from this LoRa fleet.</div></div>
 </div>
 <div class="fleet-row-actions" style="margin-top:10px">
   <button type="button" onclick="fleetDevicePollNow(${addr})">Poll now</button>
   <button type="button" onclick="fleetDeviceSetInterval(${addr})">Set interval</button>
   <button type="button" onclick="fleetDeviceSetSchedule(${addr},${intervalS === 0 ? 'true' : 'false'})">${intervalS === 0 ? 'Enable schedule' : 'Disable schedule'}</button>
   <button type="button" onclick="fleetDeviceFactoryReset(${addr})">Factory reset</button>
   <button type="button" onclick="fleetDeviceForget(${addr})">Forget</button>
 </div>`;
  host.innerHTML = `
  <div class="fleet-detail-tabs">
    <button class="tabbtn ${fleetDeviceDetailTab === 'state' ? 'active' : ''}" type="button" onclick="setFleetDeviceDetailTab('state')">State</button>
    <button class="tabbtn ${fleetDeviceDetailTab === 'manage' ? 'active' : ''}" type="button" onclick="setFleetDeviceDetailTab('manage')">Manage</button>
  </div>
  ${fleetDeviceDetailTab === 'state' ? stateView : manageView}`;
}
function updateFleetScanUi(scan) {
  const st = scan && typeof scan === 'object' ? scan : {};
  fleetScanState = Object.assign({}, fleetScanState || {}, st);
  const active = !!fleetScanState.active;
  const start = Number(fleetScanState.start_address || 1);
  const end = Number(fleetScanState.end_address || 32);
  const nextAddr = Number(fleetScanState.next_address || start);
  const intervalMs = Number(fleetScanState.interval_ms || 120);
  const sent = Number(fleetScanState.sent || 0);
  const total = Number(fleetScanState.total || Math.max(0, end - start + 1));
  const scanned = Number(fleetScanState.scanned || 0);
  const pct = Number(fleetScanState.progress_pct || 0);
  const startEl = document.getElementById('fleetScanStart');
  const endEl = document.getElementById('fleetScanEnd');
  const intEl = document.getElementById('fleetScanIntervalMs');
  const btn = document.getElementById('fleetScanBtn');
  const summary = document.getElementById('fleetScanSummary');
  if (startEl && !active && Number.isFinite(start) && start >= 1 && start <= 254) startEl.value = start;
  if (endEl && !active && Number.isFinite(end) && end >= 1 && end <= 254) endEl.value = end;
  if (intEl && !active && Number.isFinite(intervalMs)) intEl.value = intervalMs;
  if (startEl) startEl.disabled = active;
  if (endEl) endEl.disabled = active;
  if (intEl) intEl.disabled = active;
  if (btn) btn.innerText = active ? 'Cancel Scan' : 'Scan Fleet';
  if (summary) {
    if (active) {
      summary.innerText = `Scan running: ${Math.max(0, Math.min(100, pct))}% · sent ${sent}/${Math.max(0, total)} · next ${toHexByte(Math.max(1, Math.min(254, nextAddr)))}`;
    } else {
      summary.innerText = (sent > 0 && total > 0)
        ? `Last scan: ${Math.max(0, Math.min(100, pct))}% · sent ${sent}/${total}.`
        : 'Scan idle.';
    }
  }
}
async function toggleFleetScan() {
  const active = !!(fleetScanState && fleetScanState.active);
  const body = {};
  if (active) {
    body.cancel = true;
  } else {
    const startEl = document.getElementById('fleetScanStart');
    const endEl = document.getElementById('fleetScanEnd');
    const intEl = document.getElementById('fleetScanIntervalMs');
    let startAddr = Math.floor(Number(startEl && startEl.value));
    let endAddr = Math.floor(Number(endEl && endEl.value));
    let intervalMs = Math.floor(Number(intEl && intEl.value));
    if (!Number.isFinite(startAddr) || startAddr < 1 || startAddr > 254) { alert('Start address must be 1..254.'); return; }
    if (!Number.isFinite(endAddr) || endAddr < 1 || endAddr > 254) { alert('End address must be 1..254.'); return; }
    if (startAddr > endAddr) { alert('Start address must be <= end address.'); return; }
    if (!Number.isFinite(intervalMs) || intervalMs < 80 || intervalMs > 2000) { alert('Scan interval must be 80..2000 ms.'); return; }
    body.start_address = startAddr;
    body.end_address = endAddr;
    body.interval_ms = intervalMs;
  }
  const out = await apiJson('/api/fleet/scan', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), silent: true, allowHttpError: true });
  if (!out || out.ok !== true) {
    const err = (out && out.error) ? out.error : 'request_failed';
    showToast(`Fleet scan failed: ${err}`, true);
    return;
  }
  updateFleetScanUi(out);
  await refreshFleet();
}
async function refreshFleet() {
  if (!(activePage === 'fleet' && activeFleetTab === 'devices')) return;
  if (fleetRefreshInFlight) return;
  const host = document.getElementById('fleetTableHost');
  const summary = document.getElementById('fleetSummary');
  const detail = document.getElementById('fleetDetailHost');
  if (!host || !summary) return;
  fleetRefreshInFlight = true;
  const out = await apiJson('/api/fleet', { silent: true });
  if (!out) {
    summary.innerText = 'Fleet device list unavailable.';
    host.innerHTML = 'Fleet device list unavailable.';
    if (detail) detail.innerHTML = 'Device details unavailable.';
    fleetRefreshInFlight = false;
    return;
  }
  const role = String(out.role || '').toLowerCase();
  updateFleetScanUi(out.scan || {});
  if (role !== 'tx') {
    summary.innerText = 'Fleet view is TX-only in this firmware.';
    host.innerHTML = 'Switch role to TX to manage discovered devices.';
    if (detail) detail.innerHTML = 'Switch role to TX to view device details.';
    fleetRefreshInFlight = false;
    return;
  }
  const devices = Array.isArray(out.devices) ? out.devices : [];
  fleetDevicesCache = devices;
  summary.innerText = `Discovered devices: ${devices.length} | Global schedule default: ${Math.max(60, Math.round(Number(out.tx_default_poll_interval_ms || 60000) / 1000))}s | Global polling: ${out.tx_polling_enabled ? 'enabled' : 'disabled'}`;
  if (devices.length === 0) {
    host.innerHTML = 'No devices discovered yet.';
    if (detail) detail.innerHTML = 'No device selected.';
    selectedFleetDeviceAddr = 0;
    fleetRefreshInFlight = false;
    return;
  }
  if (!devices.some((r) => Number(r.address || 0) === Number(selectedFleetDeviceAddr || 0))) {
    selectedFleetDeviceAddr = Number(devices[0].address || 0);
  }
  const rows = devices.map((r) => {
    const addrHex = fleetDeviceAddrHex(r);
    const addr = Number(r.address || 0);
    const seenAge = (Number(r.last_seen_ms || 0) > 0) ? humanAgeMsShort(r.last_seen_age_ms || 0) : 'never';
    const freshness = freshnessChip(r);
    const webUiUrl = fleetDeviceWebUiUrl(r);
    const webUiCell = webUiUrl.length
      ? `<a href="${escapeHtml(webUiUrl)}" target="_blank" rel="noopener">Open</a>`
      : '<span class="small">-</span>';
    const selectedCls = addr === Number(selectedFleetDeviceAddr || 0) ? 'selected' : '';
    return `<tr class="${selectedCls}">
   <td><b>${escapeHtml(addrHex)}</b></td>
   <td>${relayChip(r.relay_state)}</td>
   <td>${inputChip(r.input_state)}</td>
   <td>${escapeHtml(fleetDeviceTempText(r))}</td>
   <td>${freshness} ${escapeHtml(seenAge)}</td>
   <td>${webUiCell}</td>
   <td><button type="button" onclick="selectFleetDevice(${addr})">View</button></td>
  </tr>`;
  }).join('');
  host.innerHTML = `<table class="fleet-table"><thead><tr><th>Device</th><th>Relay</th><th>Input</th><th>Sensors</th><th>Freshness</th><th>Web UI</th><th>View</th></tr></thead><tbody>${rows}</tbody></table>`;
  renderFleetDeviceDetail();
  fleetRefreshInFlight = false;
}
async function importConfig(file) {
  if (!file) return;
  try {
    const text = await file.text();
    const body = JSON.parse(text);
    const out = await fetch('/api/settings/import', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!out.ok) { showToast(`Import failed: ${await out.text()}`, true); return; }
    showToast('Import saved');
    await load();
  } catch (e) { showToast(`Import failed: ${e.message}`, true); }
}
async function uploadOta() {
  const f = document.getElementById('otaFile').files[0];
  const el = document.getElementById('otaResult');
  if (!f) { if (el) el.innerText = 'Select .bin file'; return; }
  if (el) el.innerText = 'Uploading...';
  const fd = new FormData();
  fd.append('firmware', f);
  try {
    const res = await fetch('/api/ota', { method: 'POST', body: fd });
    const t = await res.text();
    if (!res.ok) { if (el) el.innerText = `OTA failed: ${t}`; return; }
    if (el) el.innerText = 'OTA complete, rebooting...';
  } catch (e) { if (el) el.innerText = `OTA failed: ${e.message}`; }
}
window.addEventListener('error', (e) => {
  const st = document.getElementById('statusTable');
  if (st) { st.innerText = `UI error: ${e.message}`; }
});
function initPage() {
  const role = document.getElementById('role_tx');
  const modeSelect = document.getElementById('mode_select');
  const local = document.getElementById('local_address');
  const remote = document.getElementById('remote_address');
  const host = document.getElementById('lan_hostname');
  const staSsid = document.getElementById('wifi_sta_ssid');
  const staPass = document.getElementById('wifi_sta_password');
  const roleTx = document.getElementById('role_tx_true');
  const roleRx = document.getElementById('role_tx_false');
  const fleetKey = document.getElementById('fleet_passphrase');
  mobileActionBarEl = document.getElementById('mobileActionBar');
  mobileActionPrimaryEl = document.getElementById('mobileActionPrimary');
  mobileActionSecondaryEl = document.getElementById('mobileActionSecondary');
  const syncRole = () => {
    if (!role) return;
    const mode = String((modeSelect && modeSelect.value) || 'paired');
    if (mode === 'standalone') {
      if (roleTx) roleTx.checked = true;
      if (roleRx) roleRx.checked = false;
      if (roleRx) roleRx.disabled = true;
    } else {
      if (roleRx) roleRx.disabled = false;
    }
    role.value = roleTx && roleTx.checked ? 'true' : 'false';
    refreshRoleLabels();
  };
  if (roleTx) roleTx.addEventListener('change', syncRole);
  if (roleRx) roleRx.addEventListener('change', syncRole);
  if (modeSelect) modeSelect.addEventListener('change', syncRole);
  if (local) local.addEventListener('input', refreshAddressHints);
  if (remote) remote.addEventListener('input', refreshAddressHints);
  if (host && LRS_ENABLE_MDNS) host.addEventListener('input', refreshHostnamePreview);
  if (fleetKey) fleetKey.addEventListener('input', updateDeploymentKeyStrength);
  if (staSsid) staSsid.addEventListener('input', updateStaTestButtonState);
  if (staPass) staPass.addEventListener('input', updateStaTestButtonState);
  bindFreqPreset();
  syncRole();
  showFleetTab(activeFleetTab);
  updateDeploymentKeyStrength();
  try { applyTheme(localStorage.getItem('lrs_theme') === 'light' ? 'light' : 'dark'); } catch (e) { applyTheme('dark'); }
  const menuBtn = document.getElementById('menuBtn');
  if (menuBtn) { menuBtn.setAttribute('aria-expanded', 'false'); }
  document.addEventListener('keydown', (e) => { if (e.key === 'Escape') { toggleDrawer(false); } });
  window.addEventListener('resize', () => { updateMobileActionBar(); });
  showPage('status');
  window.addEventListener('beforeunload', () => { stopProvisioningPolling(); stopPagePolling(); stopHeaderPolling(); closeStatusLiveSse(); });
  document.addEventListener('visibilitychange', () => { syncPagePolling(); });
  load();
  syncPagePolling();
}
initPage();
