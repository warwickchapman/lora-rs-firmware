pub mod commands;
pub mod services;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(crate::commands::monitor::MonitorState {
            running: std::sync::Arc::new(tokio::sync::Mutex::new(false)),
        })
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![
            crate::commands::serial::list_serial_ports,
            crate::commands::flash::flash_firmware,
            crate::commands::github::get_firmware_list,
            crate::commands::status::get_system_status,
            crate::commands::status::get_app_version,
            crate::commands::device::get_device_info,
            crate::commands::monitor::toggle_serial_monitor,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
