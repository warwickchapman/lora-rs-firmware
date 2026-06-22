use rumqttd::{Broker, Config};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Clone, Default)]
pub struct MqttBrokerService {
    active_port: Arc<Mutex<Option<u16>>>,
}

impl MqttBrokerService {
    pub async fn get_status(&self) -> Option<u16> {
        *self.active_port.lock().await
    }

    pub async fn start(&self, port: u16) -> Result<Vec<String>, String> {
        let mut active_port_guard = self.active_port.lock().await;
        if let Some(p) = *active_port_guard {
            return Err(format!("Broker is already running on port {}", p));
        }

        // Test if the port is available by binding a TCP listener temporarily on 0.0.0.0
        if std::net::TcpListener::bind(("0.0.0.0", port)).is_err() {
            return Err(format!("Port {} is already in use by another service.", port));
        }

        // Setup rumqttd v0.20 TOML configuration programmatically
        let toml_config = format!(
            r#"
            id = 0

            [router]
            max_connections = 100
            max_outgoing_packet_count = 100
            max_segment_size = 1048576
            max_segment_count = 10

            [v4.1]
            name = "local_v4"
            listen = "0.0.0.0:{}"
            next_connection_delay_ms = 10

            [v4.1.connections]
            connection_timeout_ms = 5000
            max_payload_size = 65536
            max_inflight_count = 100
            dynamic_filters = true
            "#,
            port
        );

        let config: Config = toml::from_str(&toml_config)
            .map_err(|e| format!("Failed to parse broker config TOML: {}", e))?;

        let mut broker = Broker::new(config);
        
        // Spawn broker synchronous loop in a dedicated OS thread
        std::thread::spawn(move || {
            if let Err(e) = broker.start() {
                eprintln!("[Broker] Loop exited with error: {:?}", e);
            }
        });

        // Verify that the broker is reachable with a short retry loop against 127.0.0.1:port
        let mut verified = false;
        let addr = format!("127.0.0.1:{}", port);
        for _ in 0..20 {
            tokio::time::sleep(std::time::Duration::from_millis(50)).await;
            if std::net::TcpStream::connect(&addr).is_ok() {
                verified = true;
                break;
            }
        }

        if !verified {
            return Err(format!("Broker failed to start or was unreachable on port {}", port));
        }

        // Update state
        *active_port_guard = Some(port);

        // Get local LAN IPs
        let mut local_ips = Vec::new();
        if let Ok(interfaces) = get_if_addrs::get_if_addrs() {
            for iface in interfaces {
                if let get_if_addrs::IfAddr::V4(addr) = iface.addr {
                    if !addr.ip.is_loopback() {
                        local_ips.push(addr.ip.to_string());
                    }
                }
            }
        }
        
        // Fallback to loopback if no LAN IP was detected
        if local_ips.is_empty() {
            local_ips.push("127.0.0.1".to_string());
        }

        Ok(local_ips)
    }
}
