use crate::services::{firmware, network};
use crate::services::diagnostics::DiagnosticStore;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tauri::{AppHandle, Emitter, State};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, UdpSocket};
use tokio::sync::Mutex;
use tokio::task::JoinHandle;

#[derive(Clone, Default)]
pub struct UdpMonitorState {
    pub task: Arc<Mutex<Option<JoinHandle<()>>>>,
}

#[derive(Clone, Default)]
pub struct FirmwareServerState {
    pub task: Arc<Mutex<Option<JoinHandle<()>>>>,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct UdpLogEvent {
    pub line: String,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct FirmwareServerOptions {
    pub firmware_path: String,
    pub profile: Option<String>,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct FirmwareServerInfo {
    pub filename: String,
    pub sha256: String,
    pub size_bytes: u64,
    pub port: u16,
    pub urls: Vec<String>,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct NetworkInterface {
    pub ip: String,
    pub netmask: String,
}

#[tauri::command]
pub async fn local_udp_log_hosts() -> Result<Vec<String>, String> {
    Ok(network::local_lan_subnets()?
        .into_iter()
        .map(|s| s.address)
        .collect())
}

#[tauri::command]
pub async fn get_network_interfaces() -> Result<Vec<NetworkInterface>, String> {
    let mut interfaces = Vec::new();
    let addrs = get_if_addrs::get_if_addrs().map_err(|e| e.to_string())?;
    
    for iface in addrs {
        if let get_if_addrs::IfAddr::V4(addr) = iface.addr {
            if !addr.is_loopback() {
                interfaces.push(NetworkInterface {
                    ip: addr.ip.to_string(),
                    netmask: addr.netmask.to_string(),
                });
            }
        }
    }
    
    Ok(interfaces)
}

#[tauri::command]
pub async fn start_firmware_file_server(
    app: AppHandle,
    state: State<'_, FirmwareServerState>,
    diagnostics: State<'_, DiagnosticStore>,
    options: FirmwareServerOptions,
) -> Result<FirmwareServerInfo, String> {
    stop_firmware_file_server(state.clone()).await?;

    let firmware_path = firmware::resolve_firmware_path(&app, &options.firmware_path, options.profile, None).await?;
    let firmware_bytes = Arc::new(tokio::fs::read(&firmware_path).await.map_err(|e| e.to_string())?);
    let sha256 = firmware::calculate_sha256(&firmware_path)?;
    let filename = firmware_path
        .file_name()
        .map(|f| f.to_string_lossy().to_string())
        .unwrap_or_else(|| "firmware.bin".into());
    let size_bytes = firmware_bytes.len() as u64;

    let listener = TcpListener::bind(("0.0.0.0", 0)).await.map_err(|e| e.to_string())?;
    let port = listener.local_addr().map_err(|e| e.to_string())?.port();
    let urls = firmware_server_urls(port);
    let served_name = filename.clone();
    let task_bytes = firmware_bytes.clone();
    let diagnostics = diagnostics.inner().clone();
    let task = tokio::spawn(async move {
        loop {
            let Ok((mut socket, peer)) = listener.accept().await else {
                break;
            };
            let body = task_bytes.clone();
            let name = served_name.clone();
            let diagnostics = diagnostics.clone();
            tokio::spawn(async move {
                let mut req = [0u8; 1024];
                let n = match socket.read(&mut req).await {
                    Ok(n) => n,
                    Err(_) => return,
                };
                let request = String::from_utf8_lossy(&req[..n]);
                let first_line = request.lines().next().unwrap_or_default();
                let ok_path = first_line.starts_with("GET /firmware.bin ") || first_line.starts_with("GET /firmware ");
                if !ok_path {
                    diagnostics.record(peer.ip().to_string(), "http", "firmware_request_rejected", first_line, None).await;
                    let _ = socket.write_all(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").await;
                    return;
                }
                let header = format!(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\"{}\"\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                    name,
                    body.len()
                );
                if socket.write_all(header.as_bytes()).await.is_err() {
                    diagnostics.record(peer.ip().to_string(), "http", "firmware_delivery_disconnected", first_line, None).await;
                    return;
                }
                match socket.write_all(&body).await {
                    Ok(()) => diagnostics.record(peer.ip().to_string(), "http", "firmware_delivery_complete", format!("path=/firmware.bin bytes={}", body.len()), None).await,
                    Err(err) => diagnostics.record(peer.ip().to_string(), "http", "firmware_delivery_disconnected", format!("path=/firmware.bin bytes={} error={}", body.len(), err), None).await,
                }
            });
        }
    });
    *state.task.lock().await = Some(task);

    Ok(FirmwareServerInfo {
        filename,
        sha256,
        size_bytes,
        port,
        urls,
    })
}

#[tauri::command]
pub async fn stop_firmware_file_server(state: State<'_, FirmwareServerState>) -> Result<String, String> {
    if let Some(task) = state.task.lock().await.take() {
        task.abort();
    }
    Ok("Firmware file server stopped".into())
}

#[tauri::command]
pub async fn start_network_udp_monitor(
    app: AppHandle,
    state: State<'_, UdpMonitorState>,
    diagnostics: State<'_, DiagnosticStore>,
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
    let diagnostics = diagnostics.inner().clone();
    let task = tokio::spawn(async move {
        let mut buf = vec![0u8; 2048];
        loop {
            match socket.recv_from(&mut buf).await {
                Ok((len, from)) => {
                    let text = String::from_utf8_lossy(&buf[..len]).to_string();
                    for line in text.split(|c| c == '\n' || c == '\r') {
                        let trimmed = line.trim();
                        if !trimmed.is_empty() {
                            let event = trimmed.split_whitespace().find_map(|part| part.strip_prefix("event=")).unwrap_or("udp_log");
                            diagnostics.record(from.ip().to_string(), "udp", event, trimmed, None).await;
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

fn firmware_server_urls(port: u16) -> Vec<String> {
    match network::local_lan_subnets() {
        Ok(subnets) => subnets
            .into_iter()
            .map(|s| format!("http://{}:{}/firmware.bin", s.address, port))
            .collect(),
        Err(_) => vec![format!("http://127.0.0.1:{}/firmware.bin", port)],
    }
}
