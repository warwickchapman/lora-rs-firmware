#pragma once

#include <IPAddress.h>
#include <stdint.h>
#include <stddef.h>
#include "fixed_setting_string.h"

class PendingCommandManager {
public:
  PendingCommandManager();

  // Reset semantics
  void resetAll();
  void resetOnConfigApply();

  // WiFi Control
  void requestWifiControl(bool enabled, uint32_t counter, uint8_t src);
  bool hasPendingWifiControl() const { return wifi_control_pending_; }
  bool consumeWifiControl(bool &enabled, uint8_t &src, uint32_t &counter);
  void clearWifiControl();

  // UDP Log Control
  void requestUdpLogControl(bool enabled, IPAddress host, uint16_t port, uint32_t ttlS, uint8_t src);
  bool hasPendingUdpLogControl() const { return udp_log_control_pending_; }
  bool consumeUdpLogControl(bool &enabled, IPAddress &host, uint16_t &port, uint32_t &ttlS, uint8_t &src);
  void clearUdpLogControl();

  // OTA Pull
  void requestOtaPull(IPAddress host, uint16_t port, const char* sha256Hex, uint8_t src);
  bool hasPendingOtaPull() const { return ota_pull_pending_; }
  bool consumeOtaPull(IPAddress &host, uint16_t &port, char *sha256HexDest, size_t destSize, uint8_t &src);
  void clearOtaPull();

  // WiFi Provision
  void requestWifiProvision(const char* ssid, const char* password, uint8_t src);
  bool hasPendingWifiProvision() const { return wifi_prov_pending_; }
  bool consumeWifiProvision(char *ssidDest, size_t ssidSize, char *passwordDest, size_t passwordSize, uint8_t &src);
  void clearWifiProvision();

  // Factory Reset
  void requestFactoryReset(bool keepFleet, bool keepWifi, uint8_t src);
  bool hasPendingFactoryReset() const { return factory_reset_pending_; }
  bool consumeFactoryReset(bool &keepFleet, bool &keepWifi, uint8_t &src);
  void clearFactoryReset();

  // Reboot
  void requestReboot();
  bool hasPendingReboot() const { return reboot_pending_; }
  bool consumeReboot();
  void clearReboot();

  // Readdress
  void requestReaddress(uint8_t newAddress, uint8_t gwAddr);
  bool hasPendingReaddress() const { return readdress_pending_; }
  bool consumeReaddress(uint8_t &newAddress, uint8_t &gwAddr);
  void clearReaddress();

  // Peer Sync
  void requestPeerSync(uint32_t chipId, uint8_t address);
  bool hasPendingPeerSync() const { return peer_sync_pending_; }
  bool consumePeerSync(uint32_t &chipId, uint8_t &address);
  void clearPeerSync();

  // Sensor Config
  void requestSensorConfig(bool tempEnabled, bool tankEnabled, bool powerSaveEnabled, bool powerSaveBootGrace);
  bool hasPendingSensorConfig() const { return sensor_config_pending_; }
  bool consumeSensorConfig(bool &tempEnabled, bool &tankEnabled, bool &powerSaveEnabled, bool &powerSaveBootGrace);
  void clearSensorConfig();

  // Fleet Prov Apply
  void requestFleetProvApply(uint16_t sessionNonce, uint8_t newAddress, bool roleTx, uint8_t controllerAddress, const char* key);
  bool hasPendingFleetProvApply() const { return fleet_prov_apply_pending_; }
  bool consumeFleetProvApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx, uint8_t &controllerAddress, char* keyDest, size_t keySize);
  void clearFleetProvApply();

  // Fleet Key Change
  void requestFleetKeyChange(const char* key, uint8_t src);
  bool hasPendingFleetKeyChange() const { return fleet_key_pending_; }
  bool consumeFleetKeyChange(char *keyDest, size_t keySize, uint8_t &src);
  void clearFleetKeyChange();

private:
  bool wifi_control_pending_ = false;
  bool wifi_control_pending_enabled_ = true;
  uint8_t wifi_control_pending_src_ = 0;
  uint32_t wifi_control_pending_counter_ = 0;

  bool udp_log_control_pending_ = false;
  bool udp_log_control_pending_enabled_ = false;
  IPAddress udp_log_control_pending_host_;
  uint16_t udp_log_control_pending_port_ = 0;
  uint32_t udp_log_control_pending_ttl_s_ = 0;
  uint8_t udp_log_control_pending_src_ = 0;

  bool ota_pull_pending_ = false;
  IPAddress ota_pull_pending_host_;
  uint16_t ota_pull_pending_port_ = 0;
  FixedSettingString<65> ota_pull_pending_sha256_;
  uint8_t ota_pull_pending_src_ = 0;

  bool wifi_prov_pending_ = false;
  FixedSettingString<33> wifi_prov_pending_ssid_;
  FixedSettingString<65> wifi_prov_pending_password_;
  uint8_t wifi_prov_pending_src_ = 0;

  bool factory_reset_pending_ = false;
  bool factory_reset_keep_fleet_pending_ = true;
  bool factory_reset_keep_wifi_pending_ = false;
  uint8_t factory_reset_pending_src_ = 0;

  bool reboot_pending_ = false;

  bool readdress_pending_ = false;
  uint8_t readdress_pending_new_address_ = 0;
  uint8_t readdress_pending_gw_addr_ = 0;

  bool peer_sync_pending_ = false;
  uint32_t peer_sync_chip_id_ = 0;
  uint8_t peer_sync_address_ = 0;

  bool sensor_config_pending_ = false;
  bool sensor_config_temp_enabled_ = false;
  bool sensor_config_tank_enabled_ = false;
  bool sensor_config_power_save_enabled_ = false;
  bool sensor_config_power_save_boot_grace_ = true;

  bool fleet_prov_apply_pending_ = false;
  uint16_t fleet_prov_apply_session_nonce_ = 0;
  uint8_t fleet_prov_apply_address_ = 0;
  bool fleet_prov_apply_role_tx_ = false;
  uint8_t fleet_prov_apply_controller_address_ = 0;
  FixedSettingString<65> fleet_prov_apply_key_;

  bool fleet_key_pending_ = false;
  FixedSettingString<65> fleet_key_pending_key_;
  uint8_t fleet_key_pending_src_ = 0;
};
