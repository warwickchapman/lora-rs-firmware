use serde_json::Value;
use std::time::Duration;
use tauri::State;

use crate::commands::monitor::{self, MonitorState};
use crate::services::easy_pair;
use crate::services::serial_port_coordinator::SerialPortCoordinator;

#[tauri::command]
pub async fn serial_admin_command(
    coordinator: State<'_, SerialPortCoordinator>,
    monitor_state: State<'_, MonitorState>,
    port: String,
    request: Value,
    timeout_ms: Option<u64>,
) -> Result<Value, String> {
    monitor::request_stop_for_port(&monitor_state, &port).await;
    let _guard = coordinator
        .acquire(&port, "EasyPair serial command", Duration::from_secs(8))
        .await?;

    tauri::async_runtime::spawn_blocking(move || {
        easy_pair::send_serial_admin_command(&port, request, timeout_ms.unwrap_or(8000))
    })
    .await
    .map_err(|e| format!("serial admin task failed: {}", e))?
}
