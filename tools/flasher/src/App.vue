<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import Flasher from './components/Flasher.vue';

interface SystemStatus {
  ready: boolean;
  message: string;
}

const status = ref<SystemStatus>({ ready: false, message: 'Checking System...' });
const appVersion = ref('');

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

onMounted(() => {
  checkStatus();
  fetchVersion();
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