use crate::services::{firmware, network};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;
use tauri::{AppHandle, Emitter, State};
use tokio::net::UdpSocket;
use tokio::sync::Mutex;
use tokio::task::{JoinHandle, JoinSet};

#[derive(Clone, Default)]
pub struct UdpMonitorState {
    pub task: Arc<Mutex<Option<JoinHandle<()>>>>,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct UdpLogEvent {
    pub line: String,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct NetworkDiscoveryResult {
    pub subnets: Vec<network::LanSubnet>,
    pub devices: Vec<network::NetworkDevice>,
    pub scanned_hosts: usize,
    pub duration_ms: u128,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct NetworkScanProgressEvent {
    pub scanned_hosts: usize,
    pub total_hosts: usize,
    pub found_devices: usize,
    pub subnet_label: String,
}

#[tauri::command]
pub async fn discover_network_devices(app: AppHandle) -> Result<NetworkDiscoveryResult, String> {
    let started = std::time::Instant::now();
    let subnets = network::local_lan_subnets()?;
    let subnet_label = subnets
        .iter()
        .map(|s| format!("{} {}/{}", s.interface_name, s.network, s.cidr))
        .collect::<Vec<_>>()
        .join(", ");
    let targets = network::scan_targets_for_subnets(&subnets);
    let scanned_hosts = targets.len();
    let client = reqwest::Client::builder()
        .timeout(Duration::from_millis(1500))
        .pool_max_idle_per_host(0)
        .build()
        .map_err(|e| e.to_string())?;

    let mut devices = Vec::new();
    let mut completed_hosts = 0usize;
    emit_scan_progress(&app, completed_hosts, scanned_hosts, devices.len(), &subnet_label);
    for chunk in targets.chunks(32) {
        let mut tasks = JoinSet::new();
        for ip in chunk {
            let client = client.clone();
            let ip = ip.clone();
            tasks.spawn(async move { network::probe_device(client, ip).await });
        }
        while let Some(result) = tasks.join_next().await {
            completed_hosts += 1;
            if let Ok(Some(device)) = result {
                let _ = app.emit("network-device-found", device.clone());
                devices.push(device);
            }
            emit_scan_progress(&app, completed_hosts, scanned_hosts, devices.len(), &subnet_label);
        }
    }
    devices.sort_by(|a, b| ip_sort_key(&a.ip).cmp(&ip_sort_key(&b.ip)));

    Ok(NetworkDiscoveryResult {
        subnets,
        devices,
        scanned_hosts,
        duration_ms: started.elapsed().as_millis(),
    })
}

fn emit_scan_progress(
    app: &AppHandle,
    scanned_hosts: usize,
    total_hosts: usize,
    found_devices: usize,
    subnet_label: &str,
) {
    let _ = app.emit(
        "network-scan-progress",
        NetworkScanProgressEvent {
            scanned_hosts,
            total_hosts,
            found_devices,
            subnet_label: subnet_label.to_string(),
        },
    );
}

#[tauri::command]
pub async fn authenticate_network_device(
    ip: String,
    password: String,
) -> Result<network::NetworkDevice, String> {
    if password.trim().is_empty() {
        return Err("Enter an admin password for this device".into());
    }
    network::authenticated_status(&ip, password.trim()).await
}

#[tauri::command]
pub async fn ota_network_device(
    app: AppHandle,
    ip: String,
    options: network::OtaOptions,
) -> Result<String, String> {
    if options.password.trim().is_empty() {
        return Err("Enter an admin password for this device".into());
    }
    let firmware_path = resolve_firmware_path(&app, &options.firmware_path, options.region).await?;
    let sha256 = firmware::calculate_sha256(&firmware_path)?;
    let filename = firmware_path
        .file_name()
        .map(|f| f.to_string_lossy().to_string())
        .unwrap_or_else(|| "firmware.bin".into());
    let _ = app.emit(
        "network-monitor-log",
        UdpLogEvent {
            line: format!("Network OTA: {} SHA256 {}", filename, sha256),
        },
    );
    network::upload_ota(&ip, options.password.trim(), firmware_path).await
}

#[tauri::command]
pub async fn start_network_udp_monitor(
    app: AppHandle,
    state: State<'_, UdpMonitorState>,
) -> Result<String, String> {
    stop_network_udp_monitor(state.clone()).await?;

    let socket = UdpSocket::bind(("0.0.0.0", network::udp_log_port()))
        .await
        .map_err(|e| {
            format!(
                "Unable to listen for UDP logs on {}: {}",
                network::udp_log_port(),
                e
            )
        })?;
    let task = tokio::spawn(async move {
        let mut buf = vec![0u8; 2048];
        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, from)) => {
                    let text = String::from_utf8_lossy(&buf[..len]).to_string();
                    for line in text.split(|c| c == '\n' || c == '\r') {
                        let trimmed = line.trim();
                        if !trimmed.is_empty() {
                            let _ = app.emit(
                                "network-monitor-log",
                                UdpLogEvent {
                                    line: format!("{} {}", from.ip(), trimmed),
                                },
                            );
                        }
                    }
                }
                Err(err) => {
                    let _ = app.emit(
                        "network-monitor-log",
                        UdpLogEvent {
                            line: format!("UDP monitor stopped: {}", err),
                        },
                    );
                    break;
                }
            }
        }
    });

    *state.task.lock().await = Some(task);
    Ok(format!(
        "UDP monitor listening on {}",
        network::udp_log_port()
    ))
}

#[tauri::command]
pub async fn stop_network_udp_monitor(state: State<'_, UdpMonitorState>) -> Result<String, String> {
    if let Some(task) = state.task.lock().await.take() {
        task.abort();
    }
    Ok("UDP monitor stopped".into())
}

#[tauri::command]
pub async fn enable_network_udp_logging(
    ip: String,
    options: network::UdpLogOptions,
) -> Result<String, String> {
    if options.password.trim().is_empty() {
        return Err("Enter an admin password for this device".into());
    }
    network::enable_udp_logging(
        &ip,
        network::UdpLogOptions {
            password: options.password.trim().to_string(),
            ttl_s: Some(options.ttl_s.unwrap_or(300)),
        },
    )
    .await
}

async fn resolve_firmware_path(
    app: &AppHandle,
    firmware_path: &str,
    region: Option<String>,
) -> Result<PathBuf, String> {
    let local_path = PathBuf::from(firmware_path);
    if local_path.exists() && local_path.is_file() {
        return Ok(local_path);
    }

    let reg = region.ok_or_else(|| "Region is required for GitHub downloads".to_string())?;
    let releases = firmware::fetch_releases().await?;
    let release = releases
        .into_iter()
        .find(|r| r.tag_name == firmware_path)
        .ok_or_else(|| format!("Release {} not found", firmware_path))?;
    let search_suffix = format!("{}.bin", reg.to_lowercase());
    let asset = release
        .assets
        .into_iter()
        .find(|a| a.name.to_lowercase().ends_with(&search_suffix))
        .ok_or_else(|| {
            format!(
                "No asset found for region {} in release {}",
                reg, firmware_path
            )
        })?;
    firmware::download_firmware(app, &asset.browser_download_url, &asset.name).await
}

fn ip_sort_key(ip: &str) -> u32 {
    ip.parse::<std::net::Ipv4Addr>()
        .map(u32::from)
        .unwrap_or(u32::MAX)
}
