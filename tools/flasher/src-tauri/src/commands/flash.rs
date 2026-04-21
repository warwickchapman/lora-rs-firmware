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
    erase_first: Option<bool>,
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

    let (erase_op, write_op) = detect_flash_ops(&app).await?;
    log(format!(
        "Using esptool ops: erase='{}', write='{}'",
        erase_op, write_op
    ));

    if erase_first.unwrap_or(false) {
        log(format!("Erasing flash on {} before write...", port));
        run_esptool_and_stream(&app, &["--port", &port, erase_op.as_str()]).await?;
        log("Erase complete.".into());
    }

    // 5. Flash via Sidecar
    log(format!("Invoking esptool sidecar on {}...", port));
    run_esptool_and_stream(
        &app,
        &[
            "--port",
            &port,
            "--baud",
            "460800",
            write_op.as_str(),
            "0x0",
            flash_file.to_str().unwrap(),
        ],
    )
    .await?;

    log("Flash successful! Device is resetting...".into());
    let filename = flash_file.file_name().map(|f| f.to_string_lossy().to_string()).unwrap_or_else(|| "firmware".to_string());
    Ok(format!("Successfully flashed {} to {}", filename, port))
}

async fn detect_flash_ops(app: &AppHandle) -> Result<(String, String), String> {
    let shell = app.shell();
    let sidecar = shell
        .sidecar("esptool")
        .map_err(|e| format!("Failed to find sidecar: {}", e))?;

    let output = sidecar
        .args(["-h"])
        .output()
        .await
        .map_err(|e| format!("Failed to execute esptool help: {}", e))?;

    let stdout = String::from_utf8_lossy(&output.stdout).to_lowercase();
    let stderr = String::from_utf8_lossy(&output.stderr).to_lowercase();
    let combined = format!("{}\n{}", stdout, stderr);

    let has_hyphen = combined.contains("erase-flash") || combined.contains("write-flash");
    let has_underscore = combined.contains("erase_flash") || combined.contains("write_flash");

    if has_hyphen {
        return Ok(("erase-flash".to_string(), "write-flash".to_string()));
    }
    if has_underscore {
        return Ok(("erase_flash".to_string(), "write_flash".to_string()));
    }

    // Conservative fallback: keep underscore style for older/bundled variants.
    Ok(("erase_flash".to_string(), "write_flash".to_string()))
}

async fn run_esptool_and_stream(app: &AppHandle, args: &[&str]) -> Result<(), String> {
    let shell = app.shell();
    let sidecar = shell.sidecar("esptool")
        .map_err(|e| format!("Failed to find sidecar: {}", e))?;

    let (mut rx, _child) = sidecar
        .args(args)
        .env("PYTHONUNBUFFERED", "1")
        .spawn()
        .map_err(|e| format!("Failed to spawn esptool: {}", e))?;

    while let Some(event) = rx.recv().await {
        match event {
            CommandEvent::Stdout(line) => emit_log_line(app, &line),
            CommandEvent::Stderr(line) => emit_log_line(app, &line),
            CommandEvent::Terminated(payload) => {
                if payload.code == Some(0) {
                    return Ok(());
                } else {
                    return Err(format!("esptool failed with code {:?}", payload.code));
                }
            }
            _ => {}
        }
    }

    Err("esptool process terminated unexpectedly".into())
}

fn emit_log_line(app: &AppHandle, line: &[u8]) {
    let msg = String::from_utf8_lossy(line).to_string();
    let clean_msg = strip_ansi(&msg);
    if !clean_msg.is_empty() {
        let _ = app.emit("flash-log", LogEvent { message: clean_msg });
    }
}

fn strip_ansi(s: &str) -> String {
    // Robust ANSI escape code regex
    let re = regex::Regex::new(r"[\u001b\u009b][\[()#;?]*(?:[0-9;]*[0-9ABCDEFGHJKSTmpeignqrsuy])").unwrap();
    re.replace_all(s, "").to_string()
}
