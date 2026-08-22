<script setup lang="ts">
import { computed } from 'vue';
import type { MqttDraftState } from '../../composables/useMqttConnection';

export type GatewaySessionTransport = 'serial' | 'mqtt' | 'local_broker';

export interface GatewaySessionForm {
  transport: GatewaySessionTransport;
  selectedSerialPort: string;
  selectedMqttGatewayChipId: string;
  selectedMqttManualChipId: string;
  adminPassword: string;
  showAdminPassword: boolean;
}

export interface GatewaySessionDisplayState {
  state: 'active' | 'partial' | 'offline' | 'error';
  label: string;
  changeDisabled: boolean;
  changeDisabledReason: string;
  mqttConnectionState: 'disconnected' | 'connecting' | 'connected' | 'error';
  mqttError: string;
}

export interface GatewaySessionTransportState {
  ports: Array<{ port_name: string; description?: string }>;
  mqttGatewayOptions: Array<{ chip_id: string; label: string }>;
  isSelectedMqttGatewayDiscovered: boolean;
  manualMqttGatewayError: string;
  serialPortSelectorDisabled: boolean;
}

export interface GatewaySessionLocalBrokerState {
  running: boolean;
  error: string;
  lans: string[];
  isStarting: boolean;
  isClientConnecting: boolean;
  clientMessage: string;
}

const form = defineModel<GatewaySessionForm>({ required: true });
const showConnectionSettings = defineModel<boolean>('showConnectionSettings', { default: false });
const localBrokerPort = defineModel<number>('localBrokerPort', { default: 1883 });
const mqttDraft = defineModel<MqttDraftState>('mqttDraft', { required: true });

const props = defineProps<{
  displayState: GatewaySessionDisplayState;
  transportState: GatewaySessionTransportState;
  localBrokerState: GatewaySessionLocalBrokerState;
}>();

const emit = defineEmits<{
  (e: 'manual-chip-input', value: string): void;
  (e: 'connect-mqtt'): void;
  (e: 'disconnect-mqtt'): void;
  (e: 'start-local-broker'): void;
  (e: 'copy-gateway-settings'): void;
}>();

const selectedSerialPort = computed({
  get: () => form.value.selectedSerialPort,
  set: value => { form.value = { ...form.value, selectedSerialPort: value }; }
});
const selectedMqttGatewayChipId = computed({
  get: () => form.value.selectedMqttGatewayChipId,
  set: value => { form.value = { ...form.value, selectedMqttGatewayChipId: value }; }
});
const selectedMqttManualChipId = computed({
  get: () => form.value.selectedMqttManualChipId,
  set: value => { form.value = { ...form.value, selectedMqttManualChipId: value }; }
});
const adminPassword = computed({
  get: () => form.value.adminPassword,
  set: value => { form.value = { ...form.value, adminPassword: value }; }
});
const showAdminPassword = computed({
  get: () => form.value.showAdminPassword,
  set: value => { form.value = { ...form.value, showAdminPassword: value }; }
});
const mqttDraftHost = computed({
  get: () => mqttDraft.value.host,
  set: value => { mqttDraft.value = { ...mqttDraft.value, host: value }; }
});
const mqttDraftPort = computed({
  get: () => mqttDraft.value.port,
  set: value => { mqttDraft.value = { ...mqttDraft.value, port: value }; }
});
const mqttDraftTopicRoot = computed({
  get: () => mqttDraft.value.topicRoot,
  set: value => { mqttDraft.value = { ...mqttDraft.value, topicRoot: value }; }
});
const mqttDraftUser = computed({
  get: () => mqttDraft.value.user,
  set: value => { mqttDraft.value = { ...mqttDraft.value, user: value }; }
});
const mqttDraftPass = computed({
  get: () => mqttDraft.value.pass,
  set: value => { mqttDraft.value = { ...mqttDraft.value, pass: value }; }
});
const mqttDraftShowPass = computed({
  get: () => mqttDraft.value.showPass,
  set: value => { mqttDraft.value = { ...mqttDraft.value, showPass: value }; }
});

const sessionBadgeClass = computed(() => {
  if (props.displayState.state === 'active') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (props.displayState.state === 'error') return 'border-rose-500/30 bg-rose-500/10 text-rose-300';
  if (props.displayState.state === 'partial') return 'border-amber-500/30 bg-amber-500/10 text-amber-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
});

const brokerBadgeClass = computed(() => {
  if (props.displayState.mqttConnectionState === 'connected') return 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300';
  if (props.displayState.mqttConnectionState === 'error') return 'border-rose-500/30 bg-rose-500/10 text-rose-300';
  if (props.displayState.mqttConnectionState === 'connecting') return 'border-amber-500/30 bg-amber-500/10 text-amber-300';
  return 'border-slate-700 bg-slate-800/50 text-slate-400';
});

function updateTransport(event: Event) {
  form.value = {
    ...form.value,
    transport: (event.target as HTMLSelectElement).value as GatewaySessionTransport
  };
}
</script>

<template>
  <div class="glass-card shrink-0 p-3 text-left">
    <div class="mb-2 flex items-center justify-between gap-3">
      <div>
        <h2 class="text-sm font-bold text-cyan-300">Gateway Session</h2>
        <p class="mt-0.5 text-[10px] text-slate-500">Shared operational gateway for Fleet and Monitor.</p>
      </div>
    </div>

    <div class="grid grid-cols-1 items-start gap-3 md:grid-cols-2 xl:grid-cols-[11rem_minmax(18rem,1fr)_16rem_auto]">
      <div class="flex min-w-0 flex-col gap-1.5 text-xs">
        <label class="font-medium text-slate-400">Transport</label>
        <select
          :value="form.transport"
          @change="updateTransport"
          :disabled="displayState.changeDisabled"
          :title="displayState.changeDisabledReason || undefined"
          class="glass-input h-9 appearance-none disabled:opacity-50"
        >
          <option value="serial">USB Serial Gateway</option>
          <option value="mqtt">Remote MQTT Broker</option>
          <option value="local_broker">Local MQTT Broker</option>
        </select>
      </div>

      <div class="flex min-w-0 flex-col gap-1.5 text-xs">
        <label class="font-medium text-slate-400">Gateway</label>
        <select
          v-if="form.transport === 'serial'"
          v-model="selectedSerialPort"
          :disabled="displayState.changeDisabled || transportState.serialPortSelectorDisabled"
          :title="displayState.changeDisabledReason || undefined"
          class="glass-input h-9 appearance-none disabled:opacity-50"
        >
          <option value="" disabled>Select USB gateway</option>
          <option v-for="port in transportState.ports" :key="port.port_name" :value="port.port_name">
            {{ port.port_name }}{{ port.description ? ` - ${port.description}` : '' }}
          </option>
        </select>
        <div v-else-if="transportState.mqttGatewayOptions.length === 0" class="flex flex-col gap-1">
          <input
            v-model="selectedMqttManualChipId"
            @input="emit('manual-chip-input', selectedMqttManualChipId)"
            :disabled="displayState.changeDisabled"
            placeholder="Enter gateway chip ID"
            class="glass-input h-9 font-mono disabled:opacity-50"
          />
          <span v-if="transportState.manualMqttGatewayError" class="text-[10px] text-rose-300">{{ transportState.manualMqttGatewayError }}</span>
          <span v-else class="text-[10px] text-slate-500">No gateways discovered on the connected broker.</span>
        </div>
        <select
          v-else
          v-model="selectedMqttGatewayChipId"
          :disabled="displayState.changeDisabled"
          :title="displayState.changeDisabledReason || undefined"
          class="glass-input h-9 appearance-none disabled:opacity-50"
        >
          <option value="" disabled>Select MQTT gateway</option>
          <option v-for="gateway in transportState.mqttGatewayOptions" :key="gateway.chip_id" :value="gateway.chip_id">{{ gateway.label }}</option>
          <option v-if="selectedMqttGatewayChipId && !transportState.isSelectedMqttGatewayDiscovered" :value="selectedMqttGatewayChipId">
            Waiting: lrs-{{ selectedMqttGatewayChipId }}
          </option>
        </select>
      </div>

      <div class="flex min-w-0 flex-col gap-1.5 text-xs">
        <label class="font-medium text-slate-400">Gateway admin password</label>
        <div class="relative">
          <input
            v-model="adminPassword"
            :type="showAdminPassword ? 'text' : 'password'"
            class="glass-input h-9 w-full pr-10 font-mono"
            autocomplete="current-password"
          />
          <button
            type="button"
            @click="showAdminPassword = !showAdminPassword"
            class="absolute right-2 top-1 h-7 w-7 bg-transparent p-0 text-slate-400 hover:text-slate-200"
            :aria-label="showAdminPassword ? 'Hide gateway admin password' : 'Show gateway admin password'"
          >
            {{ showAdminPassword ? '◉' : '○' }}
          </button>
        </div>
      </div>

      <div class="flex min-w-0 flex-col gap-1.5 text-xs">
        <label class="font-medium text-slate-400">Status</label>
        <div class="flex min-h-9 flex-wrap items-center gap-2">
          <span v-if="form.transport !== 'serial'" :class="['inline-flex h-9 items-center rounded border px-2.5 text-[10px] font-bold', brokerBadgeClass]">
            Broker: {{ displayState.mqttConnectionState }}
          </span>
          <span :class="['inline-flex h-9 items-center rounded border px-2.5 text-[10px] font-bold', sessionBadgeClass]">
            Session: {{ displayState.label }}
          </span>
          <button
            v-if="form.transport !== 'serial'"
            @click="showConnectionSettings = !showConnectionSettings"
            class="glass-input h-9 whitespace-nowrap px-3 text-[11px] font-bold hover:bg-slate-700/70"
          >
            {{ showConnectionSettings ? 'Hide connection settings' : 'Connection settings' }}
          </button>
        </div>
      </div>
    </div>

    <p v-if="displayState.changeDisabledReason" class="mt-2 text-[10px] text-amber-300">{{ displayState.changeDisabledReason }}</p>

    <div v-if="showConnectionSettings && form.transport === 'mqtt'" class="mt-3 grid grid-cols-1 gap-3 border-t border-slate-800 pt-3 md:grid-cols-2 xl:grid-cols-5">
      <label class="flex flex-col gap-1 text-xs text-slate-400">Broker host<input v-model="mqttDraftHost" class="glass-input h-9 text-slate-200" placeholder="venus.local" /></label>
      <label class="flex flex-col gap-1 text-xs text-slate-400">Port<input v-model.number="mqttDraftPort" class="glass-input h-9 text-slate-200" type="number" min="1" max="65535" /></label>
      <label class="flex flex-col gap-1 text-xs text-slate-400">Topic root<input v-model="mqttDraftTopicRoot" class="glass-input h-9 text-slate-200" /></label>
      <label class="flex flex-col gap-1 text-xs text-slate-400">Username<input v-model="mqttDraftUser" class="glass-input h-9 text-slate-200" /></label>
      <label class="flex flex-col gap-1 text-xs text-slate-400">
        Password
        <div class="flex gap-2">
          <input v-model="mqttDraftPass" :type="mqttDraftShowPass ? 'text' : 'password'" class="glass-input h-9 min-w-0 flex-1 text-slate-200" placeholder="Blank preserves current password" />
          <button @click="mqttDraftShowPass = !mqttDraftShowPass" class="glass-input h-9 px-2 text-[10px] font-bold">{{ mqttDraftShowPass ? 'Hide' : 'Show' }}</button>
        </div>
      </label>
      <div class="flex flex-wrap items-center gap-2 md:col-span-2 xl:col-span-5">
        <button @click="emit('connect-mqtt')" :disabled="!mqttDraftHost || displayState.mqttConnectionState === 'connecting'" class="primary-btn h-8 px-3 text-xs disabled:opacity-50">
          {{ displayState.mqttConnectionState === 'connected' ? 'Save and reconnect' : displayState.mqttConnectionState === 'connecting' ? 'Connecting...' : 'Connect' }}
        </button>
        <button @click="emit('disconnect-mqtt')" :disabled="displayState.mqttConnectionState !== 'connected'" class="glass-input h-8 px-3 text-xs disabled:opacity-50">Disconnect</button>
        <span v-if="displayState.mqttError" class="text-xs text-rose-300">{{ displayState.mqttError }}</span>
      </div>
    </div>

    <div v-if="showConnectionSettings && form.transport === 'local_broker'" class="mt-3 grid grid-cols-1 gap-3 border-t border-slate-800 pt-3 md:grid-cols-[10rem_minmax(0,1fr)_auto]">
      <label class="flex flex-col gap-1 text-xs text-slate-400">Local broker port<input v-model.number="localBrokerPort" :disabled="localBrokerState.running" type="number" class="glass-input h-9 text-slate-200 disabled:opacity-50" /></label>
      <div class="flex flex-col gap-1 text-xs text-slate-400">
        <span>Gateway broker address</span>
        <div class="glass-input flex h-9 items-center overflow-x-auto whitespace-nowrap text-slate-200">{{ localBrokerState.lans.join(' / ') || 'Start the broker to discover LAN addresses' }}</div>
      </div>
      <div class="flex items-end gap-2">
        <button @click="emit('start-local-broker')" :disabled="localBrokerState.running || localBrokerState.isStarting || localBrokerState.isClientConnecting" class="primary-btn h-9 px-3 text-xs disabled:opacity-50">
          {{ localBrokerState.running ? 'Running' : localBrokerState.isStarting ? 'Starting...' : 'Start local broker' }}
        </button>
        <button @click="emit('copy-gateway-settings')" :disabled="!localBrokerState.running" class="glass-input h-9 px-3 text-xs disabled:opacity-50">Copy gateway settings</button>
      </div>
      <p v-if="localBrokerState.error" class="text-xs text-rose-300 md:col-span-3">{{ localBrokerState.error }}</p>
      <p v-else-if="localBrokerState.clientMessage" class="text-xs text-slate-400 md:col-span-3">{{ localBrokerState.clientMessage }}</p>
    </div>
  </div>
</template>
