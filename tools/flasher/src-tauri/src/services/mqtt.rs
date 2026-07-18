use std::sync::Arc;
use tokio::sync::Mutex;
use tauri::{AppHandle, Emitter};
use rumqttc::{AsyncClient, MqttOptions, QoS, Event, Packet};
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct MqttConfig {
    pub host: String,
    pub port: u16,
    pub user: Option<String>,
    pub password: Option<String>,
    pub topic_root: String,
}

#[derive(Serialize, Clone, Debug, PartialEq)]
pub enum MqttConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error(String),
}

#[derive(Serialize, Clone, Debug, PartialEq)]
pub struct MqttTelemetryPayload {
    pub gateway_id: String,
    pub address: u8,
    pub chip_id: Option<String>,
    pub field: String,
    pub value: serde_json::Value,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct MqttGatewayPayload {
    pub chip_id: String,
    pub role: String,
    pub mac: String,
    pub sta_ip: String,
    pub ap_ip: String,
    pub sta_ssid: String,
    pub uptime_ms: u64,
    pub fw_version: String,
    pub addr: u8,
    pub controller_addr: Option<u8>,
}

#[derive(Serialize, Clone, Debug)]
pub struct MqttAdminResponsePayload {
    pub chip_id: String,
    pub response: serde_json::Value,
}

#[derive(Serialize, Clone, Debug)]
pub struct MqttOtaStatusPayload {
    pub chip_id: String,
    pub status: String,
}

#[derive(Serialize, Clone, Debug)]
pub struct MqttConfigUpdatePayload {
    pub chip_id: String,
    pub field: String,
    pub value: String,
}

pub struct MqttService {
    client: Arc<Mutex<Option<AsyncClient>>>,
    state: Arc<Mutex<MqttConnectionState>>,
    event_loop_abort: Arc<Mutex<Option<tokio::task::JoinHandle<()>>>>,
}

impl Default for MqttService {
    fn default() -> Self {
        Self {
            client: Arc::new(Mutex::new(None)),
            state: Arc::new(Mutex::new(MqttConnectionState::Disconnected)),
            event_loop_abort: Arc::new(Mutex::new(None)),
        }
    }
}

impl MqttService {
    pub async fn get_state(&self) -> MqttConnectionState {
        self.state.lock().await.clone()
    }

    pub async fn disconnect(&self) -> Result<(), String> {
        let mut client_guard = self.client.lock().await;
        let mut abort_guard = self.event_loop_abort.lock().await;
        let mut state_guard = self.state.lock().await;

        if let Some(client) = client_guard.take() {
            let _ = client.disconnect().await;
        }

        if let Some(handle) = abort_guard.take() {
            handle.abort();
        }

        *state_guard = MqttConnectionState::Disconnected;
        Ok(())
    }

    pub async fn connect(&self, app: AppHandle, config: MqttConfig) -> Result<(), String> {
        // Disconnect existing if any
        self.disconnect().await?;

        let mut state_guard = self.state.lock().await;
        *state_guard = MqttConnectionState::Connecting;
        let _ = app.emit("mqtt-state-changed", state_guard.clone());

        // Setup MQTT client options
        let client_id = format!("lrs-flasher-{}", std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis())
            .unwrap_or(0));

        let mut mqttoptions = MqttOptions::new(client_id, &config.host, config.port);
        mqttoptions.set_keep_alive(std::time::Duration::from_secs(5));

        if let (Some(u), Some(p)) = (&config.user, &config.password) {
            if !u.is_empty() {
                mqttoptions.set_credentials(u, p);
            }
        }

        // Setup client and event loop
        let (client, mut eventloop) = AsyncClient::new(mqttoptions, 50);

        // Subscriptions
        let topic_root = config.topic_root.clone();
        let telemetry_filter = format!("{}/+/peers/+/#", topic_root);
        let discovery_filter = format!("{}/discovery/+", topic_root);
        let response_filter = format!("{}/+/admin_response", topic_root);
        let status_filter = format!("{}/+/ota_status", topic_root);
        let config_filter = format!("{}/+/config/#", topic_root);

        let sub_client = client.clone();
        tokio::spawn(async move {
            tokio::time::sleep(std::time::Duration::from_millis(500)).await;
            let _ = sub_client.subscribe(telemetry_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(discovery_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(response_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(status_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(config_filter, QoS::AtMostOnce).await;
        });

        // Store client
        *self.client.lock().await = Some(client.clone());
        *state_guard = MqttConnectionState::Connected;
        let _ = app.emit("mqtt-state-changed", state_guard.clone());

        // Spawn background event loop
        let state_clone = self.state.clone();
        let app_clone = app.clone();
        let handle = tokio::spawn(async move {
            loop {
                match eventloop.poll().await {
                    Ok(notification) => {
                        if let Event::Incoming(Packet::Publish(publish)) = notification {
                            if publish.retain {
                                // Ignore retained messages for request-response safety,
                                // but allow them for discovery and stats.
                                if publish.topic.contains("admin_response") {
                                    continue;
                                }
                            }

                            Self::handle_publish(&app_clone, &topic_root, publish.topic, publish.payload.to_vec());
                        }
                    }
                    Err(e) => {
                        let mut st = state_clone.lock().await;
                        *st = MqttConnectionState::Error(e.to_string());
                        let _ = app_clone.emit("mqtt-state-changed", st.clone());
                        break;
                    }
                }
            }
        });

        *self.event_loop_abort.lock().await = Some(handle);
        Ok(())
    }

    pub async fn publish_command(&self, topic: String, payload: String) -> Result<(), String> {
        let client_guard = self.client.lock().await;
        if let Some(client) = &*client_guard {
            client.publish(topic, QoS::AtMostOnce, false, payload.as_bytes())
                .await
                .map_err(|e| e.to_string())?;
            Ok(())
        } else {
            Err("MQTT client not connected".to_string())
        }
    }

    fn handle_publish(app: &AppHandle, topic_root: &str, topic: String, payload_bytes: Vec<u8>) {
        let payload_str = match String::from_utf8(payload_bytes) {
            Ok(s) => s,
            Err(_) => return,
        };

        if let Some(msg) = parse_mqtt_message(topic_root, &topic, &payload_str) {
            match msg {
                ParsedMqttMessage::Discovery(gw_payload) => {
                    let _ = app.emit("mqtt-gateway-update", gw_payload);
                }
                ParsedMqttMessage::AdminResponse { chip_id, response } => {
                    let _ = app.emit("mqtt-admin-response", MqttAdminResponsePayload {
                        chip_id,
                        response,
                    });
                }
                ParsedMqttMessage::OtaStatus { chip_id, status } => {
                    let _ = app.emit("mqtt-ota-status-update", MqttOtaStatusPayload {
                        chip_id,
                        status,
                    });
                }
                ParsedMqttMessage::ConfigUpdate { chip_id, field, value } => {
                    let _ = app.emit("mqtt-config-update", MqttConfigUpdatePayload {
                        chip_id,
                        field,
                        value,
                    });
                }
                ParsedMqttMessage::Telemetry(telemetry_payload) => {
                    let _ = app.emit("mqtt-telemetry-update", telemetry_payload);
                }
            }
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum ParsedMqttMessage {
    Discovery(MqttGatewayPayload),
    AdminResponse {
        chip_id: String,
        response: serde_json::Value,
    },
    OtaStatus {
        chip_id: String,
        status: String,
    },
    ConfigUpdate {
        chip_id: String,
        field: String,
        value: String,
    },
    Telemetry(MqttTelemetryPayload),
}

pub fn parse_mqtt_message(topic_root: &str, topic: &str, payload_str: &str) -> Option<ParsedMqttMessage> {
    // 1. Check discovery topic: <topic_root>/discovery/lrs-<chip_id>
    let discovery_prefix = format!("{}/discovery/", topic_root);
    if topic.starts_with(&discovery_prefix) {
        if let Ok(gw_payload) = serde_json::from_str::<MqttGatewayPayload>(payload_str) {
            return Some(ParsedMqttMessage::Discovery(gw_payload));
        }
        return None;
    }

    // Strip prefix for all other topics
    let prefix = format!("{}/", topic_root);
    let Some(rest) = topic.strip_prefix(&prefix) else { return None; };

    let parts: Vec<&str> = rest.split('/').collect();

    // 2. Check admin response topic: <gateway_id>/admin_response
    if parts.len() >= 2 && parts[1] == "admin_response" {
        let chip_id = parts[0].trim_start_matches("lrs-").to_string();
        if let Ok(json_val) = serde_json::from_str::<serde_json::Value>(payload_str) {
            return Some(ParsedMqttMessage::AdminResponse {
                chip_id,
                response: json_val,
            });
        }
        return None;
    }

    // Check ota status topic: <gateway_id>/ota_status
    if parts.len() >= 2 && parts[1] == "ota_status" {
        let chip_id = parts[0].trim_start_matches("lrs-").to_string();
        return Some(ParsedMqttMessage::OtaStatus {
            chip_id,
            status: payload_str.to_string(),
        });
    }

    // Check config topic: <gateway_id>/config/<field_name>
    if parts.len() >= 3 && parts[1] == "config" {
        let chip_id = parts[0].trim_start_matches("lrs-").to_string();
        let field = parts[2..].join("/");
        return Some(ParsedMqttMessage::ConfigUpdate {
            chip_id,
            field,
            value: payload_str.to_string(),
        });
    }

    // 3. Check peer telemetry: <gateway_id>/peers/<addrSeg>/<leaf>
    if parts.len() >= 4 && parts[1] == "peers" {
        let gateway_id = parts[0].trim_start_matches("lrs-").to_string();

        // Extract peer address segment and optional chip_id
        let addr_seg = parts[2];
        let mut chip_id = None;
        let address = if let Some(underscore_idx) = addr_seg.find('_') {
            let addr_part = &addr_seg[..underscore_idx];
            let rest_seg = &addr_seg[underscore_idx + 1..];
            if rest_seg.starts_with("lrs-") {
                chip_id = Some(rest_seg["lrs-".len()..].to_string());
            }
            addr_part.parse::<u8>().unwrap_or(0)
        } else {
            addr_seg.parse::<u8>().unwrap_or(0)
        };

        if address == 0 {
            return None;
        }

        let field = if parts[3] == "sensor" && parts.len() > 4 {
            parts[3..].join("/")
        } else {
            parts[3].to_string()
        };

        // Try to parse payload as JSON (if it is a JSON number/bool/string/null)
        let json_value = serde_json::from_str::<serde_json::Value>(payload_str)
            .unwrap_or_else(|_| serde_json::Value::String(payload_str.to_string()));

        return Some(ParsedMqttMessage::Telemetry(MqttTelemetryPayload {
            gateway_id,
            address,
            chip_id,
            field,
            value: json_value,
        }));
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_single_segment_root_parsing() {
        let root = "lora";

        // Admin Response
        let msg = parse_mqtt_message(root, "lora/lrs-123456/admin_response", "{\"cmd\":\"ok\"}");
        assert_eq!(msg, Some(ParsedMqttMessage::AdminResponse {
            chip_id: "123456".to_string(),
            response: serde_json::json!({"cmd":"ok"}),
        }));

        // OTA Status
        let msg = parse_mqtt_message(root, "lora/lrs-123456/ota_status", "downloading");
        assert_eq!(msg, Some(ParsedMqttMessage::OtaStatus {
            chip_id: "123456".to_string(),
            status: "downloading".to_string(),
        }));

        // Config Update
        let msg = parse_mqtt_message(root, "lora/lrs-123456/config/wifi_sta_ssid", "MySSID");
        assert_eq!(msg, Some(ParsedMqttMessage::ConfigUpdate {
            chip_id: "123456".to_string(),
            field: "wifi_sta_ssid".to_string(),
            value: "MySSID".to_string(),
        }));

        // Telemetry
        let msg = parse_mqtt_message(root, "lora/lrs-123456/peers/1_lrs-abcdef/relay", "1");
        assert_eq!(msg, Some(ParsedMqttMessage::Telemetry(MqttTelemetryPayload {
            gateway_id: "123456".to_string(),
            address: 1,
            chip_id: Some("abcdef".to_string()),
            field: "relay".to_string(),
            value: serde_json::json!(1),
        })));
    }

    #[test]
    fn test_nested_root_parsing() {
        let root = "site/home/lora";

        // Admin Response
        let msg = parse_mqtt_message(root, "site/home/lora/lrs-123456/admin_response", "{\"cmd\":\"ok\"}");
        assert_eq!(msg, Some(ParsedMqttMessage::AdminResponse {
            chip_id: "123456".to_string(),
            response: serde_json::json!({"cmd":"ok"}),
        }));

        // OTA Status
        let msg = parse_mqtt_message(root, "site/home/lora/lrs-123456/ota_status", "downloading");
        assert_eq!(msg, Some(ParsedMqttMessage::OtaStatus {
            chip_id: "123456".to_string(),
            status: "downloading".to_string(),
        }));

        // Config Update
        let msg = parse_mqtt_message(root, "site/home/lora/lrs-123456/config/wifi_sta_ssid", "MySSID");
        assert_eq!(msg, Some(ParsedMqttMessage::ConfigUpdate {
            chip_id: "123456".to_string(),
            field: "wifi_sta_ssid".to_string(),
            value: "MySSID".to_string(),
        }));

        // Telemetry
        let msg = parse_mqtt_message(root, "site/home/lora/lrs-123456/peers/1_lrs-abcdef/relay", "1");
        assert_eq!(msg, Some(ParsedMqttMessage::Telemetry(MqttTelemetryPayload {
            gateway_id: "123456".to_string(),
            address: 1,
            chip_id: Some("abcdef".to_string()),
            field: "relay".to_string(),
            value: serde_json::json!(1),
        })));
    }
}
