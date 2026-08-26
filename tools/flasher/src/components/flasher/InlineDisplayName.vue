<script setup lang="ts">
import { nextTick, ref, watch } from 'vue';
import { normalizeDisplayName } from '../../composables/useFleetDisplayNames';

const props = defineProps<{
  chipId: string;
  displayName: string;
  saving: boolean;
  loading: boolean;
  unavailable: boolean;
}>();

const emit = defineEmits<{
  (event: 'save', chipId: string, displayName: string): void;
}>();

const editing = ref(false);
const draft = ref('');
const error = ref('');
const input = ref<HTMLInputElement | null>(null);

watch(() => props.chipId, () => {
  editing.value = false;
  draft.value = '';
  error.value = '';
});

function beginEdit() {
  if (!props.chipId || props.saving || props.loading || props.unavailable) return;
  draft.value = props.displayName;
  error.value = '';
  editing.value = true;
  void nextTick(() => input.value?.focus());
}

function cancel() {
  editing.value = false;
  draft.value = props.displayName;
  error.value = '';
}

function commit() {
  if (!editing.value) return;
  try {
    const normalized = normalizeDisplayName(draft.value);
    if (normalized !== props.displayName) emit('save', props.chipId, normalized);
    editing.value = false;
    error.value = '';
  } catch (reason) {
    error.value = String(reason instanceof Error ? reason.message : reason);
    void nextTick(() => input.value?.focus());
  }
}
</script>

<template>
  <div class="min-w-32" @click.stop @mousedown.stop @keydown.stop>
    <input
      v-if="editing"
      ref="input"
      v-model="draft"
      maxlength="16"
      aria-label="Display name"
      :title="error || 'Enter saves · Escape cancels · blank clears'"
      :class="['glass-input m-0 h-7 w-36 px-2 py-1 text-xs', error ? 'border-rose-500/70' : '']"
      @blur="commit"
      @keydown.enter.prevent="commit"
      @keydown.esc.prevent="cancel"
    />
    <span v-else-if="loading" class="text-slate-500 animate-pulse">Loading…</span>
    <span v-else-if="saving" class="text-slate-500 animate-pulse">Saving…</span>
    <span v-else-if="unavailable" class="text-slate-600" title="Reconnect the gateway to retry loading this name">Unavailable</span>
    <button
      v-else
      type="button"
      class="m-0 min-h-7 rounded px-1 text-left text-slate-300 hover:bg-white/5"
      title="Click to edit name"
      @click.stop="beginEdit"
    >
      <span v-if="displayName">{{ displayName }}</span>
      <span v-else class="text-slate-600">— <span class="text-[9px]">Click to name</span></span>
    </button>
  </div>
</template>
