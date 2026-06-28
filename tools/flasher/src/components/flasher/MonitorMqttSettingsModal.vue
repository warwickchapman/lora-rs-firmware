<script setup lang="ts">
import { computed } from 'vue';

export interface MqttDraftSettings {
  host: string;
  port: number;
  topicRoot: string;
  user: string;
  pass: string;
  showPass: boolean;
}

const show = defineModel<boolean>({ required: true });
const draft = defineModel<MqttDraftSettings>('draft', { required: true });

defineProps<{
  monitorMqttConnected: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'toggle-connection'): void;
}>();

const computedHost = computed({
  get: () => draft.value.host,
  set: (val) => { draft.value = { ...draft.value, host: val }; }
});

const computedPort = computed({
  get: () => draft.value.port,
  set: (val) => { draft.value = { ...draft.value, port: val }; }
});

const computedTopicRoot = computed({
  get: () => draft.value.topicRoot,
  set: (val) => { draft.value = { ...draft.value, topicRoot: val }; }
});

const computedUser = computed({
  get: () => draft.value.user,
  set: (val) => { draft.value = { ...draft.value, user: val }; }
});

const computedPass = computed({
  get: () => draft.value.pass,
  set: (val) => { draft.value = { ...draft.value, pass: val }; }
});

const computedShowPass = computed({
  get: () => draft.value.showPass,
  set: (val) => { draft.value = { ...draft.value, showPass: val }; }
});
</script>

<template>
  <div
    v-if="show"
    class="fixed inset-0 z-40 flex items-center justify-center bg-black/45 p-4"
    @click.self="emit('close')"
  >
    <div class="glass-card w-full max-w-2xl overflow-hidden text-left">
      <div class="flex items-center justify-between border-b border-slate-800 bg-slate-900/50 px-3 py-2">
        <div>
          <h2 class="text-sm font-bold text-cyan-300">MQTT Broker Settings</h2>
          <p class="mt-0.5 text-xs text-slate-500">Used when remote administration or monitoring transport is set to MQTT.</p>
        </div>
        <button
          @click="emit('close')"
          class="glass-input h-8 w-8 p-0 hover:bg-slate-700/70"
          title="Close MQTT settings"
          aria-label="Close MQTT settings"
        >
          ×
        </button>
      </div>

      <div class="grid grid-cols-[8rem_minmax(0,1fr)] gap-x-3 gap-y-2 p-3 text-xs">
        <label class="self-center text-right font-semibold text-slate-300">Broker host</label>
        <input v-model="computedHost" class="glass-input h-9" placeholder="venus.local" />

        <label class="self-center text-right font-semibold text-slate-300">Broker port</label>
        <input v-model.number="computedPort" class="glass-input h-9" type="number" min="1" max="65535" />

        <label class="self-center text-right font-semibold text-slate-300">Topic root</label>
        <input v-model="computedTopicRoot" class="glass-input h-9" />

        <label class="self-center text-right font-semibold text-slate-300">MQTT user</label>
        <input v-model="computedUser" class="glass-input h-9" />

        <label class="self-center text-right font-semibold text-slate-300">MQTT password</label>
        <div class="flex gap-2">
          <input v-model="computedPass" :type="computedShowPass ? 'text' : 'password'" class="glass-input h-9 min-w-0 flex-1" />
          <button @click="computedShowPass = !computedShowPass" class="glass-input h-9 w-14 hover:bg-slate-700/70 text-xs font-bold">
            {{ computedShowPass ? 'Hide' : 'Show' }}
          </button>
        </div>
      </div>

      <div class="flex items-center justify-between border-t border-slate-800 bg-slate-950/30 px-3 py-2">
        <span :class="['inline-flex h-8 items-center rounded border px-2 text-[10px] font-bold', monitorMqttConnected ? 'border-emerald-500/30 bg-emerald-500/10 text-emerald-300' : 'border-slate-700 bg-slate-800/50 text-slate-400']">
          MQTT {{ monitorMqttConnected ? 'configured' : 'not active' }}
        </span>
        <div class="flex gap-2">
          <button @click="emit('close')" class="glass-input h-8 px-3 hover:bg-slate-700/70 text-xs font-bold">Cancel</button>
          <button
            @click="emit('toggle-connection')"
            :disabled="!computedHost"
            class="primary-btn h-8 px-3 text-xs font-bold disabled:opacity-50"
          >
            {{ monitorMqttConnected ? 'Disconnect MQTT' : 'Save MQTT' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
