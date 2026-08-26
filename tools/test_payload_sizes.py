#!/usr/bin/env python3
import json
import sys

def test_payloads():
    # Worst-case config block
    # Compact operational config block (MQTT contract)
    cfg = {
        "schema_version": 2,
        "commissioned": True,
        "mode": "transmitter",
        "role": "transmitter",
        "role_tx": True,
        "local_address": 254,
        "remote_address": 1,
        "lora_frequency_hz": 868100000,
        "lora_tx_power": 20,
        "lora_spreading_factor": 7,
        "lora_bandwidth_hz": 125000,
        "lora_coding_rate": 5,
        "wifi_sta_ssid": "A"*32,
        "wifi_admin_enabled": True,
        "mqtt_client_enabled": True,
        "mqtt_control_enabled": True,
        "mqtt_host": "255.255.255.255",
        "mqtt_port": 1883,
        "mqtt_topic_root": "A"*64,
        "sensor_temp_enabled": True,
        "sensor_tank_enabled": True
    }

    # set_config inbound packet representation
    set_config_envelope = {
        "cmd": "set_config",
        "id": "req-abcdefghi",
        "admin_password": "A"*32,
        "ts": 1717171717,
        "ttl_ms": 15000,
        "config": cfg
    }

    # get_config outbound packet representation
    get_config_envelope = {
        "ok": True,
        "cmd": "get_config",
        "id": "req-abcdefghi",
        "config": cfg
    }

    set_config_sz = len(json.dumps(set_config_envelope))
    get_config_sz = len(json.dumps(get_config_envelope))

    display_name_request = {
        "cmd": "set_display_name",
        "id": "req-abcdefghi",
        "flasher_compat_revision": 3,
        "admin_password": "A"*32,
        "session_id": 4294967295,
        "seq": 4294967295,
        "ts": 1717171717,
        "ttl_ms": 15000,
        "chip_id": "ffffffff",
        "display_name": "1234567890123456",
        "mac": "f"*64,
    }
    display_name_response = {
        "ok": True,
        "cmd": "set_display_name",
        "id": "req-abcdefghi",
        "chip_id": "ffffffff",
        "display_name": "1234567890123456",
    }
    display_name_request_sz = len(json.dumps(display_name_request))
    display_name_response_sz = len(json.dumps(display_name_response))

    print(f"Serialized set_config (inbound) size: {set_config_sz} bytes")
    print(f"Serialized get_config (outbound) size: {get_config_sz} bytes")
    print(f"Serialized set_display_name (authenticated inbound) size: {display_name_request_sz} bytes")
    print(f"Serialized display-name response (outbound) size: {display_name_response_sz} bytes")

    failed = False
    for name, size in [
        ("set_config", set_config_sz),
        ("get_config", get_config_sz),
        ("set_display_name", display_name_request_sz),
        ("display_name_response", display_name_response_sz),
    ]:
        if size > 1024:
            print(f"FAIL: {name} exceeds contract ceiling of 1024 bytes! (Actual: {size})", file=sys.stderr)
            failed = True
        elif size > 950:
            print(f"WARNING: {name} exceeds early threshold of 950 bytes! (Actual: {size})", file=sys.stderr)

    if failed:
        sys.exit(1)
    else:
        print("PASS: All payloads are within the contract limits.")

if __name__ == "__main__":
    test_payloads()
