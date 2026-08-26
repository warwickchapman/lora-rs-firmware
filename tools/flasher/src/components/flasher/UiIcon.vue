<script setup lang="ts">
defineProps<{
  kind: 'flash' | 'identify' | 'commands' | 'reboot' | 'logs' | 'forget' |
    'warning' | 'power' | 'wifi' | 'security' | 'serial' | 'mqtt' | 'lora' |
    'settings' | 'diagnostics';
}>();
</script>

<template>
  <svg
    :class="['h-4 w-4 shrink-0', { 'identify-led-icon': kind === 'identify' }]"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="2"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
  >
    <path v-if="kind === 'flash' || kind === 'power'" d="m13 2-9 12h8l-1 8 9-12h-8l1-8Z" />
    <template v-else-if="kind === 'identify'">
      <path d="M9 18h6M10 22h4" />
      <path d="M8.5 14.5a6 6 0 1 1 7 0c-.8.7-1.5 1.6-1.5 2.5h-4c0-.9-.7-1.8-1.5-2.5Z" />
      <path d="M12 2v2M4.9 4.9l1.4 1.4M2 12h2M19.1 4.9l-1.4 1.4M20 12h2" />
    </template>
    <template v-else-if="kind === 'commands'">
      <path d="M14.7 6.3a4 4 0 0 0-5-5L7.5 3.5l3 3 2.2-2.2a4 4 0 0 0 2 2Z" />
      <path d="m9.5 6.5-7 7a2.1 2.1 0 0 0 3 3l7-7M14 14l6 6M17 11l3-3" />
    </template>
    <template v-else-if="kind === 'reboot'">
      <path d="M20 6v6h-6" />
      <path d="M20 12a8 8 0 1 0-2.3 5.7" />
    </template>
    <template v-else-if="kind === 'logs'">
      <rect x="5" y="4" width="14" height="17" rx="2" />
      <path d="M9 4V2h6v2M9 9h6M9 13h6M9 17h4" />
    </template>
    <path v-else-if="kind === 'forget'" d="M3 6h18M8 6V4h8v2M19 6l-1 15H6L5 6M10 11v5M14 11v5" />
    <template v-else-if="kind === 'warning'">
      <path d="M10.3 3.6 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.6a2 2 0 0 0-3.4 0Z" />
      <path d="M12 9v4M12 17h.01" />
    </template>
    <template v-else-if="kind === 'wifi'">
      <path d="M5 12.6a10 10 0 0 1 14 0M8.5 16.1a5 5 0 0 1 7 0M12 20h.01" />
      <path d="M2 9.1a15 15 0 0 1 20 0" />
    </template>
    <template v-else-if="kind === 'security'">
      <circle cx="8" cy="15" r="4" />
      <path d="m11 12 8-8M15 8l2 2M17 6l2 2" />
    </template>
    <template v-else-if="kind === 'serial'">
      <path d="M8 3v5M16 3v5M6 8h12v3a6 6 0 0 1-12 0V8ZM12 17v4" />
    </template>
    <template v-else-if="kind === 'mqtt'">
      <circle cx="12" cy="12" r="9" />
      <path d="M3 12h18M12 3a15 15 0 0 1 0 18M12 3a15 15 0 0 0 0 18" />
    </template>
    <template v-else-if="kind === 'lora'">
      <path d="M12 20v-8M9 20h6M12 4h.01" />
      <path d="M8.5 7.5a5 5 0 0 0 0 7M15.5 7.5a5 5 0 0 1 0 7M5.5 4.5a9 9 0 0 0 0 13M18.5 4.5a9 9 0 0 1 0 13" />
    </template>
    <template v-else-if="kind === 'settings'">
      <circle cx="12" cy="12" r="3" />
      <path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1-2.8 2.8-.1-.1a1.7 1.7 0 0 0-1.9-.3 1.7 1.7 0 0 0-1 1.6v.2h-4V21a1.7 1.7 0 0 0-1-1.6 1.7 1.7 0 0 0-1.9.3l-.1.1L4.2 17l.1-.1a1.7 1.7 0 0 0 .3-1.9A1.7 1.7 0 0 0 3 14H2.8v-4H3a1.7 1.7 0 0 0 1.6-1 1.7 1.7 0 0 0-.3-1.9L4.2 7 7 4.2l.1.1a1.7 1.7 0 0 0 1.9.3A1.7 1.7 0 0 0 10 3V2.8h4V3a1.7 1.7 0 0 0 1 1.6 1.7 1.7 0 0 0 1.9-.3l.1-.1L19.8 7l-.1.1a1.7 1.7 0 0 0-.3 1.9 1.7 1.7 0 0 0 1.6 1h.2v4H21a1.7 1.7 0 0 0-1.6 1Z" />
    </template>
    <template v-else>
      <path d="M4 19V9M10 19V5M16 19v-7M22 19H2" />
    </template>
  </svg>
</template>
