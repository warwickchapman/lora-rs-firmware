<script setup lang="ts">
import { computed } from 'vue';
import UiIcon from './UiIcon.vue';

export interface RemoteSettingsDevice {
  address: number | string;
  chip_id?: string;
  fw_version?: string;
  role?: string;
}

export interface RemoteSettingsDraft {
  activeTab: 'sensors' | 'power' | 'wifi' | 'security';
  wifi_ssid: string;
  wifi_password: string;
  sensor_temp_enabled: boolean;
  sensor_tank_enabled: boolean;
  power_save_listen_only: boolean;
  fleet_key: string;
  fleet_key_confirmed: boolean;
  show_fleet_key: boolean;
}

const show = defineModel<boolean>({ required: true });
const draft = defineModel<RemoteSettingsDraft>('draft', { required: true });

defineProps<{
  device: RemoteSettingsDevice;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'execute-sensors', tempEnabled: boolean, tankEnabled: boolean, powerSave: boolean): void;
  (e: 'execute-wifi', ssid: string, pass: string): void;
  (e: 'execute-fleet-key', key: string): void;
}>();

const computedActiveTab = computed({
  get: () => draft.value.activeTab,
  set: (val) => { draft.value = { ...draft.value, activeTab: val }; }
});

const computedWifiSsid = computed({
  get: () => draft.value.wifi_ssid,
  set: (val) => { draft.value = { ...draft.value, wifi_ssid: val }; }
});

const computedWifiPassword = computed({
  get: () => draft.value.wifi_password,
  set: (val) => { draft.value = { ...draft.value, wifi_password: val }; }
});

const computedSensorTempEnabled = computed({
  get: () => draft.value.sensor_temp_enabled,
  set: (val) => { draft.value = { ...draft.value, sensor_temp_enabled: val }; }
});

const computedSensorTankEnabled = computed({
  get: () => draft.value.sensor_tank_enabled,
  set: (val) => { draft.value = { ...draft.value, sensor_tank_enabled: val }; }
});

const computedPowerSave = computed({
  get: () => draft.value.power_save_listen_only,
  set: (val) => { draft.value = { ...draft.value, power_save_listen_only: val }; }
});

const computedFleetKey = computed({
  get: () => draft.value.fleet_key,
  set: (val) => { draft.value = { ...draft.value, fleet_key: val }; }
});

const computedFleetKeyConfirmed = computed({
  get: () => draft.value.fleet_key_confirmed,
  set: (val) => { draft.value = { ...draft.value, fleet_key_confirmed: val }; }
});

const computedShowFleetKey = computed({
  get: () => draft.value.show_fleet_key,
  set: (val) => { draft.value = { ...draft.value, show_fleet_key: val }; }
});
</script>

<template>
  <Transition name="toast">
    <div v-if="show" class="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 px-4">
      <div class="w-full max-w-lg rounded-lg border border-slate-700 bg-slate-900 p-5 shadow-2xl flex flex-col gap-4">
        <!-- Header with device info -->
        <div>
          <h3 class="flex items-center gap-2 text-base font-bold text-slate-200">
            <UiIcon kind="settings" />
            Remote Settings — Device {{ device.address }}
          </h3>
          <p class="mt-1 text-xs text-slate-500">
            <span v-if="device.chip_id">{{ device.chip_id }}</span>
            <span v-if="device.fw_version"> · v{{ device.fw_version }}</span>
            <span v-if="device.role"> · {{ device.role }}</span>
          </p>
        </div>

        <!-- Tab bar -->
        <div class="flex gap-0 border-b border-slate-800">
          <button
            v-for="tab in ([{key:'sensors',label:'Sensors',icon:'commands'},{key:'power',label:'Power',icon:'power'},{key:'wifi',label:'WiFi',icon:'wifi'},{key:'security',label:'Security',icon:'security'}] as const)"
            :key="tab.key"
            @click="computedActiveTab = tab.key"
            class="flex items-center gap-1.5 px-4 py-2 text-[11px] font-bold transition-colors"
            :class="computedActiveTab === tab.key
              ? 'text-cyan-300 border-b-2 border-cyan-400 -mb-px'
              : 'text-slate-500 hover:text-slate-300'"
          >
            <UiIcon :kind="tab.icon" />
            {{ tab.label }}
          </button>
        </div>

        <!-- Sensors tab content -->
        <div v-if="computedActiveTab === 'sensors'" class="flex flex-col gap-3 text-left">
          <p class="text-xs text-slate-500">Enable or disable hardware sensors over LoRa. Changes persist to remote device flash memory.</p>
          <div class="flex flex-col gap-3 py-1">
            <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-3 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
              <input v-model="computedSensorTempEnabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
              <div>
                <div class="font-semibold text-slate-200">DS18B20 Temperature Sensor</div>
                <div class="text-[10px] text-slate-500 mt-0.5">Enables digital temperature probes on the device.</div>
              </div>
            </label>
            <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-3 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
              <input v-model="computedSensorTankEnabled" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-500 focus:ring-0 focus:ring-offset-0" />
              <div>
                <div class="font-semibold text-slate-200">4-20mA Pressure Tank Level Sensor</div>
                <div class="text-[10px] text-slate-500 mt-0.5">Enables analog pressure sensor mappings for tank level tracking.</div>
              </div>
            </label>
          </div>
          <div class="mt-2 flex justify-end gap-2">
            <button @click="emit('close')" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
            <button
              @click="emit('execute-sensors', computedSensorTempEnabled, computedSensorTankEnabled, computedPowerSave)"
              class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold transition-colors"
            >
              Apply Sensor Configuration
            </button>
          </div>
        </div>

        <!-- Power tab content (Stateless Commands Console) -->
        <div v-if="computedActiveTab === 'power'" class="flex flex-col gap-3 text-left">
          <p class="text-xs text-slate-500">PowerSave turns off WiFi, Serial Admin, OTA, MQTT, UDP logging, LEDs, and background services. LoRa command handling remains active so the device can be returned to Full Power remotely.</p>
          
          <div class="flex flex-col gap-3 py-1">
            <div class="flex flex-col md:flex-row md:items-center justify-between gap-3 border border-slate-800 bg-slate-950/20 rounded p-4">
              <div class="flex-1">
                <div class="font-semibold text-slate-200 text-xs flex items-center gap-1.5">
                  <span :class="['w-2 h-2 rounded-full', computedPowerSave ? 'bg-cyan-400 animate-pulse' : 'bg-emerald-500']"></span>
                  {{ computedPowerSave ? 'Power Save Mode Enabled' : 'Full Power Mode Active' }}
                </div>
                <div class="text-[10px] text-slate-400 mt-2 select-text leading-relaxed">
                  <template v-if="computedPowerSave">
                    The device is configured for deep power saving. Local Wi-Fi, Serial Admin, and background services are completely shut down to preserve battery life. LoRa receiver remains active.
                  </template>
                  <template v-else>
                    Keeps the remote device continuously awake. WiFi, Serial Admin, OTA, and active sensor polling remain fully operational at all times.
                  </template>
                </div>
              </div>
              <button
                v-if="computedPowerSave"
                @click="emit('execute-sensors', computedSensorTempEnabled, computedSensorTankEnabled, false)"
                class="m-0 h-9 self-center rounded-md border border-emerald-500/40 bg-emerald-500/10 text-emerald-300 hover:bg-emerald-500/25 px-4 text-xs font-bold transition-colors whitespace-nowrap"
              >
                Disable Power Save
              </button>
              <button
                v-else
                @click="emit('execute-sensors', computedSensorTempEnabled, computedSensorTankEnabled, true)"
                class="m-0 h-9 self-center rounded-md border border-cyan-500/40 bg-cyan-500/10 text-cyan-300 hover:bg-cyan-500/25 px-4 text-xs font-bold transition-colors whitespace-nowrap"
              >
                Enable Power Save
              </button>
            </div>
          </div>

          <div class="mt-2 flex justify-end">
            <button @click="emit('close')" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Close Console</button>
          </div>
        </div>

        <!-- WiFi tab content -->
        <div v-if="computedActiveTab === 'wifi'" class="flex flex-col gap-3 text-left">
          <p class="text-xs text-slate-500">Securely transmit targeted WiFi credentials over LoRa.</p>
          <div class="flex flex-col gap-1">
            <label class="text-[11px] font-semibold text-slate-400">SSID</label>
            <input v-model="computedWifiSsid" type="text" class="glass-input h-9 px-3 text-xs w-full" placeholder="WiFi Network Name" />
          </div>
          <div class="flex flex-col gap-1">
            <label class="text-[11px] font-semibold text-slate-400">STA Password</label>
            <input v-model="computedWifiPassword" type="password" class="glass-input h-9 px-3 text-xs w-full" placeholder="Leave blank to clear credentials" />
          </div>
          <div class="mt-2 flex justify-end gap-2">
            <button @click="emit('close')" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
            <button
              @click="emit('execute-wifi', computedWifiSsid, computedWifiPassword)"
              class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold transition-colors"
            >
              Send Credentials
            </button>
          </div>
        </div>

        <!-- Security tab content -->
        <div v-if="computedActiveTab === 'security'" class="flex flex-col gap-3 text-left">
          <p class="text-xs text-slate-500">Update the device's shared fleet passphrase over LoRa.</p>
          <div class="rounded-lg border border-rose-500/30 bg-rose-500/10 p-3 text-xs text-rose-300 flex flex-col gap-1.5 select-text leading-relaxed">
            <div class="flex items-center gap-1.5 font-bold">
              <UiIcon kind="warning" />
              CRITICAL OPERATIONAL WARNING
            </div>
            <div>
              Changing the remote's Fleet Key will make it <span class="font-bold text-rose-200">immediately unreachable</span> by this Gateway once the remote reboots.
              You must update this Gateway's Fleet Key to match, or the remote device will be permanently orphaned until manually retrieved!
            </div>
          </div>
          <div class="flex flex-col gap-3.5 py-1">
            <label class="flex items-center gap-3 text-xs text-slate-300 cursor-pointer select-none">
              <input v-model="computedFleetKeyConfirmed" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-cyan-600 focus:ring-0 focus:ring-offset-0" />
              <span class="font-semibold text-slate-200">I understand that the device will become unreachable until Gateway keys are matched.</span>
            </label>
            <div class="flex flex-col gap-1.5 text-xs">
              <label class="font-semibold text-slate-300">New Fleet Passphrase</label>
              <div class="flex gap-2">
                <input
                  v-model="computedFleetKey"
                  :type="computedShowFleetKey ? 'text' : 'password'"
                  class="glass-input h-9 flex-1"
                  placeholder="Minimum 8 characters"
                  :disabled="!computedFleetKeyConfirmed"
                />
                <button
                  @click="computedShowFleetKey = !computedShowFleetKey"
                  class="glass-input h-9 px-3 hover:bg-slate-700/70"
                  type="button"
                  :disabled="!computedFleetKeyConfirmed"
                >
                  {{ computedShowFleetKey ? 'Hide' : 'Show' }}
                </button>
              </div>
            </div>
          </div>
          <div class="mt-2 flex justify-end gap-2">
            <button @click="emit('close')" class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
            <button
              @click="emit('execute-fleet-key', computedFleetKey)"
              :disabled="!computedFleetKeyConfirmed || !computedFleetKey || computedFleetKey.length < 8"
              class="m-0 h-9 rounded-md border border-cyan-500/40 bg-cyan-500/20 text-cyan-100 hover:bg-cyan-500/30 px-4 text-xs font-bold disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
            >
              Update Key over LoRa
            </button>
          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>
