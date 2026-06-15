use tauri::{AppHandle, State};
use crate::services::mqtt::{MqttService, MqttConfig, MqttConnectionState};

#[tauri::command]
pub async fn connect_mqtt_broker(
    service: State<'_, MqttService>,
    app: AppHandle,
    config: MqttConfig,
) -> Result<(), String> {
    service.connect(app, config).await
}

#[tauri::command]
pub async fn disconnect_mqtt_broker(
    service: State<'_, MqttService>,
) -> Result<(), String> {
    service.disconnect().await
}

#[tauri::command]
pub async fn publish_mqtt_command(
    service: State<'_, MqttService>,
    topic: String,
    payload: String,
) -> Result<(), String> {
    service.publish_command(topic, payload).await
}

#[tauri::command]
pub async fn get_mqtt_state(
    service: State<'_, MqttService>,
) -> Result<MqttConnectionState, String> {
    Ok(service.get_state().await)
}
