use serde_json::Value;

use crate::services::easy_pair;

#[tauri::command]
pub async fn serial_admin_command(
    port: String,
    request: Value,
    timeout_ms: Option<u64>,
) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        easy_pair::send_serial_admin_command(&port, request, timeout_ms.unwrap_or(8000))
    })
    .await
    .map_err(|e| format!("serial admin task failed: {}", e))?
}
