use tauri::State;
use crate::services::mqtt_broker::MqttBrokerService;

#[tauri::command]
pub async fn start_local_mqtt_broker(
    service: State<'_, MqttBrokerService>,
    port: u16,
) -> Result<Vec<String>, String> {
    service.start(port).await
}

#[tauri::command]
pub async fn get_local_mqtt_broker_status(
    service: State<'_, MqttBrokerService>,
) -> Result<Option<u16>, String> {
    Ok(service.get_status().await)
}
