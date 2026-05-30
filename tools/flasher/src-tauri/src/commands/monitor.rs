use tauri::{AppHandle, Emitter, State};
use std::time::Duration;
use std::sync::Arc;
use tokio::sync::Mutex;
use std::io::{BufRead, BufReader};
use serde::{Deserialize, Serialize};
use crate::services::serial_port_coordinator::SerialPortCoordinator;
use std::collections::HashSet;

#[derive(Serialize, Deserialize, Clone)]
pub struct MonitorEvent {
    pub port: String,
    pub line: String,
}

#[derive(Default)]
pub struct MonitorStatus {
    pub active_ports: HashSet<String>,
}

pub struct MonitorState {
    pub status: Arc<Mutex<MonitorStatus>>,
}

pub async fn request_stop_for_port(state: &MonitorState, port: &str) {
    let mut status = state.status.lock().await;
    status.active_ports.remove(port);
}

#[tauri::command]
pub async fn toggle_serial_monitor(
    app: AppHandle,
    state: State<'_, MonitorState>,
    coordinator: State<'_, SerialPortCoordinator>,
    port: String,
    baud: u32,
    enable: bool,
) -> Result<(), String> {
    if enable {
        {
            let status = state.status.lock().await;
            if status.active_ports.contains(&port) {
                return Ok(()); // Already running on this port
            }
        }

        let guard = coordinator
            .acquire(&port, "serial monitor", Duration::from_secs(3))
            .await?;

        {
            let mut status = state.status.lock().await;
            if status.active_ports.contains(&port) {
                drop(guard);
                return Ok(());
            }
            status.active_ports.insert(port.clone());
        }

        let status_clone = state.status.clone();
        let app_handle = app.clone();
        let port_clone = port.clone();

        tokio::task::spawn_blocking(move || {
            let _guard = guard;
            let builder = serialport::new(&port_clone, baud)
                .timeout(Duration::from_millis(100));

            match builder.open() {
                Ok(serial_port) => {
                    let mut reader = BufReader::new(serial_port);
                    loop {
                        let mut line = String::new();
                        if reader.read_line(&mut line).is_ok() && !line.is_empty() {
                            let _ = app_handle.emit("monitor-log", MonitorEvent {
                                port: port_clone.clone(),
                                line,
                            });
                        }

                        if let Ok(status) = status_clone.try_lock() {
                            if !status.active_ports.contains(&port_clone) {
                                break;
                            }
                        }
                    }
                }
                Err(e) => {
                    let _ = app_handle.emit("monitor-log", MonitorEvent {
                        port: port_clone.clone(),
                        line: format!("Error opening port: {}", e),
                    });
                }
            }

            if let Ok(mut status) = status_clone.try_lock() {
                status.active_ports.remove(&port_clone);
            }
        });
    } else {
        let mut status = state.status.lock().await;
        status.active_ports.remove(&port);
    }
    
    Ok(())
}
