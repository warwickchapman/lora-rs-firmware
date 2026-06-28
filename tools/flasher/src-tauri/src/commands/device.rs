use serde::{Deserialize, Serialize};
use tauri::{AppHandle, State};
use tauri_plugin_shell::ShellExt;
use sha2::{Sha256, Digest};
use std::time::Duration;
use crate::commands::monitor::{self, MonitorState};
use crate::services::serial_port_coordinator::SerialPortCoordinator;

const PRODUCT_SECRET: &str = "LRS-v1-rotate-this-secret";

#[derive(Serialize, Deserialize, Clone, Default)]
pub struct DeviceInfo {
    pub chip_id: String,
    pub mac: String,
    pub password: String,
    pub local_addr: u8,
    pub remote_addr: u8,
    pub ssid: String,
}

#[tauri::command]
pub async fn derive_device_info_from_chip_id(chip_id: String) -> Result<DeviceInfo, String> {
    let chip_id = normalize_chip_id(&chip_id)?;
    Ok(derive_device_info(&chip_id, ""))
}

#[tauri::command]
pub async fn get_device_info(
    app: AppHandle,
    coordinator: State<'_, SerialPortCoordinator>,
    monitor_state: State<'_, MonitorState>,
    port: String,
) -> Result<DeviceInfo, String> {
    monitor::request_stop_for_port(&monitor_state, &port).await;
    let _guard = coordinator
        .acquire(&port, "reading device information", Duration::from_secs(8))
        .await?;
    let shell = app.shell();
    
    // 1. Get Chip ID
    let sidecar = shell.sidecar("esptool")
        .map_err(|e| format!("Failed to find sidecar: {}", e))?;
    
    let output = sidecar
        .args(["--port", &port, "chip_id"])
        .output()
        .await
        .map_err(|e| format!("Failed to execute esptool: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();
    let combined = format!("{}\n{}", stdout, stderr);
    
    let chip_id = parse_chip_id(&combined)?;
    let mac = match parse_mac(&combined) {
        Some(mac) => mac,
        None => {
            let sidecar_mac = shell.sidecar("esptool")
                .map_err(|e| format!("Failed to find sidecar: {}", e))?;

            let output_mac = sidecar_mac
                .args(["--port", &port, "read_mac"])
                .output()
                .await
                .map_err(|e| format!("Failed to execute esptool: {}", e))?;

            let stdout_mac = String::from_utf8_lossy(&output_mac.stdout).to_string();
            let stderr_mac = String::from_utf8_lossy(&output_mac.stderr).to_string();
            let combined_mac = format!("{}\n{}", stdout_mac, stderr_mac);
            parse_mac(&combined_mac).ok_or_else(|| "Unable to parse MAC from esptool output".to_string())?
        }
    };

    Ok(derive_device_info(&chip_id, &mac))
}

fn derive_device_info(chip_id: &str, mac: &str) -> DeviceInfo {
    let (local_addr, remote_addr) = derive_addresses(chip_id);
    DeviceInfo {
        chip_id: chip_id.to_string(),
        mac: mac.to_string(),
        password: derive_password(chip_id),
        local_addr,
        remote_addr,
        ssid: format!("lrs-{}", chip_id),
    }
}

fn normalize_chip_id(raw: &str) -> Result<String, String> {
    let clean = raw.trim()
        .trim_start_matches("lrs-")
        .trim_start_matches("LRS-")
        .trim_start_matches("0x")
        .trim_start_matches("0X")
        .to_lowercase();
    if clean.len() < 6 || clean.len() > 8 || !clean.chars().all(|c| c.is_ascii_hexdigit()) {
        return Err("chip_id must be 6-8 hex characters, optionally prefixed with lrs-".to_string());
    }
    Ok(clean)
}

fn parse_chip_id(output: &str) -> Result<String, String> {
    static RE: std::sync::LazyLock<regex::Regex> = std::sync::LazyLock::new(|| {
        regex::Regex::new(r"Chip ID:\s*0x([0-9A-Fa-f]+)").unwrap()
    });
    if let Some(caps) = RE.captures(output) {
        let hex_str = caps.get(1).unwrap().as_str();
        let val = u32::from_str_radix(hex_str, 16)
            .map_err(|e| format!("Failed to parse hex chip ID '{}': {}", hex_str, e))?;
        let masked = val & 0x00FFFFFF;
        Ok(format!("{:08x}", masked))
    } else {
        Err("Unable to parse chip ID from esptool output".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_chip_id_success() {
        // Assert that esptool output with MSB set is parsed and masked correctly
        assert_eq!(parse_chip_id("Chip ID: 0x800af8d9").unwrap(), "000af8d9");
        
        // Assert that a normal ID parses correctly
        assert_eq!(parse_chip_id("Chip ID: 0x0048cb85").unwrap(), "0048cb85");
    }

    #[test]
    fn test_parse_chip_id_failures() {
        // 1. Missing pattern entirely
        let res_missing = parse_chip_id("No chip ID here");
        assert!(res_missing.is_err());
        assert_ne!(res_missing, Ok("00000000".to_string()));

        // 2. Pattern matches "Chip ID: 0x" prefix but invalid characters following
        // (Fails regex match, returning standard "Unable to parse chip ID...")
        let res_invalid = parse_chip_id("Chip ID: 0xinvalid");
        assert!(res_invalid.is_err());
        assert_ne!(res_invalid, Ok("00000000".to_string()));

        // 3. Oversized hex string (Matches regex but fails u32::from_str_radix)
        let res_oversized = parse_chip_id("Chip ID: 0x100000000");
        assert!(res_oversized.is_err());
        assert!(res_oversized.as_ref().unwrap_err().contains("Failed to parse hex chip ID"));
        assert_ne!(res_oversized, Ok("00000000".to_string()));
    }
}

fn parse_mac(output: &str) -> Option<String> {
    static RE: std::sync::LazyLock<regex::Regex> = std::sync::LazyLock::new(|| {
        regex::Regex::new(r"MAC:\s*([0-9A-Fa-f:]{17})").unwrap()
    });
    if let Some(caps) = RE.captures(output) {
        Some(caps.get(1).unwrap().as_str().to_lowercase())
    } else {
        None
    }
}

fn derive_password(chip_hex: &str) -> String {
    let input = format!("{}:{}", PRODUCT_SECRET, chip_hex);
    let mut hasher = Sha256::new();
    hasher.update(input.as_bytes());
    let result = hasher.finalize();
    let hex = hex::encode(result);
    hex[..8].to_string()
}

fn derive_addresses(chip_hex: &str) -> (u8, u8) {
    let chip = u32::from_str_radix(chip_hex, 16).unwrap_or(0);
    let local_addr = ((chip & 0xFF) % 254 + 1) as u8;
    let mut remote_addr = (((chip >> 8) & 0xFF) % 254 + 1) as u8;
    if remote_addr == local_addr {
        remote_addr = (local_addr % 254) + 1;
    }
    (local_addr, remote_addr)
}
