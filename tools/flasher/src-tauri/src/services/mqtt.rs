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

#[derive(Serialize, Clone, Debug)]
pub struct MqttTelemetryPayload {
    pub gateway_id: String,
    pub address: u8,
    pub field: String,
    pub value: serde_json::Value,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct MqttGatewayPayload {
    pub chip_id: String,
    pub role: String,
    pub mac: String,
    pub sta_ip: String,
    pub ap_ip: String,
    pub sta_ssid: String,
    pub uptime_ms: u64,
    pub fw_version: String,
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
        let telemetry_filter = format!("{}/+/peers/+/+", topic_root);
        let discovery_filter = format!("{}/discovery/+", topic_root);
        let response_filter = format!("{}/+/admin_response", topic_root);
        let status_filter = format!("{}/+/ota_status", topic_root);

        let sub_client = client.clone();
        tokio::spawn(async move {
            tokio::time::sleep(std::time::Duration::from_millis(500)).await;
            let _ = sub_client.subscribe(telemetry_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(discovery_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(response_filter, QoS::AtMostOnce).await;
            let _ = sub_client.subscribe(status_filter, QoS::AtMostOnce).await;
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

        // 1. Check discovery topic: <topic_root>/discovery/lrs-<chip_id>
        let discovery_prefix = format!("{}/discovery/", topic_root);
        if topic.starts_with(&discovery_prefix) {
            if let Ok(gw_payload) = serde_json::from_str::<MqttGatewayPayload>(&payload_str) {
                let _ = app.emit("mqtt-gateway-update", gw_payload);
            }
            return;
        }

        // 2. Check admin response topic: <topic_root>/lrs-<chip_id>/admin_response
        let parts: Vec<&str> = topic.split('/').collect();
        if parts.len() >= 3 && parts[0] == topic_root && parts[2] == "admin_response" {
            let chip_id = parts[1].trim_start_matches("lrs-").to_string();
            if let Ok(json_val) = serde_json::from_str::<serde_json::Value>(&payload_str) {
                let _ = app.emit("mqtt-admin-response", MqttAdminResponsePayload {
                    chip_id,
                    response: json_val,
                });
            }
            return;
        }

        // Check ota status topic: <topic_root>/lrs-<chip_id>/ota_status
        if parts.len() >= 3 && parts[0] == topic_root && parts[2] == "ota_status" {
            let chip_id = parts[1].trim_start_matches("lrs-").to_string();
            let _ = app.emit("mqtt-ota-status-update", MqttOtaStatusPayload {
                chip_id,
                status: payload_str.clone(),
            });
            return;
        }

        // 3. Check peer telemetry: <topic_root>/lrs-<chip_id>/peers/<addrSeg>/<leaf>
        if parts.len() >= 5 && parts[0] == topic_root && parts[2] == "peers" {
            let gateway_id = parts[1].trim_start_matches("lrs-").to_string();
            
            // Extract peer address segment
            let addr_seg = parts[3];
            let address = if let Some(underscore_idx) = addr_seg.find('_') {
                addr_seg[..underscore_idx].parse::<u8>().unwrap_or(0)
            } else {
                addr_seg.parse::<u8>().unwrap_or(0)
            };

            if address == 0 {
                return;
            }

            let field = parts[4].to_string();
            
            // Try to parse payload as JSON (if it is a JSON number/bool/string/null)
            let json_value = serde_json::from_str::<serde_json::Value>(&payload_str)
                .unwrap_or_else(|_| serde_json::Value::String(payload_str.clone()));

            let _ = app.emit("mqtt-telemetry-update", MqttTelemetryPayload {
                gateway_id,
                address,
                field,
                value: json_value,
            });
        }
    }
}
