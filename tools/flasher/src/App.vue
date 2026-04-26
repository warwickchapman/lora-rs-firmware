<script setup lang="ts">
import { ref, onMounted } from 'vue';
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
  }
}

onMounted(() => {
  checkStatus();
  fetchVersion();
  syncWindowMode();
});
</script>

<template>
  <div class="h-full text-slate-200 p-6 flex flex-col overflow-hidden">
    <header class="max-w-7xl w-full mx-auto mb-6 flex items-center justify-between shrink-0">
      <div>
        <h1 class="text-3xl font-bold tracking-tight text-white flex items-center gap-3">
          <img src="./assets/logo.png" alt="Thanda" class="w-8 h-8 object-contain" />
          Thanda LoRa <span class="text-indigo-500">Flasher</span>
        </h1>
        <p class="text-slate-400 text-sm mt-1">{{ appVersion }}</p>
      </div>
      <div class="flex gap-4">
        <div :class="['glass-card px-3 py-2 flex items-center gap-2 text-sm transition-all', 
                     status.ready ? 'bg-indigo-500/10 border-indigo-500/20' : 'bg-red-500/10 border-red-500/20']"
             :title="status.message">
          <span :class="['w-2 h-2 rounded-full', status.ready ? 'bg-emerald-500 shadow-[0_0_8px_rgba(16,185,129,0.5)]' : 'bg-red-500 animate-pulse']"></span>
        </div>
        <button
          @click="toggleWindowMode"
          :disabled="isTogglingWindowMode"
          class="glass-card px-4 py-2 text-sm font-medium text-slate-300 hover:text-white hover:bg-slate-800/80 transition-all flex items-center gap-2"
          :title="isFullscreen ? 'Switch to normal windowed mode' : 'Switch to full screen mode'"
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
          {{ isFullscreen ? 'Windowed mode' : 'Full screen' }}
        </button>
        <button @click="exit()" class="glass-card px-4 py-2 text-sm font-medium text-slate-300 hover:text-white hover:bg-slate-800/80 transition-all flex items-center gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"></path><polyline points="16 17 21 12 16 7"></polyline><line x1="21" y1="12" x2="9" y2="12"></line></svg>
          Exit
        </button>
      </div>
    </header>

    <main class="max-w-7xl w-full mx-auto flex-1 min-h-0">
      <Flasher />
    </main>
  </div>
</template>

<style>
body {
  margin: 0;
  background-color: #0f172a;
}
</style>
