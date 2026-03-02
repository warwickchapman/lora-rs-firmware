use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone)]
pub struct SystemStatus {
    pub ready: bool,
    pub message: String,
    pub native_drivers_loaded: bool,
}

#[tauri::command]
pub fn get_system_status() -> SystemStatus {
    // Since espflash is compiled-in, the system is technically always ready
    // as long as the backend is running.
    SystemStatus {
        ready: true,
        message: "System Ready".to_string(),
        native_drivers_loaded: true,
    }
}

#[tauri::command]
pub fn get_app_version() -> String {
    // Embed the VERSION file at compile time for maximum reliability
    include_str!("../../../../../VERSION").trim().to_string()
}
