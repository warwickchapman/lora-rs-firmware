use tauri::State;
use serde_json::Value;
use crate::services::diagnostics::{CaptureAnomalies, CaptureTimeline, DiagnosticCapture, DiagnosticInput, DiagnosticSnapshot, DiagnosticStore};

/// UI-facing read of the same bounded store that the future MCP bridge will use.
#[tauri::command]
pub async fn diagnostic_events(
    store: State<'_, DiagnosticStore>,
    after_sequence: Option<u64>,
    limit: Option<usize>,
) -> Result<DiagnosticSnapshot, String> {
    Ok(store.since(after_sequence, limit.unwrap_or(250)).await)
}

#[tauri::command]
pub async fn begin_ota_diagnostic_capture(
    store: State<'_, DiagnosticStore>,
    capture: DiagnosticCapture,
) -> Result<DiagnosticCapture, String> {
    Ok(store.begin_capture(capture).await)
}

#[tauri::command]
pub async fn diagnostic_captures(store: State<'_, DiagnosticStore>) -> Result<Vec<DiagnosticCapture>, String> {
    Ok(store.captures().await)
}

#[tauri::command]
pub async fn update_ota_capture_transfer(store: State<'_, DiagnosticStore>, id: String, transfer_id: u8) -> Result<(), String> {
    store.update_capture_transfer(&id, transfer_id).await
}

#[tauri::command]
pub async fn record_diagnostic_event(store: State<'_, DiagnosticStore>, input: DiagnosticInput) -> Result<(), String> {
    store.record(input.source, input.transport, input.event, input.raw, input.operation_id).await;
    Ok(())
}

#[tauri::command]
pub async fn ota_capture_timeline(store: State<'_, DiagnosticStore>, id: String) -> Result<CaptureTimeline, String> {
    store.timeline(&id).await
}

#[tauri::command]
pub async fn ota_capture_anomalies(store: State<'_, DiagnosticStore>, id: String) -> Result<CaptureAnomalies, String> {
    store.anomalies(&id).await
}
#[tauri::command]
pub async fn publish_support_snapshot(store: State<'_, DiagnosticStore>, snapshot: Value) -> Result<(), String> { store.set_support_snapshot(snapshot).await; Ok(()) }
#[tauri::command]
pub async fn flasher_support_snapshot(store: State<'_, DiagnosticStore>) -> Result<Value, String> { Ok(store.support_snapshot().await) }
