#include "pending_command_manager.h"
#include <string.h>

PendingCommandManager::PendingCommandManager() {
  resetAll();
}

void PendingCommandManager::resetAll() {
  clearWifiControl();
  clearUdpLogControl();
  clearOtaPull();
  clearWifiProvision();
  clearFactoryReset();
  clearReboot();
  clearReaddress();
  clearPeerSync();
  clearSensorConfig();
  clearFleetProvApply();
  clearFleetKeyChange();
}

void PendingCommandManager::resetOnConfigApply() {
  // Clears only WiFi provision, UDP log, OTA pull, factory reset, and fleet provision apply structures.
  clearWifiProvision();
  clearUdpLogControl();
  clearOtaPull();
  clearFactoryReset();
  clearFleetProvApply();
}

// WiFi Control
void PendingCommandManager::requestWifiControl(bool enabled, uint32_t counter, uint8_t src) {
  wifi_control_pending_enabled_ = enabled;
  wifi_control_pending_counter_ = counter;
  wifi_control_pending_src_ = src;
  wifi_control_pending_ = true;
}

bool PendingCommandManager::consumeWifiControl(bool &enabled, uint8_t &src, uint32_t &counter) {
  if (!wifi_control_pending_) return false;
  enabled = wifi_control_pending_enabled_;
  src = wifi_control_pending_src_;
  counter = wifi_control_pending_counter_;
  clearWifiControl();
  return true;
}

void PendingCommandManager::clearWifiControl() {
  wifi_control_pending_ = false;
  wifi_control_pending_enabled_ = true;
  wifi_control_pending_src_ = 0;
  wifi_control_pending_counter_ = 0;
}

// UDP Log Control
void PendingCommandManager::requestUdpLogControl(bool enabled, IPAddress host, uint16_t port, uint32_t ttlS, uint8_t src) {
  udp_log_control_pending_enabled_ = enabled;
  udp_log_control_pending_host_ = host;
  udp_log_control_pending_port_ = port;
  udp_log_control_pending_ttl_s_ = ttlS;
  udp_log_control_pending_src_ = src;
  udp_log_control_pending_ = true;
}

bool PendingCommandManager::consumeUdpLogControl(bool &enabled, IPAddress &host, uint16_t &port, uint32_t &ttlS, uint8_t &src) {
  if (!udp_log_control_pending_) return false;
  enabled = udp_log_control_pending_enabled_;
  host = udp_log_control_pending_host_;
  port = udp_log_control_pending_port_;
  ttlS = udp_log_control_pending_ttl_s_;
  src = udp_log_control_pending_src_;
  clearUdpLogControl();
  return true;
}

void PendingCommandManager::clearUdpLogControl() {
  udp_log_control_pending_ = false;
  udp_log_control_pending_enabled_ = false;
  udp_log_control_pending_host_ = IPAddress();
  udp_log_control_pending_port_ = 0;
  udp_log_control_pending_ttl_s_ = 0;
  udp_log_control_pending_src_ = 0;
}

// OTA Pull
void PendingCommandManager::requestOtaPull(IPAddress host, uint16_t port, const char* sha256Hex, uint8_t src) {
  ota_pull_pending_host_ = host;
  ota_pull_pending_port_ = port;
  if (sha256Hex != nullptr) {
    ota_pull_pending_sha256_ = sha256Hex;
  } else {
    ota_pull_pending_sha256_ = "";
  }
  ota_pull_pending_src_ = src;
  ota_pull_pending_ = true;
}

bool PendingCommandManager::consumeOtaPull(IPAddress &host, uint16_t &port, char *sha256HexDest, size_t destSize, uint8_t &src) {
  if (!ota_pull_pending_) return false;
  host = ota_pull_pending_host_;
  port = ota_pull_pending_port_;
  if (sha256HexDest != nullptr && destSize > 0) {
    size_t len = ota_pull_pending_sha256_.length();
    size_t copyLen = len < (destSize - 1) ? len : (destSize - 1);
    memcpy(sha256HexDest, ota_pull_pending_sha256_.c_str(), copyLen);
    sha256HexDest[copyLen] = '\0';
  }
  src = ota_pull_pending_src_;
  clearOtaPull();
  return true;
}

void PendingCommandManager::clearOtaPull() {
  ota_pull_pending_ = false;
  ota_pull_pending_host_ = IPAddress();
  ota_pull_pending_port_ = 0;
  ota_pull_pending_sha256_ = "";
  ota_pull_pending_src_ = 0;
}

// WiFi Provision
void PendingCommandManager::requestWifiProvision(const char* ssid, const char* password, uint8_t src) {
  if (ssid != nullptr) {
    wifi_prov_pending_ssid_ = ssid;
  } else {
    wifi_prov_pending_ssid_ = "";
  }
  if (password != nullptr) {
    wifi_prov_pending_password_ = password;
  } else {
    wifi_prov_pending_password_ = "";
  }
  wifi_prov_pending_src_ = src;
  // Valid request only if SSID is not empty
  wifi_prov_pending_ = (wifi_prov_pending_ssid_.length() > 0);
}

bool PendingCommandManager::consumeWifiProvision(char *ssidDest, size_t ssidSize, char *passwordDest, size_t passwordSize, uint8_t &src) {
  if (!wifi_prov_pending_) return false;
  if (ssidDest != nullptr && ssidSize > 0) {
    size_t len = wifi_prov_pending_ssid_.length();
    size_t copyLen = len < (ssidSize - 1) ? len : (ssidSize - 1);
    memcpy(ssidDest, wifi_prov_pending_ssid_.c_str(), copyLen);
    ssidDest[copyLen] = '\0';
  }
  if (passwordDest != nullptr && passwordSize > 0) {
    size_t len = wifi_prov_pending_password_.length();
    size_t copyLen = len < (passwordSize - 1) ? len : (passwordSize - 1);
    memcpy(passwordDest, wifi_prov_pending_password_.c_str(), copyLen);
    passwordDest[copyLen] = '\0';
  }
  src = wifi_prov_pending_src_;
  clearWifiProvision();
  return true;
}

void PendingCommandManager::clearWifiProvision() {
  wifi_prov_pending_ = false;
  wifi_prov_pending_ssid_ = "";
  wifi_prov_pending_password_ = "";
  wifi_prov_pending_src_ = 0;
}

// Factory Reset
void PendingCommandManager::requestFactoryReset(bool keepFleet, bool keepWifi, uint8_t src) {
  factory_reset_keep_fleet_pending_ = keepFleet;
  factory_reset_keep_wifi_pending_ = keepWifi;
  factory_reset_pending_src_ = src;
  factory_reset_pending_ = true;
}

bool PendingCommandManager::consumeFactoryReset(bool &keepFleet, bool &keepWifi, uint8_t &src) {
  if (!factory_reset_pending_) return false;
  keepFleet = factory_reset_keep_fleet_pending_;
  keepWifi = factory_reset_keep_wifi_pending_;
  src = factory_reset_pending_src_;
  clearFactoryReset();
  return true;
}

void PendingCommandManager::clearFactoryReset() {
  factory_reset_pending_ = false;
  factory_reset_keep_fleet_pending_ = true;
  factory_reset_keep_wifi_pending_ = false;
  factory_reset_pending_src_ = 0;
}

// Reboot
void PendingCommandManager::requestReboot() {
  reboot_pending_ = true;
}

bool PendingCommandManager::consumeReboot() {
  if (!reboot_pending_) return false;
  clearReboot();
  return true;
}

void PendingCommandManager::clearReboot() {
  reboot_pending_ = false;
}

// Readdress
void PendingCommandManager::requestReaddress(uint8_t newAddress, uint8_t gwAddr) {
  readdress_pending_new_address_ = newAddress;
  readdress_pending_gw_addr_ = gwAddr;
  readdress_pending_ = true;
}

bool PendingCommandManager::consumeReaddress(uint8_t &newAddress, uint8_t &gwAddr) {
  if (!readdress_pending_) return false;
  newAddress = readdress_pending_new_address_;
  gwAddr = readdress_pending_gw_addr_;
  clearReaddress();
  return true;
}

void PendingCommandManager::clearReaddress() {
  readdress_pending_ = false;
  readdress_pending_new_address_ = 0;
  readdress_pending_gw_addr_ = 0;
}

// Peer Sync
void PendingCommandManager::requestPeerSync(uint32_t chipId, uint8_t address) {
  peer_sync_chip_id_ = chipId;
  peer_sync_address_ = address;
  peer_sync_pending_ = true;
}

bool PendingCommandManager::consumePeerSync(uint32_t &chipId, uint8_t &address) {
  if (!peer_sync_pending_) return false;
  chipId = peer_sync_chip_id_;
  address = peer_sync_address_;
  clearPeerSync();
  return true;
}

void PendingCommandManager::clearPeerSync() {
  peer_sync_pending_ = false;
  peer_sync_chip_id_ = 0;
  peer_sync_address_ = 0;
}

// Sensor Config
void PendingCommandManager::requestSensorConfig(bool tempEnabled, bool tankEnabled, bool powerSaveEnabled, bool powerSaveBootGrace) {
  sensor_config_temp_enabled_ = tempEnabled;
  sensor_config_tank_enabled_ = tankEnabled;
  sensor_config_power_save_enabled_ = powerSaveEnabled;
  sensor_config_power_save_boot_grace_ = powerSaveBootGrace;
  sensor_config_pending_ = true;
}

bool PendingCommandManager::consumeSensorConfig(bool &tempEnabled, bool &tankEnabled, bool &powerSaveEnabled, bool &powerSaveBootGrace) {
  if (!sensor_config_pending_) return false;
  tempEnabled = sensor_config_temp_enabled_;
  tankEnabled = sensor_config_tank_enabled_;
  powerSaveEnabled = sensor_config_power_save_enabled_;
  powerSaveBootGrace = sensor_config_power_save_boot_grace_;
  clearSensorConfig();
  return true;
}

void PendingCommandManager::clearSensorConfig() {
  sensor_config_pending_ = false;
  sensor_config_temp_enabled_ = false;
  sensor_config_tank_enabled_ = false;
  sensor_config_power_save_enabled_ = false;
  sensor_config_power_save_boot_grace_ = true;
}

// Fleet Prov Apply
void PendingCommandManager::requestFleetProvApply(uint16_t sessionNonce, uint8_t newAddress, bool roleTx, uint8_t controllerAddress, const char* key) {
  fleet_prov_apply_session_nonce_ = sessionNonce;
  fleet_prov_apply_address_ = newAddress;
  fleet_prov_apply_role_tx_ = roleTx;
  fleet_prov_apply_controller_address_ = controllerAddress;
  if (key != nullptr) {
    fleet_prov_apply_key_ = key;
  } else {
    fleet_prov_apply_key_ = "";
  }
  fleet_prov_apply_pending_ = (fleet_prov_apply_key_.length() > 0);
}

bool PendingCommandManager::consumeFleetProvApply(uint16_t &sessionNonce, uint8_t &newAddress, bool &roleTx, uint8_t &controllerAddress, char* keyDest, size_t keySize) {
  if (!fleet_prov_apply_pending_) return false;
  sessionNonce = fleet_prov_apply_session_nonce_;
  newAddress = fleet_prov_apply_address_;
  roleTx = fleet_prov_apply_role_tx_;
  controllerAddress = fleet_prov_apply_controller_address_;
  if (keyDest != nullptr && keySize > 0) {
    size_t len = fleet_prov_apply_key_.length();
    size_t copyLen = len < (keySize - 1) ? len : (keySize - 1);
    memcpy(keyDest, fleet_prov_apply_key_.c_str(), copyLen);
    keyDest[copyLen] = '\0';
  }
  clearFleetProvApply();
  return true;
}

void PendingCommandManager::clearFleetProvApply() {
  fleet_prov_apply_pending_ = false;
  fleet_prov_apply_session_nonce_ = 0;
  fleet_prov_apply_address_ = 0;
  fleet_prov_apply_role_tx_ = false;
  fleet_prov_apply_controller_address_ = 0;
  fleet_prov_apply_key_ = "";
}

// Fleet Key Change
void PendingCommandManager::requestFleetKeyChange(const char* key, uint8_t src) {
  if (key != nullptr) {
    fleet_key_pending_key_ = key;
  } else {
    fleet_key_pending_key_ = "";
  }
  fleet_key_pending_src_ = src;
  fleet_key_pending_ = (fleet_key_pending_key_.length() > 0);
}

bool PendingCommandManager::consumeFleetKeyChange(char *keyDest, size_t keySize, uint8_t &src) {
  if (!fleet_key_pending_) return false;
  if (keyDest != nullptr && keySize > 0) {
    size_t len = fleet_key_pending_key_.length();
    size_t copyLen = len < (keySize - 1) ? len : (keySize - 1);
    memcpy(keyDest, fleet_key_pending_key_.c_str(), copyLen);
    keyDest[copyLen] = '\0';
  }
  src = fleet_key_pending_src_;
  clearFleetKeyChange();
  return true;
}

void PendingCommandManager::clearFleetKeyChange() {
  fleet_key_pending_ = false;
  fleet_key_pending_key_ = "";
  fleet_key_pending_src_ = 0;
}
