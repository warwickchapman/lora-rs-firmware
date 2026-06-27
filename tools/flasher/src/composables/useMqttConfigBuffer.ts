import { ref } from 'vue';

export interface DeviceMqttConfig {
  buffer: Record<string, any>;
  complete: boolean;
  secretsMetadata: Record<string, boolean>;
}

export function canonicalChipId(raw: string | undefined | null): string {
  return String(raw || '')
    .trim()
    .replace(/^0x/i, '')
    .replace(/[^0-9a-f]/gi, '')
    .toLowerCase();
}

export function useMqttConfigBuffer() {
  const mqttConfigBuffers = ref<Record<string, DeviceMqttConfig>>({});

  function handleConfigUpdate(
    chip_id: string,
    field: string,
    value: string,
    onComplete?: (chipId: string, config: Record<string, any>) => void
  ) {
    const canonical = canonicalChipId(chip_id);
    if (!mqttConfigBuffers.value[canonical]) {
      mqttConfigBuffers.value[canonical] = { buffer: {}, complete: false, secretsMetadata: {} };
    }
    const deviceConfig = mqttConfigBuffers.value[canonical];

    if (field === '_complete') {
      const isComplete = value === 'true' || value === '1';
      if (!isComplete) {
        deviceConfig.buffer = {};
        deviceConfig.secretsMetadata = {};
        deviceConfig.complete = false;
      } else {
        deviceConfig.complete = true;
        if (onComplete) {
          onComplete(chip_id, { ...deviceConfig.buffer });
        }
      }
    } else if (field.endsWith('_set')) {
      const secretName = field.substring(0, field.length - 4);
      deviceConfig.secretsMetadata[secretName] = value === 'true' || value === '1';
    } else if (field === 'fleet_passphrase_default') {
      deviceConfig.secretsMetadata['fleet_passphrase_default'] = value === 'true' || value === '1';
    } else {
      let parsedValue: any = value;
      if (value === 'true') parsedValue = true;
      else if (value === 'false') parsedValue = false;
      else if (!isNaN(Number(value)) && value.trim() !== '') {
        parsedValue = Number(value);
      } else if (value.startsWith('[') && value.endsWith(']')) {
        try {
          parsedValue = JSON.parse(value);
        } catch (e) {
          // ignore
        }
      }
      deviceConfig.buffer[field] = parsedValue;
    }
  }

  return {
    mqttConfigBuffers,
    handleConfigUpdate
  };
}
