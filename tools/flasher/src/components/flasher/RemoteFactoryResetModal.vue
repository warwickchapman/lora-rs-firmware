<script setup lang="ts">
import { computed } from 'vue';

export interface RemoteFactoryResetDraft {
  keep_shared_fleet_key: boolean;
  keep_wifi_credentials: boolean;
}

const isOpen = defineModel<boolean>({ required: true });
const draft = defineModel<RemoteFactoryResetDraft>('draft', { required: true });

defineProps<{
  address: number | string;
}>();

const emit = defineEmits<{
  (e: 'cancel'): void;
  (e: 'confirm'): void;
}>();

// Computed bridges for checkbox v-models to avoid direct mutation of the parent model object
const computedKeepSharedFleetKey = computed({
  get: () => draft.value.keep_shared_fleet_key,
  set: (val) => { draft.value = { ...draft.value, keep_shared_fleet_key: val }; }
});

const computedKeepWifiCredentials = computed({
  get: () => draft.value.keep_wifi_credentials,
  set: (val) => { draft.value = { ...draft.value, keep_wifi_credentials: val }; }
});
</script>

<template>
  <Transition name="toast">
    <div v-if="isOpen" class="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 px-4">
      <div class="w-full max-w-md rounded-lg border border-slate-700 bg-slate-900 p-5 shadow-2xl flex flex-col gap-4">
        <div>
          <h3 class="text-base font-bold text-rose-400">⚠️ Factory Reset Remote Device {{ address }}</h3>
          <p class="mt-1 text-xs text-slate-500">Decommissions the remote device over LoRa, formatting its state and triggering a reboot.</p>
        </div>

        <div class="rounded border border-amber-500/20 bg-amber-500/5 p-3 text-xs text-amber-200 leading-relaxed">
          💡 Select which parts of the remote configuration to preserve during reset. Checking "Reset but keep in fleet" preserves pairing encryption keys so it can reconnect to this fleet. A full reset clears the remote's fleet key, but retains its gateway record so you can retry or recover it if the LoRa command is missed. Remove the record separately only after you have confirmed the reset.
        </div>

        <div class="flex flex-col gap-3 py-1">
          <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-2.5 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
            <input v-model="computedKeepSharedFleetKey" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-rose-500 focus:ring-0 focus:ring-offset-0" />
            <div>
              <div class="font-semibold text-slate-200">Reset but keep in fleet</div>
              <div class="text-[10px] text-slate-500 mt-0.5">Preserves pairing encryption keys to stay in this gateway's secure fleet.</div>
            </div>
          </label>
          <label class="flex items-center gap-3 text-xs text-slate-200 border border-slate-800/80 bg-slate-950/20 rounded p-2.5 cursor-pointer hover:bg-slate-800/20 transition-colors select-none">
            <input v-model="computedKeepWifiCredentials" type="checkbox" class="w-4 h-4 rounded border-slate-700 bg-slate-900 text-rose-500 focus:ring-0 focus:ring-offset-0" />
            <div>
              <div class="font-semibold text-slate-200">Keep WiFi Credentials</div>
              <div class="text-[10px] text-slate-500 mt-0.5">Preserves local WiFi SSID and password settings.</div>
            </div>
          </label>
        </div>

        <div class="mt-2 flex justify-end gap-2">
          <button
            @click="emit('cancel')"
            class="glass-input m-0 h-9 px-4 hover:bg-slate-700/70 text-xs font-bold"
          >
            Cancel
          </button>
          <button
            @click="emit('confirm')"
            class="m-0 h-9 rounded-md border border-rose-500/40 bg-rose-500/20 text-rose-100 hover:bg-rose-500/30 px-4 text-xs font-bold transition-colors"
          >
            Factory Reset Device
          </button>
        </div>
      </div>
    </div>
  </Transition>
</template>
