#!/usr/bin/env python3
import json
import sys

def test_payloads():
    # Worst-case config block
    cfg = {
        "schema_version": 2,
        "commissioned": True,
        "mode": "transmitter",
        "role": "transmitter",
        "role_tx": True,
        "local_address": 254,
        "remote_address": 1,
        "paired_target_addresses": [1]*12,
        "allowed_controller_addresses": [1]*12,
        "known_peer_addresses": [1]*12,
        "lora_frequency_hz": 868100000,
        "lora_tx_power": 20,
        "lora_spreading_factor": 7,
        "lora_bandwidth_hz": 125000,
        "lora_coding_rate": 5,
        "heartbeat_ms": 10000,
        "heartbeat_enabled": True,
        "ack_timeout_ms": 1000,
        "mqtt_remote_retry_timeout_ms": 180000,
        "tx_mqtt_remote_polling_enabled": True,
        "tx_mqtt_remote_default_poll_interval_ms": 300000,
        "maintenance_debug_telemetry_enabled": False,
        "rx_push_on_change_enabled": True,
        "rx_push_min_interval_ms": 60000,
        "input_control_paired_lora_enabled": True,
        "tx_command_retry_timeout_ms": 180000,
        "rx_failsafe_mode": "hold_last",
        "rx_failsafe_timeout_ms": 180000,
        "wifi_sta_ssid": "A"*32,
        "wifi_sta_password": "A"*64,
        "wifi_sta_password_set": True,
        "lan_hostname": "A"*64,
        "computed_lan_hostname": "A"*64,
        "ap_always_on": False,
        "wifi_phy_mode": "11b",
        "wifi_tx_power_dbm": 20.5,
        "wifi_sleep_enabled": False,
        "wifi_static_ip_enabled": False,
        "wifi_static_ip": "255.255.255.255",
        "wifi_static_gateway": "255.255.255.255",
        "wifi_static_subnet": "255.255.255.255",
        "wifi_channel_override": 11,
        "wifi_ap_fallback_policy": "fallback_on_disconnect",
        "wifi_admin_enabled": True,
        "power_save_listen_only": False,
        "mqtt_client_enabled": True,
        "mqtt_control_enabled": True,
        "mqtt_controller_addresses": "1,2,3,4,5,6,7,8,9,10",
        "mqtt_host": "255.255.255.255",
        "mqtt_port": 1883,
        "mqtt_user": "A"*32,
        "mqtt_password": "A"*64,
        "mqtt_topic_root": "A"*64,
        "allow_mqtt_secret_export": True,
        "sensor_temp_enabled": True,
        "sensor_temp_pin": 12,
        "sensor_temp_interval_s": 10,
        "sensor_tank_enabled": True,
        "sensor_tank_range_mm": 5000,
        "sensor_tank_vref_mv": 3300,
        "sensor_tank_sense_ohms": 120,
        "sensor_tank_interval_s": 5,
        "fleet_passphrase": "A"*64,
        "fleet_passphrase_set": True,
        "fleet_passphrase_default": False,
        "fleet_setup_prompt_dismissed": True,
        "admin_password": "A"*32,
        "admin_password_set": True,
        "factory_serial": "A"*32
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

    print(f"Serialized set_config (inbound) size: {set_config_sz} bytes")
    print(f"Serialized get_config (outbound) size: {get_config_sz} bytes")

    failed = False
    for name, size in [("set_config", set_config_sz), ("get_config", get_config_sz)]:
        if size > 2944:
            print(f"FAIL: {name} exceeds contract ceiling of 2944 bytes! (Actual: {size})", file=sys.stderr)
            failed = True
        elif size > 2700:
            print(f"WARNING: {name} exceeds early threshold of 2700 bytes! (Actual: {size})", file=sys.stderr)

    if failed:
        sys.exit(1)
    else:
        print("PASS: Both payloads are within the contract limits.")

if __name__ == "__main__":
    test_payloads()
