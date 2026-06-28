<script setup lang="ts">
import { computed } from 'vue';

export interface MqttDraftState {
  host: string;
  port: number;
  topicRoot: string;
}

export interface LocalBrokerState {
  running: boolean;
  error: string;
  lans: string[];
  isStarting: boolean;
  isClientConnecting: boolean;
  clientMessage: string;
}

export interface MqttSettingsState {
  connected: boolean;
  host: string;
  port: number;
}

const sessionConnectionType = defineModel<string>({ required: true });
const showSessionConfigPanel = defineModel<boolean>('showSessionConfigPanel', { default: false });
const localBrokerPort = defineModel<number>('localBrokerPort', { default: 1883 });
const mqttDraft = defineModel<MqttDraftState>('mqttDraft', {
  default: () => ({
    host: 'venus.local',
    port: 1883,
    topicRoot: 'lora'
  })
});

defineProps<{
  isSessionConnected: boolean;
  localBrokerState: LocalBrokerState;
  mqttSettingsState: MqttSettingsState;
}>();

const emit = defineEmits<{
  (e: 'start-local-broker'): void;
  (e: 'copy-gateway-settings'): void;
  (e: 'toggle-mqtt-connection'): void;
}>();

const computedMqttDraftHost = computed({
  get: () => mqttDraft.value.host,
  set: (val) => { mqttDraft.value = { ...mqttDraft.value, host: val }; }
});

const computedMqttDraftPort = computed({
  get: () => mqttDraft.value.port,
  set: (val) => { mqttDraft.value = { ...mqttDraft.value, port: val }; }
});

const computedMqttDraftTopicRoot = computed({
  get: () => mqttDraft.value.topicRoot,
  set: (val) => { mqttDraft.value = { ...mqttDraft.value, topicRoot: val }; }
});
</script>

<template>
  <!-- Session Gateway Connection Settings Banner -->
  <div class="glass-card p-3 shrink-0 flex flex-wrap items-center justify-between gap-3 text-xs border border-slate-800/80 bg-slate-900/40 text-left">
    <div class="flex items-center gap-3">
      <span class="font-bold text-slate-300">Gateway Session:</span>
      <select v-model="sessionConnectionType" class="glass-input h-8 appearance-none min-w-[10rem] py-0 px-2 text-xs">
        <option value="serial">USB Serial Gateway</option>
        <option value="mqtt">Remote MQTT Broker</option>
        <option value="local_broker">Local MQTT Broker</option>
      </select>
      
      <span v-if="sessionConnectionType === 'mqtt'" :class="['inline-flex h-8 items-center rounded border px-2.5 text-[10px] font-mono font-bold', mqttSettingsState.connected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
        Client: {{ mqttSettingsState.connected ? 'Connected' : 'Offline' }}
      </span>

      <span :class="['inline-flex h-8 items-center rounded border px-2.5 text-[10px] font-mono font-bold transition-all',
                     isSessionConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-800 bg-slate-950/20 text-slate-400']">
        <span :class="['w-1.5 h-1.5 rounded-full mr-1.5', isSessionConnected ? 'bg-emerald-500' : 'bg-slate-500']"></span>
        Session: {{ isSessionConnected ? 'Active' : 'Offline' }}
      </span>
    </div>

    <div class="flex items-center gap-2">
      <button v-if="sessionConnectionType !== 'serial'" @click="showSessionConfigPanel = !showSessionConfigPanel" class="glass-input h-8 px-3 hover:bg-slate-700/70 text-[11px] font-bold">
        Configure Session Connection
      </button>
    </div>
  </div>

  <!-- Dropdown Session Configuration Panel -->
  <div v-if="showSessionConfigPanel && sessionConnectionType !== 'serial'" class="glass-card p-3 shrink-0 grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-3 text-left border border-slate-800">
    <div v-if="sessionConnectionType === 'local_broker'" class="flex flex-col gap-1.5 text-xs">
      <label class="font-semibold text-slate-400">Local Port</label>
      <input v-model.number="localBrokerPort" :disabled="localBrokerState.running" type="number" class="glass-input h-9 px-2 text-xs" />
      <span class="text-[9px] text-slate-500">
        {{ localBrokerState.running ? 'Port locked while running until Flasher exits.' : 'Default port is 1883.' }}
      </span>
      <span v-if="localBrokerState.error" class="text-[10px] text-rose-300 mt-1">
        {{ localBrokerState.error }}
      </span>
    </div>

    <div v-if="sessionConnectionType === 'local_broker'" class="flex flex-col gap-1.5 text-xs lg:col-span-2">
      <label class="font-semibold text-slate-400">LAN Host Details (For manual gateway config)</label>
      <div class="glass-input h-9 flex items-center px-2 text-slate-300 overflow-x-auto whitespace-nowrap custom-scrollbar">
        IP: {{ localBrokerState.lans.join(' / ') || '127.0.0.1' }}
      </div>
      <div class="flex flex-col gap-1 mt-1">
        <div class="flex gap-2">
          <button v-if="!localBrokerState.running" @click="emit('start-local-broker')" :disabled="localBrokerState.isStarting || localBrokerState.isClientConnecting" class="primary-btn h-7 px-2 text-[10px] whitespace-nowrap disabled:opacity-60">
            {{ localBrokerState.isStarting ? 'Starting...' : 'Retry Start/Connect' }}
          </button>
          <button @click="emit('copy-gateway-settings')" class="glass-input h-7 px-2 text-[10px] whitespace-nowrap">
            Copy Gateway MQTT Settings
          </button>
        </div>
        <span class="text-[9px] text-slate-400 mt-1">
          Paste into the gateway Settings over USB, or use these values when configuring MQTT manually.
        </span>
      </div>
      <span
        v-if="localBrokerState.clientMessage"
        :class="['text-[10px] mt-1', mqttSettingsState.connected && mqttSettingsState.host === '127.0.0.1' && mqttSettingsState.port === localBrokerPort ? 'text-emerald-300' : 'text-slate-400']"
      >
        {{ localBrokerState.clientMessage }}
      </span>
    </div>

    <div v-if="sessionConnectionType === 'mqtt'" class="flex flex-col gap-1.5 text-xs lg:col-span-2">
      <label class="font-semibold text-slate-400">Remote MQTT Broker Settings</label>
      <div class="grid grid-cols-3 gap-2">
        <input v-model="computedMqttDraftHost" placeholder="Host" class="glass-input h-8 px-2 text-xs" />
        <input v-model.number="computedMqttDraftPort" placeholder="Port" type="number" class="glass-input h-8 px-2 text-xs" />
        <input v-model="computedMqttDraftTopicRoot" placeholder="Topic Root" class="glass-input h-8 px-2 text-xs" />
      </div>
      <div class="flex gap-2 mt-1">
        <button @click="emit('toggle-mqtt-connection')" class="primary-btn h-7 px-2 text-[10px]">
          {{ mqttSettingsState.connected ? 'Disconnect Client' : 'Connect Client' }}
        </button>
      </div>
    </div>
  </div>
</template>
