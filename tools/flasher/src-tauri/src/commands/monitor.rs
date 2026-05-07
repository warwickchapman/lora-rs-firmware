use tauri::{AppHandle, Emitter, State};
use std::time::Duration;
use std::sync::Arc;
use tokio::sync::Mutex;
use std::io::{BufRead, BufReader};
use serde::{Deserialize, Serialize};
use crate::services::serial_port_coordinator::SerialPortCoordinator;

#[derive(Serialize, Deserialize, Clone)]
pub struct MonitorEvent {
    pub line: String,
}

#[derive(Default)]
pub struct MonitorStatus {
    pub running: bool,
    pub port: Option<String>,
}

pub struct MonitorState {
    pub status: Arc<Mutex<MonitorStatus>>,
}

pub async fn request_stop_for_port(state: &MonitorState, port: &str) {
    let mut status = state.status.lock().await;
    if status.running && status.port.as_deref() == Some(port) {
        status.running = false;
    }
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
            if status.running {
                if status.port.as_deref() == Some(&port) {
                    return Ok(());
                }
                return Err(format!(
                    "Serial monitor is already running on {}",
                    status.port.as_deref().unwrap_or("another port")
                ));
            }
        }

        let guard = coordinator
            .acquire(&port, "serial monitor", Duration::from_secs(3))
            .await?;

        {
            let mut status = state.status.lock().await;
            if status.running {
                drop(guard);
                if status.port.as_deref() == Some(&port) {
                    return Ok(());
                }
                return Err(format!(
                    "Serial monitor is already running on {}",
                    status.port.as_deref().unwrap_or("another port")
                ));
            }
            status.running = true;
            status.port = Some(port.clone());
        }

        let status_clone = state.status.clone();
        let app_handle = app.clone();

        tokio::task::spawn_blocking(move || {
            let _guard = guard;
            let builder = serialport::new(&port, baud)
                .timeout(Duration::from_millis(100));

            match builder.open() {
                Ok(serial_port) => {
                    let mut reader = BufReader::new(serial_port);
                    loop {
                        let mut line = String::new();
                        if reader.read_line(&mut line).is_ok() && !line.is_empty() {
                            let _ = app_handle.emit("monitor-log", MonitorEvent { line });
                        }

                        if let Ok(status) = status_clone.try_lock() {
                            if !status.running || status.port.as_deref() != Some(&port) {
                                break;
                            }
                        }
                    }
                }
                Err(e) => {
                    let _ = app_handle.emit("monitor-log", MonitorEvent {
                        line: format!("Error opening port: {}", e)
                    });
                }
            }

            if let Ok(mut status) = status_clone.try_lock() {
                if status.port.as_deref() == Some(&port) {
                    status.running = false;
                    status.port = None;
                }
            }
        });
    } else {
        let mut status = state.status.lock().await;
        if !status.running {
            return Ok(());
        }
        if status.port.as_deref() == Some(&port) || status.port.is_none() {
            status.running = false;
        }
    }
    
    Ok(())
}
