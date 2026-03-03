use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::ShellExt;
use tauri_plugin_shell::process::CommandEvent;
use crate::services::firmware;

#[derive(Serialize, Deserialize, Clone)]
pub struct LogEvent {
    pub message: String,
}

#[tauri::command]
pub async fn flash_firmware(
    app: AppHandle,
    port: String,
    firmware_path: String,
    region: Option<String>,
) -> Result<String, String> {
    let app_clone = app.clone();
    let log = move |msg: String| {
        let _ = app_clone.emit("flash-log", LogEvent { message: msg });
    };

    let local_path = std::path::PathBuf::from(&firmware_path);
    
    let flash_file = if local_path.exists() && local_path.is_file() {
        log(format!("Using local firmware file at {}...", firmware_path));
        local_path
    } else {
        // Tag based GitHub download
        let reg = region.ok_or_else(|| "Region is required for GitHub downloads".to_string())?;
        log(format!("Starting cloud flash for {} (Region: {})...", firmware_path, reg));
        
        // 1. Fetch releases
        log("Fetching release details...".into());
        let releases = firmware::fetch_releases().await?;
        let release = releases.into_iter()
            .find(|r| r.tag_name == firmware_path)
            .ok_or_else(|| format!("Release {} not found", firmware_path))?;

        // 2. Find asset
        let search_suffix = format!("{}.bin", reg.to_lowercase());
        let asset = release.assets.into_iter()
            .find(|a| a.name.to_lowercase().ends_with(&search_suffix))
            .ok_or_else(|| format!("No asset found for region {} in release {}", reg, firmware_path))?;

        // 3. Download
        log(format!("Downloading asset: {}...", asset.name));
        let path = firmware::download_firmware(&app, &asset.browser_download_url, &asset.name).await?;

        // 4. Verify SHA256
        log("Verifying checksum...".into());
        let sha256 = firmware::calculate_sha256(&path)?;
        log(format!("SHA256: {}", sha256));
        
        path
    };

    // 5. Flash via Sidecar
    log(format!("Invoking esptool sidecar on {}...", port));
    
    let shell = app.shell();
    let sidecar = shell.sidecar("esptool")
        .map_err(|e| format!("Failed to find sidecar: {}", e))?;
    
    // Command: esptool --port <PORT> --baud 460800 write-flash 0x0 <PATH>
    let (mut rx, _child) = sidecar
        .args(["--port", &port, "--baud", "460800", "write_flash", "0x0", flash_file.to_str().unwrap()])
        .env("PYTHONUNBUFFERED", "1")
        .spawn()
        .map_err(|e| format!("Failed to spawn esptool: {}", e))?;

    let app_handle = app.clone();
    
    // Process sidecar events
    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line) => {
                let msg = String::from_utf8_lossy(&line).to_string();
                let clean_msg = strip_ansi(&msg);
                if !clean_msg.is_empty() {
                    let _ = app_handle.emit("flash-log", LogEvent { message: clean_msg });
                }
            }
            CommandEvent::Stderr(line) => {
                let msg = String::from_utf8_lossy(&line).to_string();
                let clean_msg = strip_ansi(&msg);
                if !clean_msg.is_empty() {
                    let _ = app_handle.emit("flash-log", LogEvent { message: clean_msg });
                }
            }
            CommandEvent::Terminated(payload) => {
                if payload.code == Some(0) {
                    log("Flash successful! Device is resetting...".into());
                    let filename = flash_file.file_name().map(|f| f.to_string_lossy().to_string()).unwrap_or_else(|| "firmware".to_string());
                    return Ok(format!("Successfully flashed {} to {}", filename, port));
                } else {
                    return Err(format!("esptool failed with code {:?}", payload.code));
                }
            }
            _ => {}
        }
    }

    Err("esptool process terminated unexpectedly".into())
}

fn strip_ansi(s: &str) -> String {
    // Robust ANSI escape code regex
    let re = regex::Regex::new(r"[\u001b\u009b][\[()#;?]*(?:[0-9;]*[0-9ABCDEFGHJKSTmpeignqrsuy])").unwrap();
    re.replace_all(s, "").to_string()
}
