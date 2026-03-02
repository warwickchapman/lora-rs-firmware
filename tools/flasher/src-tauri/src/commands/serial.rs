use crate::services::serial;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone)]
pub struct SerialPortInfo {
    pub port_name: String,
    pub description: Option<String>,
    pub score: i32,
}

#[tauri::command]
pub fn list_serial_ports() -> Vec<SerialPortInfo> {
    serial::list_ports()
        .into_iter()
        .map(|p| SerialPortInfo {
            port_name: p.port_name,
            description: p.description,
            score: p.score,
        })
        .collect()
}
