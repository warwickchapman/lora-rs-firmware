const VIEW_MODE_KEY = "devmon.view_mode";
const ACTIVE_TAB_KEY = "devmon.active_tab";

const state = {
  slots: [],
  ports: [],
  viewMode: localStorage.getItem(VIEW_MODE_KEY) || "stacked",
  activeTab: Number(localStorage.getItem(ACTIVE_TAB_KEY) || "0"),
};

const dom = {
  body: document.body,
  wsStatus: document.getElementById("ws-status"),
  grid: document.getElementById("grid"),
  slotTemplate: document.getElementById("slot-template"),
  cards: new Map(),
  modeStackedBtn: document.getElementById("mode-stacked"),
  modeTabbedBtn: document.getElementById("mode-tabbed"),
  deviceTabs: document.getElementById("device-tabs"),
};

function setWsStatus(mode, text) {
  dom.wsStatus.className = `ws-status ws-${mode}`;
  dom.wsStatus.textContent = text;
}

async function copyTextToClipboard(text) {
  if (navigator.clipboard && window.isSecureContext) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const ta = document.createElement("textarea");
  ta.value = text;
  ta.style.position = "fixed";
  ta.style.left = "-9999px";
  document.body.appendChild(ta);
  ta.focus();
  ta.select();
  const ok = document.execCommand("copy");
  document.body.removeChild(ta);
  if (!ok) throw new Error("copy failed");
}

async function api(path, method = "GET", body = null) {
  const res = await fetch(path, {
    method,
    headers: body ? { "content-type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  if (!res.ok) {
    const t = await res.text();
    throw new Error(`${method} ${path} failed: ${res.status} ${t}`);
  }
  if (res.status === 204) {
    return null;
  }
  return res.json();
}

function pingText(ping) {
  if (!ping) return "unknown";
  if (ping.state === "online") {
    if (typeof ping.latency_ms === "number") return `online ${ping.latency_ms.toFixed(1)}ms`;
    return "online";
  }
  if (ping.state === "offline") return "offline";
  if (ping.state === "error") return "error";
  return "unknown";
}

function serialText(serial) {
  if (!serial) return "serial: idle";
  if (serial.running) return "serial: connected";
  if (serial.wanted) {
    const retryIn = Number(serial.retry_in_s ?? 0);
    if (serial.error) return `serial retry in ${Math.max(0, Math.ceil(retryIn))}s (${serial.error})`;
    return `serial retry in ${Math.max(0, Math.ceil(retryIn))}s`;
  }
  if (serial.error) return `serial error: ${serial.error}`;
  return "serial: stopped";
}

function serialIndicatorClass(serial) {
  if (!serial) return "serial-idle";
  if (serial.running) return "serial-ok";
  if (serial.wanted) return "serial-retry";
  if (serial.error) return "serial-error";
  return "serial-idle";
}

function refreshPortSelect(selectEl, currentPort) {
  const values = new Set(state.ports);
  if (currentPort) values.add(currentPort);

  const sorted = Array.from(values).sort();
  selectEl.innerHTML = "";

  if (!sorted.length) {
    const opt = document.createElement("option");
    opt.value = "";
    opt.textContent = "No serial ports found";
    selectEl.appendChild(opt);
    return;
  }

  for (const port of sorted) {
    const opt = document.createElement("option");
    opt.value = port;
    opt.textContent = port;
    if (port === currentPort) opt.selected = true;
    selectEl.appendChild(opt);
  }
}

function setViewMode(mode) {
  state.viewMode = mode === "tabbed" ? "tabbed" : "stacked";
  localStorage.setItem(VIEW_MODE_KEY, state.viewMode);
  renderModeControls();
  applyCardVisibility();
}

function setActiveTab(slotId) {
  state.activeTab = slotId;
  localStorage.setItem(ACTIVE_TAB_KEY, String(slotId));
  renderDeviceTabs();
  applyCardVisibility();
}

function renderModeControls() {
  dom.modeStackedBtn.classList.toggle("mode-active", state.viewMode === "stacked");
  dom.modeTabbedBtn.classList.toggle("mode-active", state.viewMode === "tabbed");
  dom.body.classList.toggle("view-tabbed", state.viewMode === "tabbed");
}

function renderDeviceTabs() {
  dom.deviceTabs.innerHTML = "";
  const showTabs = state.viewMode === "tabbed";
  dom.deviceTabs.style.display = showTabs ? "flex" : "none";
  if (!showTabs) return;

  for (const slot of state.slots) {
    const btn = document.createElement("button");
    btn.className = "device-tab";
    if (slot.slot_id === state.activeTab) btn.classList.add("device-tab-active");
    const pingState = slot.ping?.state || "unknown";
    const serialClass = serialIndicatorClass(slot.serial);
    btn.innerHTML = `<span>Device ${slot.slot_id + 1}</span><span class="tab-ping ping-${pingState}"></span><span class="tab-serial ${serialClass}"></span>`;
    btn.addEventListener("click", () => setActiveTab(slot.slot_id));
    dom.deviceTabs.appendChild(btn);
  }
}

function applyCardVisibility() {
  for (const [slotId, card] of dom.cards.entries()) {
    const hidden = state.viewMode === "tabbed" && slotId !== state.activeTab;
    card.root.classList.toggle("hidden", hidden);
  }
}

function renderSlotCard(slot) {
  let card = dom.cards.get(slot.slot_id);
  if (!card) {
    const fragment = dom.slotTemplate.content.cloneNode(true);
    const root = fragment.querySelector(".slot-card");
    root.dataset.slotId = String(slot.slot_id);
    const refs = {
      root,
      title: root.querySelector(".slot-title"),
      badge: root.querySelector(".ping-badge"),
      ipInput: root.querySelector(".ip-input"),
      portSelect: root.querySelector(".port-select"),
      baudInput: root.querySelector(".baud-input"),
      saveBtn: root.querySelector(".save-btn"),
      startBtn: root.querySelector(".start-btn"),
      stopBtn: root.querySelector(".stop-btn"),
      clearBtn: root.querySelector(".clear-btn"),
      serialIndicator: root.querySelector(".serial-indicator"),
      copyLogBtn: root.querySelector(".copy-log-btn"),
      logPane: root.querySelector(".log-pane"),
    };

    refs.saveBtn.addEventListener("click", async () => {
      try {
        await api(`/api/slots/${slot.slot_id}/config`, "POST", {
          ip: refs.ipInput.value.trim(),
          port: refs.portSelect.value.trim(),
          baud: Number(refs.baudInput.value || 115200),
        });
      } catch (err) {
        refs.serialIndicator.title = String(err);
        refs.serialIndicator.className = "serial-indicator serial-error";
      }
    });

    refs.startBtn.addEventListener("click", async () => {
      try {
        await api(`/api/slots/${slot.slot_id}/serial/start`, "POST");
      } catch (err) {
        refs.serialIndicator.title = String(err);
        refs.serialIndicator.className = "serial-indicator serial-error";
      }
    });

    refs.stopBtn.addEventListener("click", async () => {
      try {
        await api(`/api/slots/${slot.slot_id}/serial/stop`, "POST");
      } catch (err) {
        refs.serialIndicator.title = String(err);
        refs.serialIndicator.className = "serial-indicator serial-error";
      }
    });

    refs.clearBtn.addEventListener("click", async () => {
      try {
        await api(`/api/slots/${slot.slot_id}/logs/clear`, "POST");
      } catch (err) {
        refs.serialIndicator.title = String(err);
        refs.serialIndicator.className = "serial-indicator serial-error";
      }
    });

    refs.copyLogBtn.addEventListener("click", async () => {
      try {
        await copyTextToClipboard(refs.logPane.textContent || "");
        refs.copyLogBtn.classList.add("copy-ok");
        refs.copyLogBtn.textContent = "Copied";
        setTimeout(() => {
          refs.copyLogBtn.classList.remove("copy-ok");
          refs.copyLogBtn.textContent = "Copy";
        }, 800);
      } catch {
        refs.copyLogBtn.textContent = "Err";
        setTimeout(() => {
          refs.copyLogBtn.textContent = "Copy";
        }, 900);
      }
    });

    dom.grid.appendChild(fragment);
    card = refs;
    dom.cards.set(slot.slot_id, card);
  }

  card.title.textContent = `Device ${slot.slot_id + 1}`;
  card.ipInput.value = slot.ip;
  card.baudInput.value = String(slot.baud || 115200);

  const ping = slot.ping || { state: "unknown" };
  card.badge.className = `ping-badge ping-${ping.state || "unknown"}`;
  card.badge.textContent = pingText(ping);

  refreshPortSelect(card.portSelect, slot.port || "");

  const indicatorClass = serialIndicatorClass(slot.serial);
  card.serialIndicator.className = `serial-indicator ${indicatorClass}`;
  card.serialIndicator.title = serialText(slot.serial);

  card.logPane.textContent = (slot.logs || []).join("\n");
  card.logPane.scrollTop = card.logPane.scrollHeight;
}

function renderAll() {
  state.slots.forEach(renderSlotCard);
  renderDeviceTabs();
  applyCardVisibility();
}

function applySlot(slot) {
  const idx = state.slots.findIndex((s) => s.slot_id === slot.slot_id);
  if (idx >= 0) state.slots[idx] = slot;
  else {
    state.slots.push(slot);
    state.slots.sort((a, b) => a.slot_id - b.slot_id);
  }
  if (!state.slots.some((s) => s.slot_id === state.activeTab)) state.activeTab = state.slots[0]?.slot_id ?? 0;
  renderSlotCard(slot);
  renderDeviceTabs();
  applyCardVisibility();
}

function appendLog(slotId, line) {
  const slot = state.slots.find((s) => s.slot_id === slotId);
  if (!slot) return;
  if (!Array.isArray(slot.logs)) slot.logs = [];
  slot.logs.push(line);
  if (slot.logs.length > 800) slot.logs = slot.logs.slice(slot.logs.length - 800);

  const card = dom.cards.get(slotId);
  if (card) {
    card.logPane.textContent = slot.logs.join("\n");
    card.logPane.scrollTop = card.logPane.scrollHeight;
  }
}

function applyEvent(event) {
  if (event.type === "snapshot") {
    state.slots = event.data.slots || [];
    state.ports = event.data.ports || [];
    if (!state.slots.some((s) => s.slot_id === state.activeTab)) state.activeTab = state.slots[0]?.slot_id ?? 0;
    renderAll();
    return;
  }

  if (event.type === "slot") {
    applySlot(event.slot);
    return;
  }

  if (event.type === "ports") {
    state.ports = event.ports || [];
    for (const slot of state.slots) {
      const card = dom.cards.get(slot.slot_id);
      if (card) refreshPortSelect(card.portSelect, slot.port || "");
    }
    return;
  }

  if (event.type === "ping") {
    const slot = state.slots.find((s) => s.slot_id === event.slot_id);
    if (!slot) return;
    slot.ping = { state: event.state, latency_ms: event.latency_ms, last_ok_at: event.at };
    const card = dom.cards.get(event.slot_id);
    if (card) {
      card.badge.className = `ping-badge ping-${event.state || "unknown"}`;
      card.badge.textContent = pingText(slot.ping);
    }
    renderDeviceTabs();
    return;
  }

  if (event.type === "serial_status") {
    const slot = state.slots.find((s) => s.slot_id === event.slot_id);
    if (!slot) return;
    slot.serial = {
      running: Boolean(event.running),
      error: event.error || null,
      wanted: Boolean(event.wanted),
      retry_in_s: event.retry_in_s,
    };
    const card = dom.cards.get(event.slot_id);
    if (card) {
      card.serialIndicator.className = `serial-indicator ${serialIndicatorClass(slot.serial)}`;
      card.serialIndicator.title = serialText(slot.serial);
    }
    renderDeviceTabs();
    return;
  }

  if (event.type === "serial_line") {
    appendLog(event.slot_id, event.line || "");
    if (event.ip_detected) {
      const slot = state.slots.find((s) => s.slot_id === event.slot_id);
      if (slot) slot.ip = event.ip_detected;
      const card = dom.cards.get(event.slot_id);
      if (card) card.ipInput.value = event.ip_detected;
    }
    return;
  }

  if (event.type === "logs_cleared") {
    const slot = state.slots.find((s) => s.slot_id === event.slot_id);
    if (slot) slot.logs = [];
    const card = dom.cards.get(event.slot_id);
    if (card) card.logPane.textContent = "";
  }
}

function connectWs() {
  const proto = window.location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${window.location.host}/ws`);
  let heartbeatId = null;

  ws.addEventListener("open", () => {
    setWsStatus("connected", "connected");
    heartbeatId = setInterval(() => {
      if (ws.readyState === WebSocket.OPEN) ws.send("ping");
    }, 15000);
  });

  ws.addEventListener("message", (msg) => {
    try {
      applyEvent(JSON.parse(msg.data));
    } catch {
      // no-op
    }
  });

  ws.addEventListener("close", () => {
    if (heartbeatId) clearInterval(heartbeatId);
    setWsStatus("disconnected", "reconnecting");
    setTimeout(connectWs, 1000);
  });

  ws.addEventListener("error", () => ws.close());
}

function attachUiHandlers() {
  dom.modeStackedBtn.addEventListener("click", () => setViewMode("stacked"));
  dom.modeTabbedBtn.addEventListener("click", () => setViewMode("tabbed"));

  window.addEventListener("keydown", (event) => {
    if (event.target && ["INPUT", "SELECT", "TEXTAREA"].includes(event.target.tagName)) return;
    const k = String(event.key || "").toLowerCase();

    if (k === "s") {
      setViewMode("stacked");
      return;
    }
    if (k === "t") {
      setViewMode("tabbed");
      return;
    }

    if (state.viewMode !== "tabbed") return;
    if (!["1", "2", "3"].includes(k)) return;
    const idx = Number(k) - 1;
    const slot = state.slots[idx];
    if (slot) setActiveTab(slot.slot_id);
  });
}

async function boot() {
  attachUiHandlers();
  renderModeControls();
  setWsStatus("connecting", "connecting");
  try {
    const snapshot = await api("/api/state");
    applyEvent({ type: "snapshot", data: snapshot });
  } catch (err) {
    setWsStatus("disconnected", String(err));
  }
  connectWs();
}

boot();
