<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { exit } from '@tauri-apps/plugin-process';
import Flasher from './components/Flasher.vue';

interface SystemStatus {
  ready: boolean;
  message: string;
}

const status = ref<SystemStatus>({ ready: false, message: 'Checking System...' });
const appVersion = ref('');
const isFullscreen = ref(true);
const isTogglingWindowMode = ref(false);
const WINDOW_MODE_KEY = 'flasher.windowMode';
const MODE_STORAGE_KEY = 'thanda-flasher-active-mode';

type ActiveMode = 'pair' | 'serial' | 'network' | 'monitor';

function initialActiveMode(): ActiveMode {
  try {
    const saved = localStorage.getItem(MODE_STORAGE_KEY);
    if (saved === 'pair' || saved === 'serial' || saved === 'network' || saved === 'monitor') return saved;
    return 'serial';
  } catch {
    return 'serial';
  }
}

const activeMode = ref<ActiveMode>(initialActiveMode());

async function fetchVersion() {
  try {
    appVersion.value = await invoke('get_app_version');
  } catch (e) {
    appVersion.value = 'v0.0.0-unknown';
  }
}

async function checkStatus() {
  try {
    status.value = await invoke('get_system_status');
  } catch (e) {
    status.value = { ready: false, message: 'Backend Error' };
  }
}

async function syncWindowMode() {
  try {
    isFullscreen.value = await getCurrentWindow().isFullscreen();
  } catch {
    // Keep a safe default if the window API is unavailable.
    isFullscreen.value = true;
  }
}

const isMacOS = /mac os x/.test(navigator.userAgent.toLowerCase());

function getSavedFullscreenPreference(): boolean | null {
  try {
    const value = localStorage.getItem(WINDOW_MODE_KEY);
    if (value === 'fullscreen') return true;
    if (value === 'windowed') return false;
  } catch {
    // Ignore storage read failures and use runtime state.
  }
  return null;
}

function saveFullscreenPreference(fullscreen: boolean) {
  try {
    localStorage.setItem(WINDOW_MODE_KEY, fullscreen ? 'fullscreen' : 'windowed');
  } catch {
    // Ignore storage write failures; runtime toggle still works.
  }
}

async function sleep(ms: number) {
  await new Promise(resolve => setTimeout(resolve, ms));
}

async function refreshWindowModeWithRetry(retries = 6, delayMs = 120) {
  for (let i = 0; i < retries; i += 1) {
    await syncWindowMode();
    if (!isTogglingWindowMode.value || i === retries - 1) {
      return;
    }
    await sleep(delayMs);
  }
}

async function setWindowFullscreenMode(fullscreen: boolean) {
  const window = getCurrentWindow();
  if (!isMacOS) {
    await window.setFullscreen(fullscreen);
    return;
  }

  if (!fullscreen) {
    // Exiting native fullscreen on macOS is most reliable via setFullscreen(false).
    try {
      await window.setFullscreen(false);
      await sleep(120);
      if (!(await window.isFullscreen())) return;
    } catch {
      // Continue to fallback path.
    }

    // Fallback for simple-fullscreen sessions.
    await window.setSimpleFullscreen(false);
    return;
  }

  // Entering fullscreen: native first, simple fullscreen as fallback.
  try {
    await window.setFullscreen(true);
  } catch {
    await window.setSimpleFullscreen(true);
  }
}

async function applySavedWindowModePreference() {
  const preferredFullscreen = getSavedFullscreenPreference();
  if (preferredFullscreen === null) return;

  const window = getCurrentWindow();
  const currentlyFullscreen = await window.isFullscreen();
  if (currentlyFullscreen === preferredFullscreen) {
    return;
  }

  await setWindowFullscreenMode(preferredFullscreen);
}

async function toggleWindowMode() {
  if (isTogglingWindowMode.value) return;
  isTogglingWindowMode.value = true;
  try {
    const window = getCurrentWindow();
    const currentlyFullscreen = await window.isFullscreen();
    await setWindowFullscreenMode(!currentlyFullscreen);
    await refreshWindowModeWithRetry();
  } catch {
    // Ignore window-mode toggle failures and keep UI responsive.
  } finally {
    isTogglingWindowMode.value = false;
    await syncWindowMode();
    saveFullscreenPreference(isFullscreen.value);
  }
}

onMounted(async () => {
  checkStatus();
  fetchVersion();
  await syncWindowMode();
  await applySavedWindowModePreference();
  await syncWindowMode();
  saveFullscreenPreference(isFullscreen.value);
});

watch(activeMode, (mode) => {
  try {
    localStorage.setItem(MODE_STORAGE_KEY, mode);
  } catch {
    // Ignore storage failures; the mode toggle itself should stay responsive.
  }
});
</script>

<template>
  <div class="h-full text-slate-200 p-3 flex flex-col overflow-hidden">
    <header class="w-full mb-3 flex items-center justify-between gap-4 shrink-0 border-b border-slate-700/80 pb-2">
      <div class="min-w-0">
        <h1 class="text-lg font-bold tracking-tight text-white flex items-center gap-2 whitespace-nowrap">
          <img src="./assets/logo.png" alt="Thanda" class="w-5 h-5 object-contain" />
          Thanda LoRa <span class="text-cyan-400">Flasher</span>
        </h1>
        <p class="text-slate-400 text-xs leading-none">{{ appVersion }}</p>
      </div>
      <div class="flex min-w-[28rem] max-w-xl flex-1 overflow-hidden rounded border border-slate-700 bg-slate-900/70">
        <button
          @click="activeMode = 'serial'"
          :class="['m-0 h-8 w-1/4 rounded-none border-r border-slate-800 px-3 text-xs font-semibold transition-colors shadow-none', activeMode === 'serial' ? 'bg-cyan-700 text-white' : 'bg-transparent text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
        >
          Flash
        </button>
        <button
          @click="activeMode = 'pair'"
          :class="['m-0 h-8 w-1/4 rounded-none border-r border-slate-800 px-3 text-xs font-semibold transition-colors shadow-none', activeMode === 'pair' ? 'bg-cyan-700 text-white' : 'bg-transparent text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
        >
          Provision
        </button>
        <button
          @click="activeMode = 'network'"
          :class="['m-0 h-8 w-1/4 rounded-none border-r border-slate-800 px-3 text-xs font-semibold transition-colors shadow-none', activeMode === 'network' ? 'bg-cyan-700 text-white' : 'bg-transparent text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
        >
          Fleet
        </button>
        <button
          @click="activeMode = 'monitor'"
          :class="['m-0 h-8 w-1/4 rounded-none px-3 text-xs font-semibold transition-colors shadow-none', activeMode === 'monitor' ? 'bg-cyan-700 text-white' : 'bg-transparent text-slate-400 hover:bg-slate-800 hover:text-slate-100']"
        >
          Monitor
        </button>
      </div>
      <div class="flex gap-2">
        <div :class="['glass-card h-8 px-2 flex items-center gap-2 text-sm transition-all',
                     status.ready ? 'bg-cyan-500/10 border-cyan-500/30' : 'bg-red-500/10 border-red-500/20']"
             :title="status.message">
          <span :class="['w-2 h-2 rounded-full', status.ready ? 'bg-emerald-500' : 'bg-red-500 animate-pulse']"></span>
        </div>
        <button
          @click="toggleWindowMode"
          :disabled="isTogglingWindowMode"
          class="glass-card h-8 w-8 p-0 text-slate-300 hover:text-white hover:bg-slate-800/80 transition-all flex items-center justify-center disabled:opacity-60"
          :title="isFullscreen ? 'Switch to normal windowed mode' : 'Switch to full screen mode'"
          :aria-label="isFullscreen ? 'Switch to normal windowed mode' : 'Switch to full screen mode'"
        >
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path v-if="isFullscreen" d="M8 3H5a2 2 0 0 0-2 2v3"></path>
            <path v-if="isFullscreen" d="M16 3h3a2 2 0 0 1 2 2v3"></path>
            <path v-if="isFullscreen" d="M8 21H5a2 2 0 0 1-2-2v-3"></path>
            <path v-if="isFullscreen" d="M16 21h3a2 2 0 0 0 2-2v-3"></path>
            <path v-if="!isFullscreen" d="M15 3h6v6"></path>
            <path v-if="!isFullscreen" d="M9 21H3v-6"></path>
            <path v-if="!isFullscreen" d="M21 3l-7 7"></path>
            <path v-if="!isFullscreen" d="M3 21l7-7"></path>
          </svg>
        </button>
        <button
          @click="exit()"
          class="glass-card h-8 w-8 p-0 text-slate-300 hover:text-white hover:bg-slate-800/80 transition-all flex items-center justify-center"
          title="Close"
          aria-label="Close"
        >
          <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.25" stroke-linecap="round" stroke-linejoin="round">
            <path d="M18 6 6 18"></path>
            <path d="m6 6 12 12"></path>
          </svg>
        </button>
      </div>
    </header>

    <main class="w-full flex-1 min-h-0">
      <Flasher v-model:active-mode="activeMode" />
    </main>
  </div>
</template>

<style>
body {
  margin: 0;
  background-color: #15212a;
}
</style>
