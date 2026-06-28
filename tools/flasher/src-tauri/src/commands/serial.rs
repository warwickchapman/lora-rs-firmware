use crate::services::serial;

#[tauri::command]
pub fn list_serial_ports() -> Vec<serial::SerialPortInfo> {
    serial::list_ports()
}
